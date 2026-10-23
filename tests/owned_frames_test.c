#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "dodo/mm/owned_frames.h"

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

static void test_owned_allocation_lifecycle(void) {
    struct dodo_limine_memmap_entry usable;
    struct dodo_limine_memmap_entry *entries[1];
    struct dodo_limine_memmap_response map;
    make_usable_map(&usable, entries, &map, 8);

    struct dodo_phys_allocator allocator;
    struct dodo_owned_frames pool;
    uint64_t frame = 0;

    assert(dodo_phys_init(&allocator, &map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&pool, &allocator);
    assert(dodo_owned_frames_validate(&pool) == DODO_OWNED_FRAMES_OK);
    assert(allocator.free_pages == 8);
    assert(dodo_owned_frames_alloc(&pool, 7, DODO_FRAME_KIND_USER_DATA,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    assert(frame == UINT64_C(0x1000));
    assert(allocator.free_pages == 7 && allocator.allocated_pages == 1);

    uint32_t owned = 0;
    assert(dodo_frame_ledger_count_owner(&pool.ledger, 7, &owned) ==
           DODO_FRAME_LEDGER_OK);
    assert(owned == 1);
    assert(dodo_owned_frames_validate(&pool) == DODO_OWNED_FRAMES_OK);
    assert(dodo_owned_frames_release(&pool, 8, frame) ==
           DODO_OWNED_FRAMES_LEDGER_ERROR);
    assert(allocator.allocated_pages == 1 && allocator.free_pages == 7);

    assert(dodo_owned_frames_release(&pool, 7, frame) == DODO_OWNED_FRAMES_OK);
    assert(allocator.allocated_pages == 0 && allocator.free_pages == 8);
    assert(dodo_owned_frames_release(&pool, 7, frame) ==
           DODO_OWNED_FRAMES_LEDGER_ERROR);
    assert(allocator.allocated_pages == 0 && allocator.free_pages == 8);
    assert(dodo_owned_frames_validate(&pool) == DODO_OWNED_FRAMES_OK);

    uint64_t reused = 0;
    assert(dodo_owned_frames_alloc(&pool, 9, DODO_FRAME_KIND_PAGE_TABLE,
                                   &reused) == DODO_OWNED_FRAMES_OK);
    assert(reused == frame);
    assert(dodo_owned_frames_release(&pool, 9, reused) == DODO_OWNED_FRAMES_OK);
}

static void test_invalid_arguments_preserve_output(void) {
    struct dodo_limine_memmap_entry usable;
    struct dodo_limine_memmap_entry *entries[1];
    struct dodo_limine_memmap_response map;
    make_usable_map(&usable, entries, &map, 2);

    struct dodo_phys_allocator allocator;
    struct dodo_owned_frames pool;
    assert(dodo_phys_init(&allocator, &map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&pool, &allocator);

    uint64_t output = UINT64_C(0xfeedface);
    assert(dodo_owned_frames_alloc(NULL, 1, DODO_FRAME_KIND_USER_DATA,
                                   &output) == DODO_OWNED_FRAMES_INVALID_ARGUMENT);
    assert(output == UINT64_C(0xfeedface));
    assert(dodo_owned_frames_alloc(&pool, DODO_FRAME_OWNER_INVALID,
                                   DODO_FRAME_KIND_USER_DATA, &output) ==
           DODO_OWNED_FRAMES_INVALID_ARGUMENT);
    assert(output == UINT64_C(0xfeedface));
    assert(dodo_owned_frames_alloc(&pool, 1,
                                   (enum dodo_frame_kind)99, &output) ==
           DODO_OWNED_FRAMES_INVALID_ARGUMENT);
    assert(output == UINT64_C(0xfeedface));
    assert(allocator.allocated_pages == 0 && allocator.free_pages == 2);
}

static void test_corrupt_ledger_fails_closed_before_allocation(void) {
    struct dodo_limine_memmap_entry usable;
    struct dodo_limine_memmap_entry *entries[1];
    struct dodo_limine_memmap_response map;
    make_usable_map(&usable, entries, &map, 2);

    struct dodo_phys_allocator allocator;
    struct dodo_owned_frames pool;
    assert(dodo_phys_init(&allocator, &map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&pool, &allocator);

    /* Deliberately violate count-vs-record consistency. */
    pool.ledger.count = 1;
    uint64_t output = UINT64_C(0xfeedface);
    assert(dodo_owned_frames_alloc(&pool, 1, DODO_FRAME_KIND_USER_DATA,
                                   &output) == DODO_OWNED_FRAMES_LEDGER_ERROR);
    assert(output == UINT64_C(0xfeedface));
    assert(allocator.allocated_pages == 0 && allocator.free_pages == 2);
}

static void test_corrupt_ledger_prevents_release(void) {
    struct dodo_limine_memmap_entry usable;
    struct dodo_limine_memmap_entry *entries[1];
    struct dodo_limine_memmap_response map;
    make_usable_map(&usable, entries, &map, 2);

    struct dodo_phys_allocator allocator;
    struct dodo_owned_frames pool;
    uint64_t frame = 0;
    assert(dodo_phys_init(&allocator, &map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&pool, &allocator);
    assert(dodo_owned_frames_alloc(&pool, 5, DODO_FRAME_KIND_USER_DATA,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    assert(allocator.allocated_pages == 1 && allocator.free_pages == 1);

    /* Corrupt metadata and prove release does not free a still-owned frame. */
    pool.ledger.count = 0;
    assert(dodo_owned_frames_release(&pool, 5, frame) ==
           DODO_OWNED_FRAMES_LEDGER_ERROR);
    assert(allocator.allocated_pages == 1 && allocator.free_pages == 1);
}

static void test_validate_detects_ledger_frame_freed_outside_adapter(void) {
    struct dodo_limine_memmap_entry usable;
    struct dodo_limine_memmap_entry *entries[1];
    struct dodo_limine_memmap_response map;
    make_usable_map(&usable, entries, &map, 2);

    struct dodo_phys_allocator allocator;
    struct dodo_owned_frames pool;
    uint64_t frame = 0;
    assert(dodo_phys_init(&allocator, &map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&pool, &allocator);
    assert(dodo_owned_frames_alloc(&pool, 12, DODO_FRAME_KIND_PAGE_TABLE,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    assert(dodo_owned_frames_validate(&pool) == DODO_OWNED_FRAMES_OK);

    /* Simulate a caller bypassing ownership and freeing a tracked frame. */
    assert(dodo_phys_free_page(&allocator, frame) == DODO_PHYS_OK);
    assert(dodo_owned_frames_validate(&pool) == DODO_OWNED_FRAMES_ALLOCATOR_ERROR);

    const uint64_t free_before = allocator.free_pages;
    const uint64_t allocated_before = allocator.allocated_pages;
    uint64_t output = UINT64_C(0xfeedface);
    assert(dodo_owned_frames_alloc(&pool, 13, DODO_FRAME_KIND_USER_DATA,
                                   &output) == DODO_OWNED_FRAMES_ALLOCATOR_ERROR);
    assert(output == UINT64_C(0xfeedface));
    assert(allocator.free_pages == free_before);
    assert(allocator.allocated_pages == allocated_before);
    assert(dodo_owned_frames_release(&pool, 12, frame) ==
           DODO_OWNED_FRAMES_ALLOCATOR_ERROR);
    assert(allocator.free_pages == free_before);
    assert(allocator.allocated_pages == allocated_before);
}

static void test_ledger_capacity_failure_rolls_back_physical_frame(void) {
    const uint64_t pages = (uint64_t)DODO_FRAME_LEDGER_CAPACITY + 1u;
    struct dodo_limine_memmap_entry usable;
    struct dodo_limine_memmap_entry *entries[1];
    struct dodo_limine_memmap_response map;
    make_usable_map(&usable, entries, &map, pages);

    struct dodo_phys_allocator allocator;
    struct dodo_owned_frames pool;
    assert(dodo_phys_init(&allocator, &map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&pool, &allocator);

    for (uint32_t i = 0; i < DODO_FRAME_LEDGER_CAPACITY; ++i) {
        uint64_t frame = 0;
        assert(dodo_owned_frames_alloc(&pool, 42, DODO_FRAME_KIND_PAGE_TABLE,
                                       &frame) == DODO_OWNED_FRAMES_OK);
        assert(frame != 0);
    }

    assert(pool.ledger.count == DODO_FRAME_LEDGER_CAPACITY);
    assert(allocator.allocated_pages == DODO_FRAME_LEDGER_CAPACITY);
    assert(allocator.free_pages == 1);

    /* The next physical allocation must be rolled back when ownership is full. */
    uint64_t output = UINT64_C(0xfeedface);
    assert(dodo_owned_frames_alloc(&pool, 42, DODO_FRAME_KIND_ROOT, &output) ==
           DODO_OWNED_FRAMES_LEDGER_ERROR);
    assert(output == UINT64_C(0xfeedface));
    assert(pool.ledger.count == DODO_FRAME_LEDGER_CAPACITY);
    assert(dodo_frame_ledger_validate(&pool.ledger) == DODO_FRAME_LEDGER_OK);
    assert(allocator.allocated_pages == DODO_FRAME_LEDGER_CAPACITY);
    assert(allocator.free_pages == 1);
}

int main(void) {
    test_owned_allocation_lifecycle();
    test_invalid_arguments_preserve_output();
    test_corrupt_ledger_fails_closed_before_allocation();
    test_corrupt_ledger_prevents_release();
    test_validate_detects_ledger_frame_freed_outside_adapter();
    test_ledger_capacity_failure_rolls_back_physical_frame();
    puts("owned frame tests: ok");
    return 0;
}
