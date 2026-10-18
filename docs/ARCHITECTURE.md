# DODO OS architecture (draft 0.2)

## Goals

DODO is an original operating-system research project intended to become a daily-use desktop with security integrated into its architecture. It is not currently a usable desktop OS. This document records design decisions and open questions, not implemented guarantees.

## Initial platform

- CPU mode: x86-64 long mode.
- First execution target: QEMU; physical PCs come later.
- Initial kernel language: freestanding C; assembly only for architecture-specific entry and low-level operations.
- Build products: a kernel ELF with no host C runtime dependency and a bootable test ISO.
- Initial diagnostics: COM1 serial console.
- Boot protocol: Limine base revision 3, with a pinned binary release and SHA-256 verification. Limine is a bootstrap dependency; DODO's kernel is project-owned.
- Current boot milestone: validate the protocol handoff and memory-map response, initialize a bootstrap physical page allocator, retain four allocated frames for the next memory-management steps, and halt.

## Layers

1. **Kernel:** CPU setup, interrupt/exception handling, physical and virtual memory, scheduling, process isolation, IPC primitives, and capability enforcement.
2. **System services:** filesystem, network, device services, input, audio, graphics, and update/recovery.
3. **Security policy:** grants capabilities to a process or isolated workload; policy decisions should be explicit and testable.
4. **DODO SOMBRA (research):** isolated workloads, resource-access records, change review, and discard/commit workflows.
5. **Desktop and applications:** daily-use workflows, settings, file manager, browser strategy, development tools, and accessibility.
6. **DODO Web (future):** a browser-facing workspace or remote instance using shared APIs. It is not the native kernel running unrestricted inside a browser tab.

## Memory ownership

DODO does not yet own its page tables or virtual-memory mappings. Its bootstrap page allocator returns only physical addresses from complete 4 KiB pages inside ranges marked usable by the Limine memory map. It rejects malformed, overlapping, unaligned, and overflowing ranges instead of trying to guess.

The allocator currently is intentionally simple: no page reclamation, allocation zones, NUMA policies, virtual mappings, zeroing, or ownership transfer. It is a bootstrap component and not an application-facing API.

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
