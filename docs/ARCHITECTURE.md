# DODO OS architecture (draft 0.3)

## Goals

DODO is an original operating-system research project intended to become a daily-use desktop with security integrated into its architecture. It is not currently a usable desktop OS. This document records design decisions and open questions, not implemented guarantees.

## Initial platform

- CPU mode: x86-64 long mode, four-level paging for the first target.
- First execution target: QEMU; physical PCs come later.
- Initial kernel language: freestanding C; assembly only for architecture-specific entry and low-level operations.
- Build products: a kernel ELF with no host C runtime dependency and a bootable test ISO.
- Initial diagnostics: COM1 serial console.
- Boot protocol: Limine base revision 3, with a pinned binary release and SHA-256 verification. Limine is a bootstrap dependency; DODO's kernel is project-owned.
- Current boot path: validate protocol and memory-map responses, initialize a bootstrap physical page allocator, install an initial exception IDT, take ownership of a copied PML4 root, and run controlled CPU/memory protection tests.

## Layers

1. **Kernel:** CPU setup, interrupt/exception handling, physical and virtual memory, scheduling, process isolation, IPC primitives, and capability enforcement.
2. **System services:** filesystem, network, device services, input, audio, graphics, and update/recovery.
3. **Security policy:** grants capabilities to a process or isolated workload; policy decisions should be explicit and testable.
4. **DODO SOMBRA (research):** isolated workloads, resource-access records, change review, and discard/commit workflows.
5. **Desktop and applications:** daily-use workflows, settings, file manager, browser strategy, development tools, and accessibility.
6. **DODO Web (future):** a browser-facing workspace or remote instance using shared APIs. It is not the native kernel running unrestricted inside a browser tab.

## Physical memory

DODO's bootstrap frame allocator lives in include/dodo/mm/phys.h and src/mm/phys.c.

- It only accepts ranges explicitly marked usable by the Limine memory map.
- It selects complete 4 KiB frames by aligning range start upward and end downward.
- It rejects malformed maps, overflow, duplicate/overlapping usable ranges, and capacity violations.
- Host-side tests cover alignment, exclusion of reserved ranges, invalid responses, overflow, overlaps, and exhaustion.
- The allocator returns physical addresses. It does not make them directly dereferenceable or map/free/reclaim them.

The current bootstrap reserves five page frames: three for intermediate page tables, one for a writable test page, and one for a read-only/NX protection page. These frames remain allocated during the test.

## Virtual memory and permissions

The x86-64 paging helper first reads the CR3 root provided by Limine and uses the higher-half direct map to access table pages. During bootstrap, DODO recursively clones the active four-level page-table tree into newly allocated physical frames, preserving leaf physical targets and permissions while redirecting intermediate entries to the cloned tables. Shared table references and recursive root mappings are preserved through an old-to-new table cache; 1 GiB and 2 MiB huge-page leaves are copied without descending into them. DODO activates the cloned root with CR3 only after cloning completes. The old table frames are not reclaimed because their ownership and lifetime are not yet tracked. The mapper refuses unsupported five-level paging, invalid addresses, unknown flags, huge-page collisions, conflicting parent permissions, existing leaf mappings, and reused or unaligned table frames.

It maps a test page into an unused kernel PML4 slot. The QEMU self-test verifies that the direct-map alias and the new virtual alias see the same contents.

Before enforcing supervisor read-only checks, DODO enables CR0.WP. If the CPU supports NX, it enables EFER.NXE. The boot test then demonstrates hardware enforcement:
- reading the test page through a read-only mapping succeeds;
- writing through the read-only mapping raises a page fault with the expected access bits and is recovered only because the test was explicitly armed;
- instruction fetch from an NX mapping raises the expected instruction-fetch page fault and is recovered only in the armed test path.

Unhandled page faults remain fatal. These tests cover particular paths in QEMU; they are not a complete virtual-memory manager or a comprehensive security proof.

Current limitations:
- DODO owns a cloned four-level page-table tree and retains the inherited leaf mappings to the same physical pages.
- DODO still relies on the Limine-provided HHDM layout and does not reclaim the old tree or manage per-process address spaces.
- There is no virtual-memory area manager, allocation reclamation, demand paging, copy-on-write, per-process address space, or guard-page stack allocator.
- DODO adds test mappings only in an unused kernel PML4 slot; the mapper intentionally never overwrites a present leaf. The initial clone is bounded to 8192 table frames and fails closed if that limit is reached.

## Exception handling

The initial IDT installs CPU exception vectors 0–31. Assembly stubs normalize exception error-code layouts and preserve general-purpose register state before invoking a C handler.

The verified tests cover INT3 dispatch/resumption and intentionally triggered page faults for read-only and NX protection. All other unexpected exceptions halt the kernel and log basic context. IRQs, timer delivery, nested interrupts, IST stacks, privilege transitions, and a full panic policy remain future work.

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
