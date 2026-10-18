# DODO boot and kernel entry contract

## Boot chain

1. Firmware starts the Limine bootloader from the test ISO.
2. Limine reads `/boot/limine/limine.conf` and loads `/boot/dodo-kernel.elf`.
3. Limine enters the ELF at `_start` in x86-64 long mode using the protocol's higher-half mapping.
4. `_start` disables maskable interrupts, installs DODO's temporary bootstrap stack, clears the frame pointer, and calls `kernel_main`.
5. `kernel_main` initializes the COM1 serial port and validates that the bootloader accepted the requested Limine base revision.
6. This prototype prints status and halts. It does not yet install an IDT, enable a scheduler, allocate memory, or start user processes.

## Protocol contract

The initial prototype uses the Limine boot protocol base revision 3. The start/end request delimiters and base revision tag are retained in a dedicated loadable ELF segment. The bootloader updates the third 64-bit element of the base revision tag to zero when the requested revision is accepted; DODO refuses to continue its bootstrap if the value remains nonzero.

Limine is an external bootstrap dependency, not DODO's kernel. The binary release is pinned by version and SHA-256 checksum in the Makefile.

## ELF memory layout

- Request metadata: writable load segment, because the bootloader updates the base-revision tag.
- `.text`: read + execute.
- `.rodata`: read only.
- `.data` and `.bss`: read + write.
- Kernel virtual base: `0xffffffff80000000`.

These segment flags are an early defense-in-depth choice. Later memory-management work must preserve permission boundaries when DODO takes over page tables.

## Current limitations

- COM1 I/O is used for diagnostics; there is no terminal service.
- Interrupts are disabled in the bootstrap entry path and no IDT is installed yet.
- There is no physical-page allocator or general memory map parser.
- There is no user mode, scheduler, IPC, capability enforcement, filesystem, network stack, or GUI.
- The initial test confirms a boot handoff only; it does not establish that DODO is secure or suitable for daily use.
