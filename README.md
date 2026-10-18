# DODO OS

**DODO is an experimental operating-system project built around a kernel of its own and security as a core system property.**

The goal is not to create another Linux distribution or a collection of security utilities. DODO aims to become a practical daily-use desktop operating system whose architecture limits what programs can access, makes their actions explainable, and allows risky work to happen in isolated environments.

> **Current status:** early project scaffold. The first milestone is a bootable x86-64 kernel prototype. DODO is not yet a usable operating system, and security guarantees are not claimed until they are implemented and tested.

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

## First target

- Architecture: x86-64.
- Boot path: UEFI through the Limine boot protocol for the initial prototype.
- Implementation: freestanding C and small amounts of x86-64 assembly only where required.
- Development environment: QEMU before physical hardware.
- Initial observable result: boot a DODO kernel and write a versioned status message to a serial console.
- Initial safety boundary: no disk installation, no privileged host access, and no real network tools.

Using a bootloader is a deliberate bootstrap choice. DODO's kernel and operating-system services remain our own work; writing a bootloader, every driver, and all desktop applications at the same time would slow the first useful milestone without improving its security argument.

## Roadmap

See [docs/ROADMAP.md](docs/ROADMAP.md) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

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
├── docs/                 # Architecture, threat model, roadmap, contribution notes
├── src/kernel/           # Own freestanding kernel code
├── linker/               # Kernel link layout
├── config/               # Bootloader configuration
├── scripts/              # Build/run helpers
└── .github/workflows/    # Reproducible CI checks
```

## Development status

Nothing in the current scaffold should be considered production secure. The first goal is correctness and observability, then isolation primitives, and only later a desktop environment. Until the relevant checks exist and pass, do not install DODO on a daily-use machine.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) and [SECURITY.md](SECURITY.md). Please report vulnerabilities privately when possible; do not publish exploit details for a real affected system before maintainers can investigate.
