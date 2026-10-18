# DODO OS roadmap

This roadmap tracks implemented behavior, not just intended features. A checked box means code exists; milestone acceptance still depends on repeatable tests passing in the latest CI run.

## M0 — Reproducible kernel build

- [x] Build a freestanding x86-64 kernel ELF without linking a host C runtime.
- [x] Add an architecture-specific entry point and dedicated stack.
- [x] Initialize COM1 and print a versioned boot message.
- [x] Halt safely if the kernel returns.
- [x] Reproduce the build in GitHub Actions and inspect the ELF metadata.
- [x] Integrate a pinned Limine boot path and create a BIOS/UEFI test ISO.

**Acceptance:** the kernel ELF builds, QEMU reaches the kernel, the protocol base revision is accepted, and serial output is checked by CI. The kernel is still only a bootstrap prototype.

## M1 — CPU and diagnostics

- [x] Install an initial IDT for CPU exception vectors 0–31.
- [x] Normalize exception stack frames and report breakpoint/page-fault details.
- [x] Add a QEMU breakpoint self-test which resumes through `iretq`.
- [ ] Add a reusable panic/assertion policy and structured fatal diagnostics.
- [ ] Add a timer interrupt and deterministic timer test hooks.
- [ ] Add tests for a controlled page fault and other fatal exceptions.

**Acceptance:** expected test exceptions produce useful diagnostics; fatal exceptions stop safely rather than continuing with corrupted state. The current INT3 self-test verifies dispatch and resumption, not all exception paths.

## M2 — Memory

- [x] Parse and validate the bootloader memory map.
- [x] Implement a bootstrap physical page-frame allocator for complete 4 KiB usable frames.
- [x] Test alignment, exhaustion, null/malformed entries, overflow, and overlapping usable ranges.
- [ ] Add broader allocator stress tests and allocation ownership tracking.
- [ ] Build kernel page tables / virtual-memory mappings and guard pages.
- [ ] Define explicit physical-frame ownership and lifetime rules.

**Acceptance:** stress tests, malformed-map rejection, and mapping permission tests pass. The current allocator returns physical addresses only; it is not a virtual-memory manager and does not support freeing frames.

## M3 — Isolation primitives

- [ ] User/kernel privilege separation.
- [ ] Process address spaces.
- [ ] Context switching and scheduler.
- [ ] IPC with validated message boundaries.
- [ ] Capability table and deny-by-default resource access.

**Acceptance:** a test process cannot read another process's memory or use a resource without a capability.

## M4 — DODO SOMBRA prototype

- [ ] Isolated workload with an explicit resource policy.
- [ ] Filesystem change overlay or equivalent transactional mechanism.
- [ ] Event journal with documented coverage and integrity limits.
- [ ] Review and discard a workload's changes.
- [ ] Repeatable positive and negative security tests.

**Acceptance:** tests prove denied access, isolated writes, accurate journal coverage for the supported operations, and safe discard after normal exit and crash.

## M5 — Useful system services

- [ ] Persistent filesystem and recovery checks.
- [ ] Storage and input drivers.
- [ ] Network service and policy enforcement.
- [ ] Basic graphics and windowing.
- [ ] Update and rollback strategy.
- [ ] Audio, USB, power management, and broader device support.

**Acceptance:** documented hardware compatibility matrix and recovery test suite.

## M6 — Daily-use candidate

- [ ] File manager, settings, browser/application strategy, terminal and development tools.
- [ ] Accessibility and localization.
- [ ] Security update process and signed artifacts.
- [ ] Performance and reliability benchmarks.
- [ ] External review of the threat model and security-sensitive code.

**Acceptance:** a published set of daily-use tasks can be completed repeatedly without data loss on supported hardware.

## M7 — DODO Web (future, separate project phase)

- [ ] Define a shared API and identity model with native DODO.
- [ ] Decide between a local browser workspace and a remotely hosted DODO instance.
- [ ] Specify authentication, isolation, storage, and threat boundaries.
- [ ] Prototype only after native security primitives and API contracts stabilize.

A regular web page cannot boot the native kernel or gain unrestricted access to the computer's hardware. DODO Web must be explicit about whether work happens locally within browser limits or on a remote instance.
