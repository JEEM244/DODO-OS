# DODO boot and kernel entry contract

## Boot chain

1. Firmware starts the Limine bootloader from the test ISO.
2. Limine reads `/boot/limine/limine.conf` and loads `/boot/dodo-kernel.elf`.
3. Limine enters the ELF at `_start` in x86-64 long mode using the protocol's higher-half mapping.
4. `_start` disables maskable interrupts, installs DODO's temporary bootstrap stack, clears the frame pointer, and calls `kernel_main`.
5. `kernel_main` initializes the COM1 serial port, validates the requested Limine base revision, and checks the memory-map response.
6. DODO validates the response pointer and entry count, rejects null entries, totals the lengths of entries marked usable with overflow checking, prints the summary, and halts.

## Protocol contract

The initial prototype uses the Limine boot protocol base revision 3. The start/end request delimiters and base revision tag are retained in a dedicated loadable ELF segment. The bootloader updates the third 64-bit element of the base revision tag to zero when the requested revision is accepted; DODO refuses to continue its bootstrap if the value remains nonzero.

DODO's private `include/dodo/boot/limine_protocol.h` contains only the memory-map data structures and constants required by this milestone. Limine is an external bootstrap dependency, not DODO's kernel. The binary release is pinned by version and SHA-256 checksum in the Makefile.

## ELF memory layout

- Request metadata: writable load segment, because the bootloader updates the base-revision tag and request response pointers.
- `.text`: read + execute.
- `.rodata`: read only.
- `.data` and `.bss`: read + write.
- Kernel virtual base: `0xffffffff80000000`.

These segment flags are an early defense-in-depth choice. Later memory-management work must preserve permission boundaries when DODO takes over page tables.

## Current limitations

- COM1 I/O is used for diagnostics; there is no terminal service.
- Interrupts are disabled in the bootstrap entry path and no IDT is installed yet.
- The kernel can inspect and summarize the bootloader-provided memory map, but does not yet own a physical-page allocator or its own page tables.
- There is no user mode, scheduler, IPC, capability enforcement, filesystem, network stack, or GUI.
- The QEMU smoke test checks this specific boot handoff and map response only; it does not establish that DODO is secure or suitable for daily use.
