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
