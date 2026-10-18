# DODO boot and kernel entry contract

## Boot chain

1. Firmware starts the Limine bootloader from the test ISO.
2. Limine reads /boot/limine/limine.conf and loads /boot/dodo-kernel.elf.
3. Limine enters the ELF at _start in x86-64 long mode using its higher-half mapping.
4. The assembly entry disables maskable interrupts, installs DODO's bootstrap stack, clears the frame pointer, and calls kernel_main.
5. The kernel initializes COM1 and loads a project-owned IDT for CPU exception vectors 0–31.
6. The kernel validates the base protocol revision, memory-map response and HHDM response.
7. DODO's physical allocator prepares complete usable 4 KiB frames. Five pages are reserved for table/mapping/protection self-tests, and another frame is allocated for DODO's PML4 root.
8. The paging helpers read CR3, verify four-level paging (rejecting LA57), enable CR0.WP, and enable EFER.NXE when the CPU supports it.
9. DODO copies the current PML4 entries to its own allocated root frame and activates it with CR3. This preserves current mappings but continues to use the inherited lower-level tables.
10. DODO creates mappings in an unused kernel PML4 slot. It verifies alias consistency, rejects an attempt to replace an existing mapping and a non-canonical address, then intentionally tests read-only and NX enforcement.
11. An INT3 test verifies exception dispatch and return through iretq. The kernel halts after successful bootstrap checks.

## Protocol contract

The prototype uses Limine base revision 3. The start/end request delimiters and base-revision tag are retained in a dedicated loadable ELF segment. Limine updates the third 64-bit element of the base-revision tag to zero when the requested revision is accepted.

DODO's include/dodo/boot/limine_protocol.h contains only the memory-map and HHDM request/response structures used by this milestone. Limine is an external bootstrap dependency, not DODO's kernel. The binary release is pinned by version and SHA-256 checksum in the Makefile.

## Physical page allocator

include/dodo/mm/phys.h exposes the bootstrap allocator interface; src/mm/phys.c implements it without requiring the C runtime.

- Uses only entries explicitly marked usable by the boot protocol.
- Aligns usable ranges inward so each returned address identifies a complete 4 KiB page.
- Rejects null entries, excessive entry counts, arithmetic overflow, and overlapping usable ranges.
- Tracks remaining and allocated frame counts.
- Returns physical addresses only; it does not make them directly dereferenceable, create mappings, zero data, or support freeing.
- Has host-side tests covering alignment, exclusion of reserved ranges, malformed maps, overflow, overlap and exhaustion.

## Virtual memory mapper and protection tests

include/dodo/arch/x86_64/paging.h and src/arch/x86_64/paging.c provide a deliberately small x86-64 mapping primitive.

- Copies the current CR3 PML4 into a new frame allocated by DODO, activates that root, and uses the Limine HHDM to access the inherited lower-level table frames.
- Supports four-level paging only; rejects LA57 (five-level paging).
- Maps one aligned 4 KiB page in an unused kernel PML4 slot and never overwrites an existing leaf mapping.
- Rejects unsupported flags, invalid/unaligned addresses, huge-page conflicts, conflicting parent permissions and repeated table frames.
- Enables CR0.WP before tests so supervisor code cannot silently bypass read-only leaf protection.
- Enables EFER.NXE if supported, and only then runs the NX execution test.
- The controlled test traps only page faults whose address and error bits match the currently armed test. Any unrelated page fault remains fatal.

The QEMU smoke test proves the specific assertions in this path:
- the direct-map and test virtual aliases see the same data;
- write access through the read-only mapping is blocked;
- instruction fetch through an NX mapping is blocked when NX is supported;
- the handler resumes only the intentionally triggered faults.

DODO owns the active PML4 root but still inherits the subordinate page-table hierarchy. This is not a complete virtual-memory system: no full subordinate-table ownership, user-mode spaces, allocation reclamation, demand paging, copy-on-write, or guard-page stack allocator exists yet.

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
- DODO owns the top-level PML4 root but preserves Limine's current subordinate mappings; it does not yet own a fully independent page-table tree.
- There is no user mode, scheduler, IPC, capability enforcement, filesystem, network stack, or GUI.
- The QEMU smoke test covers the tested paths only; it does not establish that DODO is secure or suitable for daily use.
