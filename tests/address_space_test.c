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
    test_init_and_empty_validation();
    test_owned_mapping_record_and_forget();
    test_borrowed_mapping_is_not_freed();
    test_invalid_flags_and_duplicate_virtual_page();
    test_owned_mapping_requires_matching_frame_owner();
    test_writable_executable_mapping_is_rejected();
    test_owned_frame_cannot_have_two_owning_mappings();
    test_lookup_preserves_output_on_miss();
    puts("address-space metadata tests: ok");
    return 0;
}
