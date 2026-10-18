# DODO OS roadmap

This roadmap tracks implemented behavior, not just intended features. A checked box means code exists; milestone acceptance still depends on repeatable tests passing in the latest CI run.

## M0 — Reproducible kernel build

- [x] Build a freestanding x86-64 kernel ELF without linking a host C runtime.
- [x] Add an architecture-specific entry point and dedicated stack.
- [x] Initialize COM1 and print a versioned boot message.
- [x] Halt safely after the kernel bootstrap checks.
- [x] Reproduce the build in GitHub Actions and inspect ELF metadata.
- [x] Integrate a pinned Limine boot path and create a BIOS/UEFI test ISO.

**Acceptance:** the kernel ELF builds, QEMU reaches the kernel, the protocol base revision is accepted, and serial output is checked by CI. The kernel is still only a bootstrap prototype.

## M1 — CPU and diagnostics

- [x] Install a project-owned GDT while preserving Limine's initial selector layout and reserve user-mode code/data descriptors.
- [x] Install an initial IDT for CPU exception vectors 0–31.
- [x] Normalize exception stack frames and report breakpoint/page-fault details.
- [x] Add a QEMU breakpoint self-test which resumes through iretq.
- [x] Test controlled read-only and NX page faults, resuming only explicitly armed test cases.
- [ ] Add a reusable panic/assertion policy and structured fatal diagnostics.
- [ ] Add a timer interrupt and deterministic timer test hooks.
- [ ] Add tests for more fatal exception paths, including an intentional page fault outside the armed self-test.

**Acceptance:** tested INT3, read-only write, and NX instruction-fetch faults produce expected diagnostics; only the explicitly armed protection tests resume. Other exceptions remain fatal. This does not mean the entire exception model is complete.

## M2 — Memory

- [x] Parse and validate the bootloader memory map.
- [x] Implement a bootstrap physical page-frame allocator for complete 4 KiB usable frames.
- [x] Test alignment, exhaustion, null/malformed entries, overflow, and overlapping usable ranges.
- [x] Track active physical-frame allocations; support free/reuse, reject double frees and unknown frames, and enforce a fixed tracking cap.
- [x] Add validated, non-overlapping regions that the kernel has independently determined safe to reclaim.
- [x] Adopt the protocol-marked bootloader-reclaimable memory after replacing the active page-table tree and GDT/IDT; invalidate Limine response pointers afterward.
- [x] Extend Limine's active 4-level page tables with a checked single-page mapper in an unused kernel PML4 slot.
- [x] Verify physical/direct-map aliases and a new virtual mapping in QEMU.
- [x] Verify read-only and NX page protection behavior in QEMU.
- [x] Verify the mapper rejects remapping a present page and rejects non-canonical virtual addresses.
- [ ] Add broader allocator stress tests, locking/SMP safety, and complete frame ownership transfer rules.
- [x] Recursively clone the active four-level page-table tree into DODO-owned frames while retaining leaf mappings and permissions.
- [x] Preserve shared table references and recursive root mappings during cloning.
- [x] Add allocation-failure injection during page-table cloning and verify rollback restores allocator counts.
- [ ] Track table/frame ownership, reclaim the obsolete bootloader tree safely, and stress rollback failure paths.
- [ ] Build a full virtual-memory manager and guard-page support.
- [ ] Define physical-frame release, ownership transfer, and lifetime rules.

**Acceptance:** host allocator tests cover free/reuse and invalid frees; QEMU checks mapping/protection and a failed-clone rollback. DODO activates a cloned four-level table tree, but retains the same leaf physical mappings and does not yet reclaim the old tree or support per-process address spaces. The allocator is not yet SMP-safe.

## M3 — Isolation primitives

- [ ] Implement a verified privilege-level transition using the reserved ring-3 GDT selectors.
- [ ] User/kernel privilege separation and user stack setup.
- [ ] Process address spaces.
- [ ] Context switching and scheduler.
- [ ] IPC with validated message boundaries.
- [ ] Capability table and deny-by-default resource access.

**Acceptance:** a test process cannot read another process's memory or use a resource without a capability.

## M4 — DODO SOMBRA prototype

- [ ] Isolated workload with an explicit resource policy.
- [ ] Filesystem change overlay or equivalent transactional mechanism.
- [ ] Event journal with documented coverage and integrity limits.
- [ ] Review and discard a workload's changes.
- [ ] Repeatable positive and negative security tests.

**Acceptance:** tests prove denied access, isolated writes, accurate journal coverage for the supported operations, and safe discard after normal exit and crash.

## M5 — Useful system services

- [ ] Persistent filesystem and recovery checks.
- [ ] Storage and input drivers.
- [ ] Network service and policy enforcement.
- [ ] Basic graphics and windowing.
- [ ] Update and rollback strategy.
- [ ] Audio, USB, power management, and broader device support.

**Acceptance:** documented hardware compatibility matrix and recovery test suite.

## M6 — Daily-use candidate

- [ ] File manager, settings, browser/application strategy, terminal and development tools.
- [ ] Accessibility and localization.
- [ ] Security update process and signed artifacts.
- [ ] Performance and reliability benchmarks.
- [ ] External review of the threat model and security-sensitive code.

**Acceptance:** a published set of daily-use tasks can be completed repeatedly without data loss on supported hardware.

## M7 — DODO Web (future, separate project phase)

- [ ] Define a shared API and identity model with native DODO.
- [ ] Decide between a local browser workspace and a remotely hosted DODO instance.
- [ ] Specify authentication, isolation, storage, and threat boundaries.
- [ ] Prototype only after native security primitives and API contracts stabilize.

A regular web page cannot boot the native kernel or gain unrestricted access to the computer's hardware. DODO Web must be explicit about whether work happens locally within browser limits or on a remote instance.
