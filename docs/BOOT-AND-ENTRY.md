# DODO boot and kernel entry contract

## Boot chain

1. Firmware starts the Limine bootloader from the test ISO.
2. Limine reads `/boot/limine/limine.conf` and loads `/boot/dodo-kernel.elf`.
3. Limine enters the ELF at `_start` in x86-64 long mode using the protocol's higher-half mapping.
4. `_start` disables maskable interrupts, installs DODO's temporary bootstrap stack, clears the frame pointer, and calls `kernel_main`.
5. The kernel initializes COM1 and loads a project-owned IDT for CPU exceptions 0–31.
6. The kernel validates the boot protocol and memory map, initializes the physical page allocator, and retains four allocated frames for later memory-management work.
7. The kernel executes an intentional `INT3` to test exception dispatch and recovery. The handler logs the breakpoint, returns through `iretq`, and execution continues to the final self-check message.

## Protocol contract

The initial prototype uses the Limine boot protocol base revision 3. The start/end request delimiters and base revision tag are retained in a dedicated loadable ELF segment. The bootloader updates the third 64-bit element of the base revision tag to zero when the requested revision is accepted; DODO refuses to continue its bootstrap if the value remains nonzero.

DODO's private `include/dodo/boot/limine_protocol.h` contains only the memory-map data structures and constants required by this milestone. Limine is an external bootstrap dependency, not DODO's kernel. The binary release is pinned by version and SHA-256 checksum in the Makefile.

## Physical page allocator

`include/dodo/mm/phys.h` exposes the bootstrap allocator interface; `src/mm/phys.c` implements it without requiring the C runtime.

- Uses only entries explicitly marked usable by the boot protocol.
- Aligns ranges inward so every returned address identifies a complete 4 KiB page.
- Rejects null entries, excessive entry counts, arithmetic overflow, and overlapping usable ranges.
- Tracks remaining and allocated frame counts.
- Returns physical addresses only. It does not create mappings, zero memory, or make a physical address directly dereferenceable.
- Has host-side unit tests in `tests/phys_allocator_test.c`, plus a self-check during QEMU boot.

This is a simple bootstrap allocator, not a complete memory manager. It has no page reclamation, zones, NUMA policy, virtual-memory mapping, demand paging, or allocation ownership beyond the sequential frame list.

## Early exception IDT

`src/arch/x86_64/idt.c` installs 16-byte IDT gates for exception vectors 0–31. `src/arch/x86_64/interrupts.S` provides stubs that normalize exceptions with and without CPU error codes and save general-purpose registers before calling the C handler.

- IDT gates use the code selector active at kernel entry instead of assuming a specific selector value.
- A breakpoint self-test verifies dispatch and the return path through `iretq`.
- Unhandled exceptions print the vector name, error code, and instruction pointer, then halt. Page faults additionally report CR2.
- The handler is a ring-0 bootstrap mechanism; it does not yet support user-mode transitions or interrupt-driven scheduling.

Maskable interrupts stay disabled. Hardware interrupt controllers, timer delivery, interrupt nesting, IST stacks, user-mode exception frames, and full panic/recovery policy remain future work.

## ELF memory layout

- Request metadata: writable load segment, because the bootloader updates the base-revision tag and request response pointers.
- `.text`: read + execute.
- `.rodata`: read only.
- `.data` and `.bss`: read + write.
- Kernel virtual base: `0xffffffff80000000`.

These segment flags are an early defense-in-depth choice. Later memory-management work must preserve permission boundaries when DODO takes over page tables.

## Current limitations

- COM1 I/O is used for diagnostics; there is no terminal service.
- No timer interrupt or hardware interrupt delivery is enabled.
- The kernel can consume physical frame addresses but does not yet own its page tables or a virtual-memory manager.
- There is no user mode, scheduler, IPC, capability enforcement, filesystem, network stack, or GUI.
- The QEMU smoke test checks the tested boot path, frame allocator, and intentional breakpoint only; it does not establish that DODO is secure or suitable for daily use.
