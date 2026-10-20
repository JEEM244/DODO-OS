# x86-64 context-switching design

Status: design note only. The current scheduler stores task metadata; it does **not** switch CPU execution. Do not describe this milestone as implemented until the kernel actually resumes two independent task entry points in QEMU.

## Current interrupt-frame ABI

`src/arch/x86_64/interrupts.S` saves general-purpose registers in this push order: RAX, RBX, RCX, RDX, RBP, RDI, RSI, R8, R9, R10, R11, R12, R13, R14, R15. Since the stack grows downward, the C-visible frame begins at R15 and ends at RFLAGS, with vector and error code between the saved registers and the CPU return frame.

The fixed prefix is declared by `struct dodo_x86_64_exception_frame` in `include/dodo/arch/x86_64/interrupts.h`. Its static assertions are part of the ABI and must remain aligned with the assembly. CPU exceptions that push an error code and stubs that synthesize one must continue to produce the same normalized frame shape.

For an interrupt that occurs in ring 0, the CPU return frame is RIP, CS, and RFLAGS. A privilege transition from ring 3 additionally pushes the previous RSP and SS. The existing controlled ring-3 return path deliberately abandons that transient frame; do not reuse it as an ordinary kernel-task return path.

## Required design before enabling preemption

1. **One owned kernel stack per task.** Allocate and track each stack explicitly; establish alignment and a guard strategy before enabling untrusted task code. Do not pass arbitrary nonzero addresses as if they were owned stacks.
2. **A documented initial frame.** Build a synthetic interrupt frame on a new task's stack that exactly matches the assembly pop order and the intended `iretq` return frame. Define a task-entry trampoline that calls the entry function and routes its return value into the task-exit path.
3. **Timer dispatch contract.** IRQ0 must save the interrupted frame, account for the timer tick, choose a runnable task, and return the selected task's frame. The interrupt path must send the PIC EOI exactly once. It must not use the exception handler's existing integer return values as a stack pointer.
4. **Atomic scheduler transition.** Interrupts must not observe a partially updated `current_id`, task state, or saved frame. Keep the first implementation single-CPU and explicitly disable interrupts around scheduler metadata changes; SMP locking is a separate milestone.
5. **Address-space policy.** Do not load `address_space_root` into CR3 merely because the field exists. Until ownership and shared-kernel mappings are fully specified, task switching must either stay within a known shared address space or use a separately reviewed address-space switch path.
6. **Exit and idle behavior.** A task must never resume after becoming ZOMBIE. If no task is runnable, switch to a dedicated idle context rather than returning to a dead task or leaving `current_id` inconsistent.
7. **ABI and compiler constraints.** Preserve the kernel's `-mno-red-zone` and general-register-only assumptions. Keep the interrupt-frame C structure, assembly save/restore sequence, and synthetic-frame builder under the same tests.

## Acceptance tests

The context-switching milestone is incomplete until all of these pass:

- Host tests validate task lifecycle and reject invalid scheduler state.
- A QEMU test starts at least two kernel tasks with independent stacks and distinct entry functions.
- Each task emits a unique serial marker multiple times; the output proves both tasks make progress after timer preemption, not just sequential function calls.
- A task that returns is marked ZOMBIE and is never scheduled again; its exit code can be read and its slot reaped safely.
- A blocked task does not run until woken, and the idle path works when all tasks are blocked.
- Timer IRQs continue advancing, the shell remains responsive, and the existing ring-3 isolation and exception tests still pass.
- CI builds the ISO and verifies these runtime assertions from QEMU serial output.

## Implementation order

1. Introduce an architecture-specific saved-frame type and a compile-time layout contract.
2. Add owned kernel-stack allocation and rollback-safe task creation.
3. Add and test the synthetic first-run frame and entry trampoline without timer preemption.
4. Extend the interrupt return path to restore a selected frame.
5. Connect IRQ0 to the scheduler behind a boot-time self-test switch.
6. Enable preemption only after all context, exit, idle, and regression tests pass.

This ordering intentionally favors a verifiable kernel over a premature context switch that can corrupt the active stack or return through an invalid CPU frame.
