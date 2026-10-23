#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "dodo/mm/address_space.h"

static void make_usable_map(struct dodo_limine_memmap_entry *usable,
                            struct dodo_limine_memmap_entry **entries,
                            struct dodo_limine_memmap_response *map,
                            uint64_t pages) {
    usable->base = UINT64_C(0x1000);
    usable->length = pages * DODO_PHYS_PAGE_SIZE;
    usable->type = DODO_LIMINE_MEMMAP_USABLE;
    entries[0] = usable;
    map->revision = 0;
    map->entry_count = 1;
    map->entries = entries;
}

struct fixture {
    struct dodo_limine_memmap_entry usable;
    struct dodo_limine_memmap_entry *entries[1];
    struct dodo_limine_memmap_response map;
    struct dodo_phys_allocator allocator;
    struct dodo_owned_frames frames;
    struct dodo_address_space space;
};

static void fixture_init(struct fixture *f) {
    make_usable_map(&f->usable, f->entries, &f->map, 16);
    assert(dodo_phys_init(&f->allocator, &f->map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&f->frames, &f->allocator);
    assert(dodo_address_space_init(&f->space, &f->frames, 77,
                                   UINT64_C(0x400000),
                                   UINT64_C(0x800000)) == DODO_ADDRESS_SPACE_OK);
}

static void test_external_free_is_detected_before_metadata_mutation(void) {
    struct fixture f;
    fixture_init(&f);
    uint64_t frame = 0;
    assert(dodo_owned_frames_alloc(&f.frames, 77, DODO_FRAME_KIND_USER_DATA,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x417000),
        frame, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_phys_free_page(&f.allocator, frame) == DODO_PHYS_OK);
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OWNERSHIP_ERROR);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x417000)) ==
        DODO_ADDRESS_SPACE_OWNERSHIP_ERROR);
    assert(f.space.mapping_count == 1u && f.space.virtual_space.count == 1u);
}
static void test_frame_pool_corruption_propagates_as_ownership_error(void) {
    struct fixture f;
    fixture_init(&f);
    f.allocator.allocated_pages = DODO_PHYS_MAX_TRACKED_PAGES + 1u;
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OWNERSHIP_ERROR);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x40d000),
        UINT64_C(0x1000), DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_OWNERSHIP_ERROR);
    f.allocator.allocated_pages = 0u;
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
}
static void test_validation_detects_duplicate_owned_frame_corruption(void) {
    struct fixture f;
    fixture_init(&f);
    uint64_t first = 0;
    uint64_t second = 0;
    assert(dodo_owned_frames_alloc(&f.frames, 77, DODO_FRAME_KIND_USER_DATA,
                                   &first) == DODO_OWNED_FRAMES_OK);
    assert(dodo_owned_frames_alloc(&f.frames, 77, DODO_FRAME_KIND_USER_DATA,
                                   &second) == DODO_OWNED_FRAMES_OK);
    const uint64_t flags = DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE;
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x418000),
        first, flags, DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x419000),
        second, flags, DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_OK);
    f.space.mappings[1].physical_address = first;
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OWNERSHIP_ERROR);
    f.space.mappings[1].physical_address = second;
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x419000)) ==
           DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x418000)) ==
           DODO_ADDRESS_SPACE_OK);
}
static void test_reservation_and_mapping_addresses_must_match(void) {
    struct fixture f;
    fixture_init(&f);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x415000),
        UINT64_C(0x1000), DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_OK);
    f.space.mappings[0].virtual_address = UINT64_C(0x416000);
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_INVALID_STATE);
    f.space.mappings[0].virtual_address = UINT64_C(0x415000);
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
}
static void test_corrupt_mapping_flags_are_rejected(void) {
    struct fixture f;
    fixture_init(&f);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x414000),
        UINT64_C(0x1000), DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_OK);
    f.space.mappings[0].flags |= DODO_PAGE_GLOBAL;
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_INVALID_STATE);
    f.space.mappings[0].flags = DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE;
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
}
static void test_stale_unused_mapping_slot_is_rejected(void) {
    struct fixture f;
    fixture_init(&f);
    f.space.mappings[DODO_ADDRESS_SPACE_MAX_MAPPINGS - 1u].physical_address =
        UINT64_C(0x1000);
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_INVALID_STATE);
    f.space.mappings[DODO_ADDRESS_SPACE_MAX_MAPPINGS - 1u] =
        (struct dodo_address_space_mapping){0};
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
}
static void test_corrupt_metadata_fails_closed(void) {
    struct fixture f;
    fixture_init(&f);
    f.space.mapping_count = 1u;
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_INVALID_STATE);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x40c000),
        UINT64_C(0x1000), DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_INVALID_STATE);
    struct dodo_address_space_mapping out = { .virtual_address = UINT64_C(0xabc000) };
    assert(dodo_address_space_lookup(&f.space, UINT64_C(0x40c000), &out) ==
           DODO_ADDRESS_SPACE_INVALID_STATE);
    assert(out.virtual_address == UINT64_C(0xabc000));
    f.space.mapping_count = 0u;
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
}
static void test_init_requires_initialized_physical_allocator(void) {
    struct dodo_phys_allocator allocator = {0};
    struct dodo_owned_frames frames;
    struct dodo_address_space space;
    dodo_owned_frames_init(&frames, &allocator);
    assert(dodo_address_space_init(&space, &frames, 77,
        UINT64_C(0x400000), UINT64_C(0x800000)) ==
        DODO_ADDRESS_SPACE_OWNERSHIP_ERROR);
}
static void test_init_refuses_corrupt_frame_ledger(void) {
    struct fixture f;
    make_usable_map(&f.usable, f.entries, &f.map, 8);
    assert(dodo_phys_init(&f.allocator, &f.map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&f.frames, &f.allocator);
    f.frames.ledger.count = 1u;
    assert(dodo_address_space_init(&f.space, &f.frames, 77,
        UINT64_C(0x400000), UINT64_C(0x800000)) ==
        DODO_ADDRESS_SPACE_OWNERSHIP_ERROR);
}
static void test_init_rejects_invalid_owner_id(void) {
    struct fixture f;
    make_usable_map(&f.usable, f.entries, &f.map, 8);
    assert(dodo_phys_init(&f.allocator, &f.map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&f.frames, &f.allocator);
    assert(dodo_address_space_init(&f.space, &f.frames, DODO_FRAME_OWNER_INVALID,
        UINT64_C(0x400000), UINT64_C(0x800000)) ==
        DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
}
static void test_null_arguments_are_rejected(void) {
    struct fixture f;
    fixture_init(&f);
    assert(dodo_address_space_init(NULL, &f.frames, 77,
        UINT64_C(0x400000), UINT64_C(0x800000)) ==
        DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
    assert(dodo_address_space_init(&f.space, NULL, 77,
        UINT64_C(0x400000), UINT64_C(0x800000)) ==
        DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
    assert(dodo_address_space_validate(NULL) == DODO_ADDRESS_SPACE_INVALID_STATE);
    assert(dodo_address_space_lookup(&f.space, UINT64_C(0x400000), NULL) ==
        DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
    assert(dodo_address_space_record_mapped_page(NULL, UINT64_C(0x400000),
        UINT64_C(0x1000), DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
    assert(dodo_address_space_forget_unmapped_page(NULL, UINT64_C(0x400000)) ==
        DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
}
static void test_init_rejects_invalid_user_interval(void) {
    struct fixture f;
    make_usable_map(&f.usable, f.entries, &f.map, 8);
    assert(dodo_phys_init(&f.allocator, &f.map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&f.frames, &f.allocator);
    assert(dodo_address_space_init(&f.space, &f.frames, 77,
        UINT64_C(0x800000), UINT64_C(0x400000)) ==
        DODO_ADDRESS_SPACE_RANGE_ERROR);
    assert(dodo_address_space_init(&f.space, &f.frames, 77,
        UINT64_C(0), UINT64_C(0x400000)) ==
        DODO_ADDRESS_SPACE_RANGE_ERROR);
    assert(dodo_address_space_init(&f.space, &f.frames, 77,
        UINT64_C(0x00007fffffff0000), UINT64_C(0x0000800000001000)) ==
        DODO_ADDRESS_SPACE_RANGE_ERROR);
}
static void test_init_and_empty_validation(void) {
    struct fixture f;
    fixture_init(&f);
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
    assert(f.space.mapping_count == 0u);
    assert(f.space.virtual_space.count == 0u);
}

static void test_owned_mapping_record_and_forget(void) {
    struct fixture f;
    fixture_init(&f);
    uint64_t frame = 0;
    assert(dodo_owned_frames_alloc(&f.frames, 77, DODO_FRAME_KIND_USER_DATA,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x401000),
        frame, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_WRITABLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
    assert(f.space.mapping_count == 1u && f.space.virtual_space.count == 1u);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x401000)) ==
           DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
    assert(f.space.mapping_count == 0u && f.allocator.allocated_pages == 0u);
}

static void test_validation_rejects_borrowed_record_of_local_frame(void) {
    struct fixture f;
    fixture_init(&f);
    uint64_t frame = 0;
    assert(dodo_owned_frames_alloc(&f.frames, 77, DODO_FRAME_KIND_USER_DATA,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x41e000),
        UINT64_C(0x2000), DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_OK);
    f.space.mappings[0].physical_address = frame;
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OWNERSHIP_ERROR);
    f.space.mappings[0].physical_address = UINT64_C(0x2000);
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x41e000)) ==
           DODO_ADDRESS_SPACE_OK);
    assert(dodo_owned_frames_release(&f.frames, 77, frame) == DODO_OWNED_FRAMES_OK);
}
static void test_locally_owned_frame_cannot_be_marked_borrowed(void) {
    struct fixture f;
    fixture_init(&f);
    uint64_t frame = 0;
    assert(dodo_owned_frames_alloc(&f.frames, 77, DODO_FRAME_KIND_USER_DATA,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x40ff000),
        frame, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_OWNERSHIP_ERROR);
    assert(f.space.mapping_count == 0u);
    assert(dodo_owned_frames_release(&f.frames, 77, frame) == DODO_OWNED_FRAMES_OK);
}
static void test_borrowed_alias_cannot_outlive_owned_mapping(void) {
    struct fixture f;
    fixture_init(&f);
    uint64_t frame = 0;
    assert(dodo_owned_frames_alloc(&f.frames, 77, DODO_FRAME_KIND_USER_DATA,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x410000),
        frame, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x411000),
        frame, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_OWNERSHIP_ERROR);
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
    assert(f.space.mapping_count == 1u && f.allocator.allocated_pages == 1u);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x410000)) ==
           DODO_ADDRESS_SPACE_OK);
    assert(f.allocator.allocated_pages == 0u);
}
static void test_borrowed_aliases_share_external_lifetime(void) {
    struct fixture f;
    fixture_init(&f);
    uint64_t frame = 0;
    assert(dodo_owned_frames_alloc(&f.frames, 99, DODO_FRAME_KIND_USER_DATA,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    const uint64_t flags = DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE;
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x41a000),
        frame, flags, DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x41b000),
        frame, flags, DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x41a000)) ==
           DODO_ADDRESS_SPACE_OK);
    assert(f.allocator.allocated_pages == 1u);
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x41b000)) ==
           DODO_ADDRESS_SPACE_OK);
    assert(f.allocator.allocated_pages == 1u);
    assert(dodo_owned_frames_release(&f.frames, 99, frame) == DODO_OWNED_FRAMES_OK);
}
static void test_borrowed_mapping_is_not_freed(void) {
    struct fixture f;
    fixture_init(&f);
    uint64_t frame = 0;
    assert(dodo_owned_frames_alloc(&f.frames, 99, DODO_FRAME_KIND_USER_DATA,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x402000),
        frame, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x402000)) ==
           DODO_ADDRESS_SPACE_OK);
    assert(f.allocator.allocated_pages == 1u);
    assert(dodo_owned_frames_release(&f.frames, 99, frame) == DODO_OWNED_FRAMES_OK);
}

static void test_invalid_flags_and_duplicate_virtual_page(void) {
    struct fixture f;
    fixture_init(&f);
    uint64_t frame = 0;
    assert(dodo_owned_frames_alloc(&f.frames, 77, DODO_FRAME_KIND_USER_DATA,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x403000),
        frame, DODO_PAGE_WRITABLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x403000),
        frame, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE | DODO_PAGE_GLOBAL,
        DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x403000),
        frame, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x403000),
        frame, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_ALREADY_MAPPED);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x403000)) ==
           DODO_ADDRESS_SPACE_OK);
}

static void test_kernel_half_address_is_rejected_for_user_metadata(void) {
    struct fixture f;
    fixture_init(&f);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0xffff800000000000),
        UINT64_C(0x1000), DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
    assert(f.space.mapping_count == 0u);
}
static void test_physical_address_outside_pte_range_is_rejected(void) {
    struct fixture f;
    fixture_init(&f);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x412000),
        UINT64_C(0x0010000000000000),
        DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
    assert(f.space.mapping_count == 0u);
}
static void test_unaligned_addresses_are_rejected(void) {
    struct fixture f;
    fixture_init(&f);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x40f001),
        UINT64_C(0x1000), DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x40f000),
        UINT64_C(0x1001), DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
    assert(f.space.mapping_count == 0u);
}
static void test_unsupported_cache_flags_are_rejected(void) {
    struct fixture f;
    fixture_init(&f);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x413000),
        UINT64_C(0x1000), DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE |
        DODO_PAGE_CACHE_DISABLE, DODO_ADDRESS_SPACE_BORROWED_FRAME) ==
        DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x413000),
        UINT64_C(0x1000), DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE |
        DODO_PAGE_WRITE_THROUGH, DODO_ADDRESS_SPACE_BORROWED_FRAME) ==
        DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
}
static void test_invalid_physical_frame_and_policy_are_rejected(void) {
    struct fixture f;
    fixture_init(&f);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x40e000),
        0u, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x40e000),
        UINT64_C(0x1000), DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        (enum dodo_address_space_frame_policy)99) ==
        DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
    assert(f.space.mapping_count == 0u && f.space.virtual_space.count == 0u);
}
static void test_null_page_mapping_is_rejected(void) {
    struct fixture f;
    fixture_init(&f);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0),
        UINT64_C(0x1000), DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_RANGE_ERROR);
    assert(f.space.mapping_count == 0u && f.space.virtual_space.count == 0u);
}
static void test_mapping_outside_reserved_interval_is_rejected(void) {
    struct fixture f;
    fixture_init(&f);
    uint64_t frame = 0;
    assert(dodo_owned_frames_alloc(&f.frames, 77, DODO_FRAME_KIND_USER_DATA,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x300000),
        frame, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_RANGE_ERROR);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x800000),
        frame, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_RANGE_ERROR);
    assert(f.space.mapping_count == 0u && f.space.virtual_space.count == 0u);
    assert(dodo_owned_frames_release(&f.frames, 77, frame) == DODO_OWNED_FRAMES_OK);
}
static void test_owned_mapping_requires_user_data_frame_kind(void) {
    struct fixture f;
    fixture_init(&f);
    uint64_t frame = 0;
    assert(dodo_owned_frames_alloc(&f.frames, 77, DODO_FRAME_KIND_PAGE_TABLE,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x409000),
        frame, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_OWNERSHIP_ERROR);
    assert(f.space.mapping_count == 0u);
    assert(dodo_owned_frames_release(&f.frames, 77, frame) == DODO_OWNED_FRAMES_OK);
}
static void test_owned_mapping_requires_matching_frame_owner(void) {
    struct fixture f;
    fixture_init(&f);
    uint64_t frame = 0;
    assert(dodo_owned_frames_alloc(&f.frames, 99, DODO_FRAME_KIND_USER_DATA,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x408000),
        frame, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_OWNERSHIP_ERROR);
    assert(f.space.mapping_count == 0u && f.space.virtual_space.count == 0u);
    assert(dodo_owned_frames_release(&f.frames, 99, frame) == DODO_OWNED_FRAMES_OK);
}
static void test_writable_non_executable_mapping_is_allowed(void) {
    struct fixture f;
    fixture_init(&f);
    const uint64_t flags = DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_WRITABLE |
                           DODO_PAGE_NO_EXECUTE;
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x41d000),
        UINT64_C(0x1000), flags, DODO_ADDRESS_SPACE_BORROWED_FRAME) ==
        DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x41d000)) ==
           DODO_ADDRESS_SPACE_OK);
}
static void test_read_only_executable_mapping_is_allowed(void) {
    struct fixture f;
    fixture_init(&f);
    const uint64_t flags = DODO_PAGE_USER_ACCESSIBLE;
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x41c000),
        UINT64_C(0x1000), flags, DODO_ADDRESS_SPACE_BORROWED_FRAME) ==
        DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x41c000)) ==
           DODO_ADDRESS_SPACE_OK);
}
static void test_writable_executable_mapping_is_rejected(void) {
    struct fixture f;
    fixture_init(&f);
    uint64_t frame = 0;
    assert(dodo_owned_frames_alloc(&f.frames, 77, DODO_FRAME_KIND_USER_DATA,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x407000),
        frame, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_WRITABLE,
        DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
    assert(f.space.mapping_count == 0u && f.space.virtual_space.count == 0u);
    assert(dodo_owned_frames_release(&f.frames, 77, frame) == DODO_OWNED_FRAMES_OK);
}
static void test_owned_frame_cannot_have_two_owning_mappings(void) {
    struct fixture f;
    fixture_init(&f);
    uint64_t frame = 0;
    assert(dodo_owned_frames_alloc(&f.frames, 77, DODO_FRAME_KIND_USER_DATA,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x405000),
        frame, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x406000),
        frame, DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_OWNERSHIP_ERROR);
    assert(f.space.mapping_count == 1u && f.space.virtual_space.count == 1u);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x405000)) ==
           DODO_ADDRESS_SPACE_OK);
}
static void test_forget_rejects_missing_or_misaligned_page(void) {
    struct fixture f;
    fixture_init(&f);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x40b000)) ==
           DODO_ADDRESS_SPACE_NOT_MAPPED);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x40b001)) ==
           DODO_ADDRESS_SPACE_INVALID_ARGUMENT);
    assert(f.space.mapping_count == 0u && f.space.virtual_space.count == 0u);
}
static void test_out_of_order_records_remain_sorted(void) {
    struct fixture f;
    fixture_init(&f);
    const uint64_t flags = DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE;
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x420000),
        UINT64_C(0x2000), flags, DODO_ADDRESS_SPACE_BORROWED_FRAME) ==
        DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x410000),
        UINT64_C(0x3000), flags, DODO_ADDRESS_SPACE_BORROWED_FRAME) ==
        DODO_ADDRESS_SPACE_OK);
    assert(f.space.mappings[0].virtual_address == UINT64_C(0x410000));
    assert(f.space.mappings[1].virtual_address == UINT64_C(0x420000));
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x420000)) ==
        DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x410000)) ==
        DODO_ADDRESS_SPACE_OK);
}
static void test_mapping_capacity_fails_without_mutation(void) {
    struct fixture f;
    fixture_init(&f);
    for (uint32_t i = 0u; i < DODO_ADDRESS_SPACE_MAX_MAPPINGS; ++i) {
        const uint64_t address = UINT64_C(0x400000) +
            (uint64_t)i * DODO_VADDR_PAGE_SIZE;
        assert(dodo_address_space_record_mapped_page(&f.space, address,
            UINT64_C(0x1000), DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
            DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_OK);
    }
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
    assert(f.space.mapping_count == DODO_ADDRESS_SPACE_MAX_MAPPINGS);
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x440000),
        UINT64_C(0x1000), DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_CAPACITY);
    assert(f.space.mapping_count == DODO_ADDRESS_SPACE_MAX_MAPPINGS);
    for (uint32_t i = 0u; i < DODO_ADDRESS_SPACE_MAX_MAPPINGS; ++i) {
        const uint64_t address = UINT64_C(0x400000) +
            (uint64_t)i * DODO_VADDR_PAGE_SIZE;
        assert(dodo_address_space_forget_unmapped_page(&f.space, address) ==
               DODO_ADDRESS_SPACE_OK);
    }
    assert(dodo_address_space_validate(&f.space) == DODO_ADDRESS_SPACE_OK);
}
static void test_lookup_returns_exact_mapping(void) {
    struct fixture f;
    fixture_init(&f);
    uint64_t frame = 0;
    assert(dodo_owned_frames_alloc(&f.frames, 77, DODO_FRAME_KIND_USER_DATA,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    const uint64_t flags = DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE;
    assert(dodo_address_space_record_mapped_page(&f.space, UINT64_C(0x40a000),
        frame, flags, DODO_ADDRESS_SPACE_OWNED_FRAME) == DODO_ADDRESS_SPACE_OK);
    struct dodo_address_space_mapping out = {0};
    assert(dodo_address_space_lookup(&f.space, UINT64_C(0x40a000), &out) ==
           DODO_ADDRESS_SPACE_OK);
    assert(out.virtual_address == UINT64_C(0x40a000));
    assert(out.physical_address == frame && out.flags == flags);
    assert(out.frame_policy == DODO_ADDRESS_SPACE_OWNED_FRAME);
    assert(dodo_address_space_forget_unmapped_page(&f.space, UINT64_C(0x40a000)) ==
           DODO_ADDRESS_SPACE_OK);
}
static void test_lookup_preserves_output_on_miss(void) {
    struct fixture f;
    fixture_init(&f);
    struct dodo_address_space_mapping out = {
        .virtual_address = UINT64_C(0xdead000),
        .physical_address = UINT64_C(0xbeef000),
        .flags = UINT64_C(0x1234),
        .frame_policy = DODO_ADDRESS_SPACE_BORROWED_FRAME
    };
    assert(dodo_address_space_lookup(&f.space, UINT64_C(0x404000), &out) ==
           DODO_ADDRESS_SPACE_NOT_MAPPED);
    assert(out.virtual_address == UINT64_C(0xdead000));
    assert(out.physical_address == UINT64_C(0xbeef000));
    assert(out.flags == UINT64_C(0x1234));
}

int main(void) {
    test_external_free_is_detected_before_metadata_mutation();
    test_frame_pool_corruption_propagates_as_ownership_error();
    test_validation_detects_duplicate_owned_frame_corruption();
    test_reservation_and_mapping_addresses_must_match();
    test_corrupt_mapping_flags_are_rejected();
    test_stale_unused_mapping_slot_is_rejected();
    test_corrupt_metadata_fails_closed();
    test_init_requires_initialized_physical_allocator();
    test_init_refuses_corrupt_frame_ledger();
    test_init_rejects_invalid_owner_id();
    test_null_arguments_are_rejected();
    test_init_rejects_invalid_user_interval();
    test_init_and_empty_validation();
    test_owned_mapping_record_and_forget();
    test_validation_rejects_borrowed_record_of_local_frame();
    test_locally_owned_frame_cannot_be_marked_borrowed();
    test_borrowed_alias_cannot_outlive_owned_mapping();
    test_borrowed_aliases_share_external_lifetime();
    test_borrowed_mapping_is_not_freed();
    test_invalid_flags_and_duplicate_virtual_page();
    test_kernel_half_address_is_rejected_for_user_metadata();
    test_physical_address_outside_pte_range_is_rejected();
    test_unaligned_addresses_are_rejected();
    test_unsupported_cache_flags_are_rejected();
    test_invalid_physical_frame_and_policy_are_rejected();
    test_null_page_mapping_is_rejected();
    test_mapping_outside_reserved_interval_is_rejected();
    test_owned_mapping_requires_user_data_frame_kind();
    test_owned_mapping_requires_matching_frame_owner();
    test_writable_non_executable_mapping_is_allowed();
    test_read_only_executable_mapping_is_allowed();
    test_writable_executable_mapping_is_rejected();
    test_owned_frame_cannot_have_two_owning_mappings();
    test_forget_rejects_missing_or_misaligned_page();
    test_out_of_order_records_remain_sorted();
    test_mapping_capacity_fails_without_mutation();
    test_lookup_returns_exact_mapping();
    test_lookup_preserves_output_on_miss();
    puts("address-space metadata tests: ok");
    return 0;
}
