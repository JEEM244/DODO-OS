# DODO OS

**DODO is an experimental operating-system project with its own kernel and security as a core system property.**

The goal is not to create another Linux distribution or a collection of security utilities. DODO aims to become a practical daily-use desktop operating system whose architecture limits what programs can access, makes their actions explainable, and allows risky work to happen in isolated environments.

> **Current status:** the repository now contains a freestanding x86-64 kernel entry, a pinned Limine boot path, an ISO build, and a QEMU serial smoke test. This is only an early kernel prototype, not a desktop OS. Check the latest GitHub Actions run before assuming the build or boot test has passed.

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

- Architecture: x86-64.
- Boot path: Limine boot protocol using a pinned upstream binary release and checksum.
- Implementation: freestanding C and a small x86-64 assembly entry point.
- Link layout: higher-half kernel with separate request, executable, read-only, and writable ELF segments.
- Diagnostics: COM1 serial output.
- Test environment: QEMU before physical hardware.
- Safety boundary: no disk installation, no privileged host access, and no real network tools.

Limine is deliberately used only to bootstrap the kernel. DODO's kernel and future operating-system services remain our own work; developing a custom bootloader, every driver, and all desktop applications at the same time would delay the first useful milestones.

## Build and test

Read [docs/BUILDING.md](docs/BUILDING.md) before building.

```sh
make clean all       # build kernel and bootable test ISO
make run             # launch the ISO in QEMU
make qemu-smoke      # verify kernel handoff via serial output
```

The initial kernel prints diagnostic messages and halts. A blank graphical display is expected until graphics support exists. The automated workflow also uploads the ISO as a short-lived artifact.

## Roadmap

See [docs/ROADMAP.md](docs/ROADMAP.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), and [docs/BOOT-AND-ENTRY.md](docs/BOOT-AND-ENTRY.md).

1. **Kernel bring-up:** boot, serial diagnostics, panic path, and repeatable build.
2. **CPU and memory:** interrupt handling, physical/virtual memory, allocator, scheduler, and isolation.
3. **Security primitives:** capability model, IPC, process boundaries, and policy tests.
4. **SOMBRA prototype:** isolated workload, change journal, review, and discard tests.
5. **Useful system services:** storage, networking, input, graphics, and update/recovery.
6. **Daily-use evaluation:** hardware support, reliability, usability, application strategy, and independent testing.
7. **Future DODO Web:** a separate browser-accessible workspace or remote environment. A normal browser cannot run the native kernel with unrestricted hardware access; the web experience will be designed as a separate access path later.

## Repository layout

```text
.
├── config/                  # Limine boot menu
├── docs/                    # Architecture, build, threat model, and roadmap
├── src/arch/x86_64/          # Architecture-specific entry code
├── src/kernel/              # Freestanding kernel source
├── linker/                  # ELF load layout
├── Makefile                 # Kernel, ISO, and QEMU targets
└── .github/workflows/       # Build and boot smoke test
```

## Security status

DODO is experimental and must not be used to protect sensitive data or as a production security boundary. A successful boot test only proves that the tested artifact reaches a particular point; it does not prove the absence of vulnerabilities. Do not install early builds on a daily-use computer.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) and [SECURITY.md](SECURITY.md). Please report vulnerabilities privately when possible; do not publish exploit details for a real affected system before maintainers can investigate.
