# DODO boot and kernel entry contract

## Boot chain

1. Firmware starts the Limine bootloader from the test ISO.
2. Limine reads /boot/limine/limine.conf and loads /boot/dodo-kernel.elf.
3. Limine enters the ELF at _start in x86-64 long mode using its higher-half mapping.
4. The assembly entry disables maskable interrupts, installs DODO's bootstrap stack, clears the frame pointer, and calls kernel_main.
5. The kernel initializes COM1 and loads DODO's own GDT and TSS. It preserves Limine's entry selector layout (CS 0x28, data selectors 0x30), reserves ring-3 selectors, and installs a dedicated RSP0 stack for privilege transitions.
6. The kernel loads a project-owned IDT for CPU exception vectors 0–31 and one DPL3 gate for the controlled vector-128 test, then validates the base protocol revision, memory-map response and HHDM response.
7. DODO's physical allocator prepares complete usable 4 KiB frames. Five pages are reserved for mapping/protection tests; the page-table tree clone and user-mode probe allocate additional tracked frames.
8. The paging helpers read CR3, verify four-level paging (rejecting LA57), enable CR0.WP, and enable EFER.NXE when the CPU supports it.
9. DODO recursively clones the active four-level page-table tree into its own allocated frames, preserves the leaf mappings, and activates the new root with CR3 only after cloning succeeds. A fault-injection test also confirms temporary frames are returned if a clone fails.
10. DODO creates mappings in an unused kernel PML4 slot. It verifies alias consistency, rejects an attempt to replace an existing mapping and a non-canonical address, then intentionally tests read-only and NX enforcement.
11. The CPL3 probe maps a small user-code page and user stack, executes iretq into ring 3, calls the DPL3 int 0x80 gate, and returns by abandoning the transient interrupt frame and restoring the saved kernel stack/registers. It then tries to read a supervisor-only kernel virtual address; QEMU confirms the CPU raises the expected user protection fault and that the test handler recovers.
12. An INT3 test verifies exception dispatch and return through iretq.
13. After the last boot-response access, DODO adopts the protocol-marked bootloader-reclaimable regions into its physical allocator, allocates and frees a page from an adopted region to check accounting, and nulls its memory-map/HHDM response pointers. The kernel halts after successful bootstrap checks.

## Controlled ring-3 proof of concept

The test is deliberately small and single-threaded:

- GDT selectors for user code/data and a 64-bit TSS descriptor are installed by DODO. TSS.RSP0 points to a dedicated ring-0 interrupt stack.
- A DPL3 interrupt gate at vector 128 permits only the intentional software interrupt test; the handler checks a one-shot armed flag, CPL3, and the expected marker in RAX.
- DODO allocates a separate user PML4 root. Its low-half entries start empty; the kernel-half entries are copied with U/S cleared at the PML4 level so CPL3 cannot access any high-half address through those entries. The user code page and user stack are mapped only into this root.
- A boot self-check confirms that the chosen user PML4 slot is still empty in the kernel root but present and U/S-enabled in the first user root.
- The code executes the software interrupt and is returned to the kernel through a trampoline that restores the kernel CR3, original kernel stack, and SysV callee-saved registers.
- The same user page is then filled with a short read instruction targeting 0xffffffff80000000. The page-fault handler only returns to the kernel when CR2, saved CPL, and error-code access bits match the expected supervisor-page read fault.
- A second root is built with its own code and stack mappings, leaving the first root's code virtual address absent. User code in the second root reads that address and must fault with a non-present user-page error; the kernel root must still lack the corresponding low-half PML4 entry.
- The QEMU smoke test requires serial markers for the separate user root, successful ring-3 entry/return, supervisor-memory denial, and cross-root mapping denial.

This is not a general syscall ABI. The boot probe creates two user roots but uses global one-shot state and a single fixed TSS.RSP0 stack; it has no scheduler, SMP safety, per-thread trap context, per-process kernel stacks, user-page/table cleanup, syscall argument validation, or capability enforcement. Each root shares supervisor-only higher-half mappings with the kernel while keeping its lower-half mappings separate. Unexpected faults remain fatal.

## Protocol contract

The prototype uses Limine base revision 3. The start/end request delimiters and base-revision tag are retained in a dedicated loadable ELF segment. Limine updates the third 64-bit element of the base-revision tag to zero when the requested revision is accepted.

DODO's include/dodo/boot/limine_protocol.h contains only the memory-map and HHDM request/response structures used by this milestone. Limine is an external bootstrap dependency, not DODO's kernel. The binary release is pinned by version and SHA-256 checksum in the Makefile.

## Physical page allocator

include/dodo/mm/phys.h exposes the bootstrap allocator interface; src/mm/phys.c implements it without requiring the C runtime.

- Uses only entries explicitly marked usable by the boot protocol.
- Aligns usable ranges inward so each returned address identifies a complete 4 KiB page.
- Rejects null entries, excessive entry counts, arithmetic overflow, and overlapping usable ranges.
- Tracks remaining pages, outstanding allocations, and a bounded recycled-frame stack.
- Supports returning a tracked frame and reusing it; rejects unaligned, unknown, and duplicate frees.
- Returns physical addresses only; it does not make them directly dereferenceable, create mappings, or zero data.
- Has host-side tests covering alignment, malformed maps, overflow, overlap, exhaustion, freeing/reuse, frame zero, added regions, and allocation-ledger capacity.
- Reclaiming bootloader regions is gated on the caller's lifetime decision; the boot path only does so after activating its own page-table clone and GDT/IDT.
- The boot path explicitly allocates one frame through the newly added region index, checks that its address lies inside the reclaimed range, frees it again, and verifies the allocator counters remain unchanged.

## Virtual memory mapper and protection tests

include/dodo/arch/x86_64/paging.h and src/arch/x86_64/paging.c provide a deliberately small x86-64 mapping primitive.

- Recursively clones the four-level table hierarchy into DODO-owned frames, preserves leaf mappings, handles shared references and recursive root mappings, and activates the new root only after the copy succeeds. Uses the Limine HHDM to access source and destination tables during bootstrap.
- If cloning fails before CR3 activation, calls the frame-release callback for cached clone frames in reverse order. QEMU injects an allocation failure after two successful frame allocations and checks that free/live allocator counts are restored.
- Supports four-level paging only; rejects LA57 (five-level paging).
- Maps one aligned 4 KiB page in an unused kernel PML4 slot and never overwrites an existing leaf mapping.
- Rejects unsupported flags, invalid/unaligned addresses, huge-page conflicts, conflicting parent permissions and repeated table frames.
- Enables CR0.WP before tests so supervisor code cannot silently bypass read-only leaf protection.
- Enables EFER.NXE if supported, and only then runs the NX execution test.
- The controlled test traps only page faults whose address and error bits match the currently armed test. Any unrelated page fault remains fatal. After the bootloader-reclaimable regions have been added, the Limine response pointers are set to NULL to prevent accidental reads through stale response metadata.

The QEMU smoke test proves the specific assertions in this path:
- the direct-map and test virtual aliases see the same data;
- write access through the read-only mapping is blocked;
- instruction fetch through an NX mapping is blocked when NX is supported;
- the handler resumes only the intentionally triggered faults.

DODO owns a clone of the four-level page-table hierarchy, but retains the same leaf physical mappings and does not reclaim the previous Limine tree. The physical allocator supports freeing tracked frames, but the kernel does not yet reclaim old table frames or provide a general virtual-memory area manager. Per-process spaces, demand paging, copy-on-write, and guard-page stack allocation are still absent.

## Early exception IDT

src/arch/x86_64/idt.c installs 16-byte IDT gates for vectors 0–31. src/arch/x86_64/interrupts.S provides stubs that normalize exceptions with and without CPU error codes and preserve general-purpose registers before calling the C handler.

The verified tests exercise INT3 resumption and two controlled page-fault cases (read-only write and NX instruction fetch). Unexpected exceptions print the vector, error code and instruction pointer, and halt; page faults additionally report CR2. Maskable interrupts stay disabled.

Hardware interrupt controllers, timer delivery, interrupt nesting, IST stacks, user-mode exception frames, and a comprehensive panic/recovery policy remain future work.

## ELF memory layout

- Request metadata: writable load segment because the bootloader updates the base-revision tag and request response pointers.
- .text: read + execute.
- .rodata: read only.
- .data and .bss: read + write.
- Kernel virtual base: 0xffffffff80000000.

These segment flags are defense in depth. Later memory-management work must preserve page permissions when DODO takes over its own page tables.

## Current limitations

- COM1 is used for diagnostics; there is no terminal service.
- There is no timer or external interrupt delivery.
- The GDT is owned by DODO, but ring-3 entry/return instructions and actual user-mode processes are not implemented yet.
- DODO owns the cloned page-table structure but preserves Limine's leaf mappings and HHDM layout; it does not yet maintain its own physical-frame reclamation or per-process mapping policy.
- There is no user mode, scheduler, IPC, capability enforcement, filesystem, network stack, or GUI.
- The QEMU smoke test covers the tested paths only; it does not establish that DODO is secure or suitable for daily use.
