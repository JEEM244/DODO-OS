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
- Current boot path: validate protocol and memory-map responses, initialize a bootstrap physical page allocator, install DODO's own GDT/TSS/IDT, clone and activate DODO-owned page tables, run controlled CPU/memory protection tests and a single-threaded CPL3 isolation probe, then adopt bootloader-reclaimable memory after boot-response use ends.

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
- Host-side tests cover alignment, exclusion of reserved ranges, invalid responses, overflow, overlaps, exhaustion, freeing, reuse, double-free rejection, unknown-frame rejection, and the tracking cap.
- A compact live-allocation ledger tracks up to 16,384 outstanding frames. A LIFO free stack recycles returned pages before the allocator advances to new ranges.
- Freeing a frame only returns it to the allocator; callers must prove no page table, device, or other live object still references it.
- The allocator is single-CPU bootstrap code with no lock or SMP-safe synchronization yet, and it does not map or dereference physical addresses.
- A checked add-region API admits new page-aligned ranges only after the caller establishes that the region is safe to reclaim; it rejects overlap with known regions.
- A region-targeted allocation path lets the boot test exercise a specific reclaimed range even while older usable regions remain. The test allocates a frame from a new range, returns it, and verifies both the counters and normal recycled-frame reuse.

The current bootstrap reserves five page frames for mapping/protection tests. The page-table tree clone allocates additional frames through the same ledger; a failed clone attempts to release all recorded temporary frames before returning.

## Virtual memory and permissions

The x86-64 paging helper first reads the CR3 root provided by Limine and uses the higher-half direct map to access table pages. During bootstrap, DODO recursively clones the active four-level page-table tree into newly allocated physical frames, preserving leaf physical targets and permissions while redirecting intermediate entries to the cloned tables. Shared table references and recursive root mappings are preserved through an old-to-new table cache; 1 GiB and 2 MiB huge-page leaves are copied without descending into them. DODO activates the cloned root with CR3 only after cloning completes. If the clone fails before activation, it walks the clone cache backward and attempts to return every temporary frame through the allocator's release callback. A QEMU fault-injection check forces allocation to fail partway through cloning and compares free/live page counts before and after rollback. The old Limine table frames are reclaimable only after switching to the cloned tree and installing DODO's own GDT/IDT/stack. The boot path now adopts the protocol-marked bootloader-reclaimable ranges after its final response access, then clears both Limine response pointers. It directly allocates and returns a page from the first newly adopted region to verify the memory is usable. In the tested QEMU configuration, it recovered four regions containing 247 pages; that count is environment-specific. The kernel does not yet reclaim individual obsolete table frames separately or use its own firmware-independent boot path. The mapper refuses unsupported five-level paging, invalid addresses, unknown flags, huge-page collisions, conflicting parent permissions, existing leaf mappings, and reused or unaligned table frames. It also rejects user mappings marked global, because global TLB entries may survive a CR3 switch and could violate address-space isolation when page-global caching is enabled. It rejects both the active root and kernel root as caller-supplied intermediate-table frames so a bad frame pool cannot zero either root. Callers remain responsible for supplying frames not referenced by any other live page table; the bootstrap API is not a global frame-ownership tracker. Its companion unmap operation removes a present 4 KiB leaf, invalidates that virtual address in the TLB, and returns the physical frame and flags without freeing them. It deliberately leaves empty intermediate tables allocated; callers must retain ownership until a future table-reclamation API exists. Page mapping is transactional for intermediate-table links: if allocation runs out or a later level rejects the mapping, any tables created by that call are unlinked and the consumed frame count is returned to the caller for cleanup. A QEMU self-check forces this partial-construction failure and confirms the user PML4 slot stays empty.

It maps a test page into an unused kernel PML4 slot. The QEMU self-test verifies that the direct-map alias and the new virtual alias see the same contents. The self-test rejects its active/kernel root as the writable data frame before mapping or writing, with a regression check that preserves the output parameter on rejection.

Before enforcing supervisor read-only checks, DODO enables CR0.WP. If the CPU supports NX, it enables EFER.NXE. The boot test then demonstrates hardware enforcement:
- reading the test page through a read-only mapping succeeds;
- writing through the read-only mapping raises a page fault with the expected access bits and is recovered only because the test was explicitly armed;
- instruction fetch from an NX mapping raises the expected instruction-fetch page fault and is recovered only in the armed test path.

Unhandled page faults remain fatal. These tests cover particular paths in QEMU; they are not a complete virtual-memory manager or a comprehensive security proof.

Current limitations:
- DODO owns a cloned four-level page-table tree and retains the inherited leaf mappings to the same physical pages. It also uses a project-owned GDT with the Limine entry selector layout preserved.
- DODO still relies on the Limine-provided HHDM layout and does not reclaim the old tree or manage per-process address spaces.
- There is no virtual-memory area manager, demand paging, copy-on-write, per-process address space, or guard-page stack allocator. Frame release is limited to tracked physical pages and requires external proof that a frame is no longer referenced.
- DODO adds test mappings only in an unused kernel PML4 slot; the mapper intentionally never overwrites a present leaf. The initial clone is bounded to 8192 table frames and fails closed if that limit is reached.

## Virtual-address range allocator (initial groundwork)

The host-tested module in `include/dodo/mm/vaddr.h` and `src/mm/vaddr.c` manages a bounded, sorted list of reserved virtual ranges inside a caller-selected canonical low-half interval. It supports page-aligned first-fit allocation, explicit reservation, exact release, overlap rejection, alignment constraints, and a fixed metadata capacity. Host tests cover alignment, overlap, out-of-range spans, exact-release requirements, reuse after release, the upper user-half boundary, range-table capacity, corrupt metadata counts and malformed range records, preservation of the output address on failure, and 10,000 deterministic allocate/release operations with invariants checked after every step.

This module is metadata only: it does not create page-table mappings, allocate physical frames, track page-table ownership, or make concurrent access safe. It is groundwork for a future address-space object, not yet the address-space lifecycle API. Callers must coordinate range metadata with mapping and teardown operations and must not treat releasing metadata as unmapping memory.

## Exception handling

The initial IDT installs CPU exception vectors 0–31. Assembly stubs normalize exception error-code layouts and preserve general-purpose register state before invoking a C handler. One DPL3 gate is installed for the intentional user-mode test interrupt; other software interrupt vectors remain inaccessible from CPL3.

The verified tests cover INT3 dispatch/resumption and intentionally triggered page faults for read-only and NX protection. All other unexpected exceptions halt the kernel and log basic context. IRQs, timer delivery, nested interrupts, IST stacks, and a full panic policy remain future work.

## User-mode proof of concept

DODO now has a deliberately narrow CPL3 probe, not a general process model:

1. The kernel allocates a separate user PML4 root. It starts with the lower-half entries empty and copies the kernel half with the U/S bit forcibly cleared at the PML4 level.
2. The user code page and user stack are mapped only in that user root. A check verifies that the same lower-half PML4 slot remains empty in the kernel root and is present with U/S set in the user root.
3. The TSS supplies a dedicated ring-0 stack for the CPL3-to-CPL0 interrupt transition.
4. User code invokes `int 0x80` through a prototype ABI: `write(pointer, length)`, `exit(status)`, and a deliberate unknown-call probe. The gate is test-only and dispatches only while the controlled user-mode harness is armed.
5. The handler exits the transient user context through a dedicated assembly trampoline. It switches CR3 back to the kernel root and restores the saved kernel stack plus SysV callee-saved registers instead of treating the mixed-privilege interrupt frame as a normal same-level `iretq`.
6. User code is then changed to try to read `0xffffffff80000000`, a supervisor-only kernel virtual address. The page-fault handler resumes only when CR2, CPL3, and the page-fault error bits match that specific expected read; QEMU confirms the access is denied under the separate user root.
7. A second user root is created while the first remains allocated. It maps its own code/stack at different pages but leaves the first root's code virtual address unmapped. Running user code in the second root to read the first root's code address generates the expected user, non-present page fault. The kernel root also remains without that lower-half PML4 entry.
8. After switching back to the kernel CR3, the test explicitly returns the known-owned table, code, stack, and root frames for both user roots. It compares the physical allocator's free/live counters with their values before the test to detect a leak or double free. Root creation also refuses to zero the root currently loaded in CR3; a regression test verifies the active root's existing user mapping survives the rejected request.

This test is single-threaded and uses global one-shot state and fixed test roots. In addition to mapping isolation, the prototype `write` handler validates that a buffer stays in the user half, enforces a small maximum length, verifies each page is present/user-accessible, and copies in page-sized chunks. QEMU checks an actual copy that crosses from the user code page into the user stack page; rejects a kernel pointer, a user-half pointer without a present leaf, a zero-length write, a length above the cap, and a buffer whose length crosses the lower-half address limit; and rejects an unknown syscall number without panicking. The test manually releases its finite set of frames and validates allocator accounting, but DODO still lacks a reusable address-space lifecycle API, per-thread trap context, a hardened kernel-entry ABI, scheduling, signals, and general table-tree ownership/reclamation. This proves only the checked properties in the QEMU configuration; it does not establish that arbitrary user programs are safely isolated yet.

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
