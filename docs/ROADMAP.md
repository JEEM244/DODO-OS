# DODO OS roadmap

The dates are deliberately omitted: progress depends on learning, hardware support, testing, and the size of the contributor team. A milestone is complete only when its acceptance checks pass.

## M0 — Reproducible kernel build

- [ ] Build a freestanding x86-64 kernel ELF without linking a host C runtime.
- [ ] Add an architecture-specific entry point and dedicated stack.
- [ ] Initialize COM1 and print a versioned boot message.
- [ ] Halt safely if the kernel returns.
- [ ] Document toolchain versions and reproduce the build in CI.
- [ ] Add a documented boot protocol and QEMU boot image in a later substep.

**Acceptance:** clean build from a fresh checkout; inspectable ELF; serial output reproducible in QEMU after boot integration. No disk writes to the host.

## M1 — CPU and diagnostics

- [ ] Define panic and assertion paths.
- [ ] Install exception/interrupt descriptor tables.
- [ ] Handle at least breakpoint and page-fault diagnostics.
- [ ] Add a timer interrupt and deterministic test hooks.

**Acceptance:** intentionally triggered test exceptions produce useful diagnostics and do not silently continue.

## M2 — Memory

- [ ] Parse bootloader memory map.
- [ ] Physical page allocator.
- [ ] Kernel virtual-memory mappings and guard pages.
- [ ] Explicit ownership and lifetime rules.

**Acceptance:** allocator stress tests, invalid-free detection in debug builds, and memory-map validation.

## M3 — Isolation primitives

- [ ] User/kernel privilege separation.
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
