# DODO OS

**DODO is an experimental operating-system project with its own kernel and security as a core system property.**

The goal is not to create another Linux distribution or a collection of security utilities. DODO aims to become a practical daily-use desktop operating system whose architecture limits what programs can access, makes their actions explainable, and allows risky work to happen in isolated environments.

> **Current status:** DODO has a freestanding x86-64 kernel, a pinned Limine boot path, a BIOS/UEFI test ISO, a bootstrap physical page allocator, an initial exception IDT, and a checked extension of the active page tables. GitHub Actions runs host allocator tests and boots the ISO in QEMU, including read-only and NX protection tests. This remains an early kernel prototype, not a desktop OS. Check the latest CI run before treating any milestone as verified.

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
- **Boot path:** Limine protocol base revision 3, using a pinned upstream binary release verified by SHA-256.
- **Implementation:** freestanding C and x86-64 assembly.
- **Physical memory:** validates the bootloader map and allocates complete 4 KiB physical frames from usable ranges.
- **Virtual memory:** allocates and activates a DODO-owned PML4 root while copying the current entries, then maps test pages in an unused kernel PML4 slot. The lower-level tables are still inherited from Limine.
- **Page protections:** explicitly enables CR0.WP; when supported, enables EFER.NXE. QEMU tests verify read-only/NX faults, rejection of non-canonical addresses, and refusal to overwrite a present mapping.
- **Exceptions:** IDT entries for vectors 0–31, fatal exception diagnostics, page-fault CR2 reporting, and tested recovery for intentionally triggered faults.
- **Diagnostics:** COM1 serial output.
- **Test environment:** QEMU before physical hardware.
- **Safety boundary:** no disk installation, no privileged host access, and no real network tools.

Limine is deliberately used only to bootstrap the kernel. DODO's kernel and future operating-system services remain our own work; developing a custom bootloader, every driver, and all desktop applications at the same time would delay the first useful milestones.

## Build and test

Read [docs/BUILDING.md](docs/BUILDING.md) before building.

~~~sh
make test            # host-side physical allocator tests
make clean all       # build kernel and bootable test ISO
make run             # launch the ISO in QEMU
make qemu-smoke      # verify boot, page protections, and exception recovery
~~~

The prototype prints diagnostic messages over COM1 serial output and eventually halts after its self-tests. A blank graphical display is expected: DODO does not yet have graphics support or a desktop.

## Roadmap

See [docs/ROADMAP.md](docs/ROADMAP.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/BUILDING.md](docs/BUILDING.md), and [docs/BOOT-AND-ENTRY.md](docs/BOOT-AND-ENTRY.md).

1. **Kernel bring-up:** repeatable boot, serial diagnostics, and exception handling.
2. **Memory:** physical frames and an owned PML4 root are in place; next are independent lower-level tables, a full virtual-memory manager, and guard pages.
3. **Isolation primitives:** user/kernel separation, IPC, scheduler, and capability enforcement.
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
