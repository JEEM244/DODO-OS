# DODO OS

**DODO is an experimental operating-system project with its own kernel and security as a core system property.**

The goal is not to create another Linux distribution or a collection of security utilities. DODO aims to become a practical daily-use desktop operating system whose architecture limits what programs can access, makes their actions explainable, and allows risky work to happen in isolated environments.

> **Current status:** DODO has a freestanding x86-64 kernel, a pinned Limine boot path, a BIOS/UEFI test ISO, a tracked physical page allocator, a host-tested bounded virtual-range metadata allocator (not yet wired to page tables), a project-owned GDT/TSS/IDT, and a cloned DODO-owned four-level page-table tree. Successful QEMU smoke tests cover separate CPL3 roots, supervisor-memory denial, test-root teardown and allocator accounting, plus a test-only `int 0x80` `write`/`exit` ABI. The prototype rejects kernel pointers, unmapped user pointers, zero-length and oversized writes, buffers that cross the user-half address limit, and unknown call numbers; it also verifies copying a user buffer across two mapped pages. The kernel now also remaps the legacy PIC, counts PIT timer interrupts at approximately 100 Hz, and accepts PS/2 keyboard input through IRQ1; `uptime` exposes the tick count. `make qemu-keyboard-smoke` injects real virtual key events through QEMU's monitor and checks the shell response. A host-tested fixed-capacity round-robin task-state module tracks READY/RUNNING/BLOCKED/ZOMBIE tasks, supports zombie reaping and slot reuse, and validates saving a stack pointer only for the currently running task. It is not connected to interrupt-driven context switching and does not run independent tasks. This is still an early kernel prototype: the syscall interface is not a stable process ABI, and DODO is not yet a desktop OS or a complete process-isolation system.

## Project principles

1. **Own kernel:** DODO's kernel code is written for this project. Existing build tools and a third-party bootloader may be used during development; this does not make DODO a Linux distribution.
2. **Security by architecture:** least privilege, explicit capabilities, process isolation, and auditable policy decisions should be foundational rather than bolted on later.
3. **Daily-use target:** networking, files, audio, graphics, USB, updates, recovery, and common applications matter as much as security tools.
4. **Evidence over slogans:** every milestone must have reproducible tests. Security and performance claims must identify their threat model, limitations, and benchmark conditions.
5. **No unsafe shortcuts:** early builds run in an emulator or disposable test media. DODO must never overwrite a user's disk as part of a development test.

## Research direction: DODO SOMBRA

DODO SOMBRA is the working name for a future security architecture combining isolated application environments, least-privilege resource access, and traceable changes.

The initial research question is:

**Can DODO connect an application's authorized capabilities to a reliable, user-readable history of resource access and file changes, while allowing changes from an isolated environment to be reviewed and discarded?**

This is a research hypothesis, not a claim of unprecedented invention. Existing work on capability systems, sandboxing, disposable environments, and transactional filesystems is relevant prior art and must be studied.

## Current kernel milestone

- **Architecture:** x86-64 long mode, 4-level page tables.
- **Boot path:** Limine protocol base revision 3, using a pinned upstream binary release verified by SHA-256. DODO installs its own GDT/TSS while preserving Limine's entry selector layout, provides a dedicated ring-0 interrupt stack, and reserves ring-3 code/data selectors.
- **Implementation:** freestanding C and x86-64 assembly.
- **Physical memory:** validates the bootloader map and allocates complete 4 KiB physical frames from usable ranges. A bounded ownership ledger tracks live frames; the allocator can release/reuse them, reject double frees and unknown pages, and add checked, non-overlapping reclaimed regions. After switching to DODO-owned tables and GDT/IDT, the kernel adds Limine's reclaimable regions, proves it can allocate and return a frame from one, and clears response pointers so freed metadata isn't reused accidentally.
- **Virtual address ranges:** a host-tested, bounded first-fit allocator reserves and releases aligned low-half ranges, rejects overlaps, and enforces a metadata-capacity limit. It is not yet connected to page-table mappings or physical-frame ownership.
- **Virtual memory:** clones the active four-level page-table tree into DODO-owned frames, switches CR3 to the new root, preserves leaf mappings, and attempts rollback if cloning fails before activation. QEMU currently reports ten cloned table frames for its test memory map.
- **Page protections:** explicitly enables CR0.WP; when supported, enables EFER.NXE. QEMU tests verify read-only/NX faults, rejection of non-canonical addresses, and refusal to overwrite a present mapping.
- **Address-space/syscall prototype:** DODO creates separate user PML4 roots with empty low halves and supervisor-only kernel-half mappings. QEMU verifies cross-root read denial, test-root teardown, and allocator accounting. The test-only CPL3 `int 0x80` ABI handles a bounded `write` and `exit`; QEMU checks cross-page copying and rejects kernel pointers, unmapped user pointers, zero-length and oversized writes, user-half boundary-crossing buffers, and unknown call numbers. It remains single-threaded and has no stable public syscall ABI or CPU-integrated scheduler; the host-tested task-state module does not switch CPU execution.
- **Diagnostics:** COM1 serial output.
- **Test environment:** QEMU before physical hardware.
- **Safety boundary:** no disk installation, no privileged host access, and no real network tools.

Limine is deliberately used only to bootstrap the kernel. DODO's kernel and future operating-system services remain our own work; developing a custom bootloader, every driver, and all desktop applications at the same time would delay the first useful milestones.

## Build and test

Read [docs/BUILDING.md](docs/BUILDING.md) before building. Windows users can follow [the CMD + QEMU quick start](docs/WINDOWS-QUICKSTART.md) and launch the prototype with `RUN-DODO-WINDOWS.bat`.

~~~sh
make test            # host-side physical and virtual-range allocator tests
make clean all       # build kernel and bootable test ISO
make run             # launch the ISO in QEMU
make qemu-smoke      # verify boot, memory protections, isolation, and syscall checks
make qemu-keyboard-smoke # verify PS/2 typing and Backspace through QEMU
make qemu-panic-smoke # verify invalid-opcode/page-fault panic diagnostics
make verify          # run all host and QEMU acceptance tests in sequence (including ELF W^X check)
~~~

The prototype now renders an initial text console on the RGB framebuffer provided by Limine and mirrors all output to COM1. The shell accepts both PS/2 keyboard input and serial input. Commands include `help`, `about`, `version`, `mem`, `memcheck`, `uptime`, `sleep N`, `echo TXT`, `clear`, and `halt`. This is a framebuffer text console, not yet a graphical desktop or window manager.

## Roadmap

See [docs/ROADMAP.md](docs/ROADMAP.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/BUILDING.md](docs/BUILDING.md), and [docs/BOOT-AND-ENTRY.md](docs/BOOT-AND-ENTRY.md).

1. **Kernel bring-up:** repeatable boot, serial diagnostics, and exception handling.
2. **Memory:** DODO owns a cloned four-level page-table tree; next are frame reclamation/ownership, a full virtual-memory manager, and guard pages.
3. **Isolation primitives:** separate test roots and a narrow validated syscall prototype are verified; next are reusable address-space lifecycles, a stable kernel-entry ABI, IPC, scheduling, and capability enforcement.
4. **SOMBRA prototype:** isolated workload, change journal, review, and discard tests.
5. **Useful system services:** storage, networking, input, graphics, and update/recovery.
6. **Daily-use evaluation:** hardware support, reliability, usability, application strategy, and independent testing.
7. **Future DODO Web:** a separate browser-accessible workspace or remote environment. A normal browser cannot run the native kernel with unrestricted hardware access; the web experience will be designed as a separate access path later.

## Repository layout

~~~text
.
├── config/                  # Limine boot menu
├── docs/                    # Architecture, build, threat model, and roadmap
├── include/dodo/            # Kernel-owned public headers
├── src/arch/x86_64/          # Entry, IDT, paging, and exception stubs
├── src/kernel/              # Main kernel bootstrap
├── src/mm/                  # Physical frame allocator
├── tests/                   # Host-side unit tests
├── linker/                  # ELF load layout
├── Makefile                 # Kernel, ISO, and test targets
└── .github/workflows/       # Build and QEMU boot tests
~~~

## Security status

DODO is experimental and must not be used to protect sensitive data or as a production security boundary. The checked mapping tests show specific hardware-enforced behaviors in QEMU; they do not prove the absence of vulnerabilities or establish a complete memory-protection design. Do not install early builds on a daily-use computer.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) and [SECURITY.md](SECURITY.md). Please report vulnerabilities privately when possible; do not publish exploit details for a real affected system before maintainers can investigate.
