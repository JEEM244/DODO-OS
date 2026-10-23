# Address-space lifecycle: implementation contract

Status: implementation contract; this document does not claim the lifecycle API exists yet.

## Why this comes before CPU preemption

DODO can create and test separate user page-table roots, but cleanup currently relies on a test-specific list of frames. A general task cannot safely exit, fail during creation, or be rescheduled until the kernel knows exactly which frames it owns. Implementing timer-driven context switching before this is settled risks leaking frames, freeing shared kernel tables, or loading an invalid CR3.

## Required invariants

1. **Explicit ownership.** Every allocated root/table/private user frame is recorded by the address-space object that owns it. A mapping alone does not imply ownership: shared higher-half kernel mappings and their tables must never be freed by a user address space.
2. **Transactional creation.** Creation either returns a fully initialized address space or leaves physical allocator counts unchanged. Every failure point must roll back only frames acquired by that attempt.
3. **Active-root protection.** An address space currently loaded in CR3 cannot be destroyed. Teardown must switch to a known kernel root first and verify the target root is no longer active.
4. **Validated user mappings.** User mappings must stay in the lower canonical half, carry the user bit at every relevant paging level, and never expose supervisor mappings. Reject unsupported page sizes and flags rather than silently weakening permissions.
5. **Unmap before release.** A private frame may be released only after its leaf mapping is removed and stale translations are invalidated. Shared kernel frames and bootloader/HHDM aliases are not private user frames.
6. **Bounded metadata.** Until a kernel heap exists, ownership records have a fixed capacity. Capacity exhaustion fails closed and rolls back; it must not silently forget ownership.
7. **No double release.** Teardown is idempotence-safe in the sense that a second destroy attempt is rejected without touching freed frames. Unknown, duplicate, or malformed ownership records fail validation.
8. **Single-CPU first.** Document that the first implementation is not SMP-safe. Disable interrupts around transitions that mutate active-root and ownership state; SMP locking is a separate milestone.

## Existing primitive now available

`dodo_vaddr_validate(const struct dodo_vaddr_space *)` is now a public validation entry point for the metadata-only low-half range allocator. It returns `DODO_VADDR_OK` only when the interval, reservation count, alignment, ordering, bounds, and non-overlap invariants are valid; malformed metadata returns `DODO_VADDR_INVALID_ARGUMENT`. This is a consistency check, not proof that hardware page tables match reservations, and it does not allocate or free physical memory. Host tests call it during the deterministic range-allocation stress loop and inject malformed overlapping records.

### Alias and lifetime restrictions

The metadata registry allows multiple borrowed aliases only when the external owner guarantees the frame's lifetime. A frame owned by this address space cannot also appear as a borrowed mapping in the same space, and an owned mapping cannot alias any other mapping. Without a reference-counted shared-frame object, allowing either case could leave a live mapping pointing at a released frame. Cross-address-space sharing still requires a future explicit lifetime protocol.

The user null page is excluded from address-space intervals. The generic virtual allocator remains capable of representing address zero for non-process uses, but process address spaces must start at or above one page.

### Read-only hardware mapping inspection

`dodo_x86_64_query_page()` walks the active four-level page tables and reports the leaf physical frame and allowed flags without mutating entries. It rejects non-canonical or unaligned virtual addresses, absent leaves, and huge-page walks; output parameters remain unchanged on failure. Kernel boot self-checks compare the query result with known kernel and user mappings and verify that a successful unmap becomes visible to the query. This is a diagnostic primitive, not yet an automatic cross-check performed by the metadata registry.

## Metadata registry prototype

`include/dodo/mm/address_space.h` and `src/mm/address_space.c` now provide a host-testable metadata layer. It validates lower-half user mapping flags, a bounded reservation table, owner-scoped `USER_DATA` frames for owned mappings, borrowed-versus-owned policy, sorted mapping records, lookup, and removal bookkeeping. Writable mappings must also be NX; unsupported cache/global flags are rejected.

This prototype deliberately does **not** write page-table entries, switch CR3, invalidate TLB entries, allocate page-table frames, or destroy a page-table tree. The caller must first successfully install a mapping with the low-level paging API before recording it, and must unmap it before forgetting it. It is a bookkeeping prerequisite, not the reusable hardware address-space lifecycle requested by the full milestone.
## Suggested API boundary

Use an opaque or tightly controlled address-space structure and return status codes, not raw assumptions:

- dodo_address_space_create(...): allocate a root and initialize the documented shared kernel-half references; rollback on every failure.
- dodo_address_space_map_user(...): reserve a valid user range, allocate/accept a frame under an explicit ownership policy, and map with user permissions.
- dodo_address_space_unmap_user(...): remove the mapping, invalidate the relevant TLB entry, and release only frames marked as owned.
- dodo_address_space_validate(...): verify root validity, canonical ranges, ownership uniqueness, table/frame bounds, and that no shared kernel object is listed as privately owned.
- dodo_address_space_destroy(...): reject the active root; remove private mappings, release owned user frames and private page-table frames bottom-up, then release the root last.

These names describe the intended contract, not functions currently present in the codebase. Keep the existing paging primitives as the low-level layer; do not make this module recursively free arbitrary page-table trees until table ownership is explicit.

## Test gates

### Host tests

- Reject null, duplicate, out-of-range, and over-capacity ownership records.
- Inject allocation failure at every creation step and prove live/free frame counters return to baseline.
- Reject double-free and attempts to claim a shared kernel table as private.
- Verify failed map/unmap operations preserve the prior valid ownership state.

### QEMU tests

- Create two address spaces with private mappings at the same virtual address and prove their physical frames differ.
- Confirm a user root cannot access supervisor-only kernel mappings or another root's private pages.
- Destroy an inactive root and prove all privately owned frames return to baseline.
- Reject destruction of the active CR3 root without changing its mappings.
- Repeat create/map/unmap/destroy cycles to expose leaks and double frees.
- Keep all existing BIOS/UEFI boot, keyboard, timer, panic, W^X, syscall-probe, and scheduler-metadata tests passing.

## Required implementation order

1. Audit the current paging helpers and physical allocator contracts.
2. Add the bounded ownership-record primitive with host tests.
3. Implement transactional address-space create/destroy using only explicitly owned frames.
4. Connect the virtual-range allocator to validated user map/unmap operations.
5. Add repeated QEMU lifecycle and allocator-accounting tests.
6. Only then build owned task stacks and CPU context switching on top of the lifecycle API.

Until these gates pass, DODO must continue describing user isolation as a tested prototype rather than a complete process-isolation system.

### Stable address-space identity

Assign each live address space a nonzero monotonically allocated ID. IDs are diagnostic identities, not physical addresses, and must not be reused while any task, capability, or log record can still refer to the old object.

### Explicit object states

Use a finite state machine such as EMPTY, BUILDING, READY, ACTIVE, and DESTROYING. Reject transitions not explicitly permitted; in particular, a partially built object must never be switchable into CR3.

### Output initialization

Initialize caller-visible output objects only after validating arguments. On any failed create operation, leave the output in a documented empty state and restore all acquired resources.

### Root-frame ownership

A newly allocated root frame belongs to the address-space owner from the moment allocation succeeds. Record ownership immediately, before any later operation can fail.

### Private table classification

Distinguish private lower-half paging tables from shared higher-half kernel tables in metadata. Teardown may reclaim only tables explicitly recorded as private.

### Mapping ownership policy

Every user mapping must declare whether its leaf frame is owned by this address space or borrowed. Borrowed frames are unmapped but never freed by address-space teardown.

### Map operation ordering

For a new mapping, validate range and permissions first, acquire and record required table frames, then publish page-table entries, then commit the virtual reservation. Roll back in reverse order if any step fails.

### Unmap operation ordering

For unmapping, clear the leaf entry, invalidate the local TLB translation, remove the virtual reservation, and release an owned leaf frame only after all preceding steps succeed.

### Local TLB invalidation

The first implementation may use INVLPG for a single page while running on the owning CPU. Any future SMP implementation must add cross-CPU shootdown before reclaiming a frame.

### Active-root exclusion

Destroy must reject both the hardware-active CR3 root and any object marked ACTIVE. A successful switch must verify CR3 readback before updating software state.

### Permission validation

Reject user mappings that request global pages, unsupported cache attributes, noncanonical addresses, ranges crossing the user limit, or writable-executable permissions when the policy forbids them. Never silently mask invalid caller flags into a weaker request.

### Ancestor permission audit

Before accepting a user leaf, verify every traversed PML4, PDPT, and PD entry permits user access. If an ancestor is supervisor-only, reject the mapping rather than widening a shared or existing table.

### No implicit huge-page support

Treat present huge-page entries in user map/unmap traversal as a distinct unsupported condition until 2 MiB and 1 GiB mapping semantics, alignment, and teardown are separately implemented and tested.

### Reservation-map agreement

The virtual-range reservation table and hardware page tables must agree at every committed API boundary. Reservations are metadata only and must never be presented as proof that a page is mapped.

### Bounded ownership capacity

Before allocating any frame, reserve enough ownership-record capacity for the whole operation or guarantee rollback for each allocation. Never allocate a frame and then lose its identity because the ledger is full.

### Duplicate frame rejection

An address-space ownership record must reject a physical frame already recorded for the same or another owner. Aliasing may be supported later through a separate reference-counted mapping object, not duplicate ownership.

### Destroy validation preflight

Run a full metadata and allocator-consistency validation before teardown starts. If validation fails, refuse destructive cleanup and report the exact inconsistency; do not guess which frames are safe to release.

### Destroy postorder

Walk private page-table branches bottom-up. Remove user leaf mappings and invalidate translations before freeing leaf frames; release a table only after no present entry refers to its child table.

### Shared kernel mapping contract

A user root may share supervisor-only kernel-half top-level entries, but the address-space owner must not record those shared descendants as private. The kernel remains responsible for their lifetime.

### Root release last

Release the root frame only after the address space is inactive, all private leaf mappings are gone, all private lower-half tables are reclaimed, and software references to the object are detached.

### Creation failure injection

Host tests should inject allocation failure at every allocation index in address-space creation. Each run must prove that live physical-frame counts, ledger counts, and virtual reservations return to their pre-call values.

### Map failure injection

For every map step that can fail, inject the failure and assert that no leaf mapping remains, no virtual reservation was committed, and every table frame acquired by the failed operation is either released or still explicitly owned.

### Unmap failure preservation

Test invalid address, absent leaf, malformed metadata, wrong owner, and unsupported huge-page cases. Failures must preserve output parameters and must not release physical frames.

### Repeated lifecycle stress

Run deterministic create-map-unmap-destroy cycles across multiple owners and reuse the same virtual addresses. Validate the ownership ledger and physical allocator after every transition.

### Two-root same-address test

Create two inactive roots and map the same virtual address to different private physical frames. Switch between them and verify each root observes only its own test value.

### Active destroy negative test

Attempt to destroy the root loaded in CR3 and verify rejection without changing CR3, page-table entries, owner records, or allocator counters.

### Shared-kernel preservation test

Create and destroy multiple user roots while checking representative kernel code, stack, and direct-map translations remain present and supervisor-only in each user root.

### Capacity exhaustion test

Fill the virtual reservation table and ownership ledger to their documented limits. The next operation must return a capacity status without modifying existing records or leaking frames.

### Corrupted-ledger negative test

Corrupt one ownership record in a host-only test fixture and verify validate, map, unmap, and destroy refuse to proceed destructively. Preserve caller outputs on rejection.

### TLB reuse test

After unmapping a private page, invalidate its translation before releasing or reusing its physical frame. Re-map the same virtual address to a different frame and verify the new content is observed.

### Status code separation

Use status values that distinguish invalid arguments, malformed state, capacity exhaustion, allocation failure, mapping conflict, inactive-root requirements, and rollback failure. Do not collapse rollback failure into an ordinary allocation error.

### Caller-owned object storage

Until a kernel heap exists, let the caller provide address-space object storage or allocate it from a documented fixed pool. Keep metadata storage separate from physical page-table frames so object lifetime is explicit.

### No heap dependency

The first lifecycle implementation must work without malloc, recursion-sized variable allocations, or unbounded metadata. Fixed capacities and deterministic rollback are required for bootstrap use.

### Transactional reservation updates

Virtual reservation updates should be staged or performed only at the final commit point. If mapping fails, callers must not need to infer whether the range was reserved.

### Frame-kind requirements

Use distinct frame kinds for roots, page-table pages, user data, and kernel stacks. Teardown must reject a record whose kind does not match the operation being performed.

### Owner identity checks

Every release operation must verify both owner ID and physical frame. A caller that knows a frame address but does not own it must not be able to free it through the address-space API.

### Zeroed page-table frames

Zero each newly allocated page-table frame before linking it into a reachable hierarchy. If zeroing or mapping prerequisites fail, keep it unreachable and roll back ownership.

### No root mutation before validation

Validate the target root frame and its expected ownership kind before writing any root entries or loading it into CR3. Reject roots that alias the active root or a shared kernel object.

### Switch transition protocol

Switching roots should validate READY state, root alignment, physical-address bounds, and shared kernel mapping assumptions before writing CR3. Update software active-state metadata only after CR3 readback succeeds.

### Interrupt critical sections

For the single-CPU implementation, protect multi-step updates to active-root state and ownership records from local interrupt re-entry where required. This is not a substitute for SMP locks or remote TLB shootdown.


## Implemented runtime wrapper (prototype)

`include/dodo/mm/address_space_runtime.h` and `src/mm/address_space_runtime.c` now provide a single-CPU runtime wrapper around the existing metadata registry, owned-frame adapter, and x86-64 paging API.

The wrapper supports:
- Create a user root with an empty lower half and shared supervisor-only kernel mappings.
- Allocate an owned user-data frame and up to three candidate intermediate table frames per mapping.
- Record metadata before installing the hardware leaf, then roll metadata and allocated frames back if mapping fails.
- Query the active leaf before unmapping and require its physical frame and leaf flags to match the metadata record.
- Retain intermediate page-table frames after unmap because the current low-level unmapper does not prune empty tables.
- Destroy an inactive address space by unmapping every recorded leaf, releasing owned data frames, then releasing retained table frames and the root.

The ownership ledger capacity is now 512 records so the bootstrap registry can represent a root, user pages, and a bounded set of retained page-table frames. The runtime refuses new mappings before its table-frame tracking array can overflow.

This remains a prototype, not a process manager: it is single-CPU, has no reference-counted shared user frames, does not reclaim empty intermediate tables on each unmap, and does not provide scheduler/context-switch integration. The boot self-check and host mock tests cover ordinary lifecycle, rollback, metadata/hardware mismatch, capacity, and teardown. A passing CI/QEMU run is still required before treating it as verified.
