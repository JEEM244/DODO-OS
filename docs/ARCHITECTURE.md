# DODO OS architecture (draft 0.1)

## Goals

DODO is an original operating-system research project intended to become a daily-use desktop with security integrated into its architecture. It is not currently a working OS. This document records design decisions and open questions, not implemented guarantees.

## Initial platform

- CPU mode: x86-64 long mode.
- First execution target: QEMU.
- Initial kernel language: freestanding C; assembly only for architecture-specific entry and low-level operations.
- Build products: a kernel ELF with no host C runtime dependency.
- Initial diagnostics: serial console (COM1).
- Boot protocol: select and integrate a documented x86-64 boot protocol after the minimal kernel build is repeatable. The first scaffold is not yet a bootable image.

## Layers

1. **Kernel:** CPU setup, interrupt/exception handling, physical and virtual memory, scheduling, process isolation, IPC primitives, and capability enforcement.
2. **System services:** filesystem, network, device services, input, audio, graphics, and update/recovery.
3. **Security policy:** grants capabilities to a process or isolated workload; policy decisions should be explicit and testable.
4. **DODO SOMBRA (research):** isolated workloads, resource-access records, change review, and discard/commit workflows.
5. **Desktop and applications:** daily-use workflows, settings, file manager, browser strategy, development tools, and accessibility.
6. **DODO Web (future):** a browser-facing workspace or remote instance using shared APIs. It is not the native kernel running unrestricted inside a browser tab.

## Security model principles

- Deny by default for privileged resources.
- Grant the smallest capability needed, for the shortest practical duration.
- Keep policy enforcement outside untrusted application code.
- Separate security decisions from UI prompts; the UI explains decisions but cannot override kernel enforcement.
- Treat logs as evidence with defined integrity and coverage, not proof that every possible event is visible.
- Failure of a nonessential service should not automatically grant additional privileges.
- A sandbox is not a guarantee that malware is harmless; network access, side channels, kernel bugs, and hardware vulnerabilities remain in the threat model.

## SOMBRA hypothesis

Research whether a system can associate capability grants, resource operations, and filesystem changes with a workload identity in a reliable, understandable journal, then safely discard or selectively retain changes.

Before implementation, define:
- the exact threat model;
- what counts as a recorded event and which events can be missed;
- how journals resist tampering;
- how changes are isolated and merged;
- how crash recovery works;
- benchmarks against a clearly named baseline;
- prior art in capability systems, sandboxing, disposable environments, audit systems, and transactional filesystems.

Do not describe SOMBRA as novel or secure until comparative research and independent tests support those claims.

## Non-goals for the first milestone

- Running Windows applications.
- GPU acceleration.
- A polished desktop.
- Wi-Fi, USB, audio, or broad device support.
- A production threat-monitoring system.
- Installing to or modifying a host disk.
- A browser-based substitute for the native kernel.
