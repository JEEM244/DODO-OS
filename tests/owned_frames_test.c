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
    assert(allocator.free_pages == 8);
    assert(dodo_owned_frames_alloc(&pool, 7, DODO_FRAME_KIND_USER_DATA,
                                   &frame) == DODO_OWNED_FRAMES_OK);
    assert(frame == UINT64_C(0x1000));
    assert(allocator.free_pages == 7 && allocator.allocated_pages == 1);

    uint32_t owned = 0;
    assert(dodo_frame_ledger_count_owner(&pool.ledger, 7, &owned) ==
           DODO_FRAME_LEDGER_OK);
    assert(owned == 1);
    assert(dodo_owned_frames_release(&pool, 8, frame) ==
           DODO_OWNED_FRAMES_LEDGER_ERROR);
    assert(allocator.allocated_pages == 1 && allocator.free_pages == 7);

    assert(dodo_owned_frames_release(&pool, 7, frame) == DODO_OWNED_FRAMES_OK);
    assert(allocator.allocated_pages == 0 && allocator.free_pages == 8);
    assert(dodo_owned_frames_release(&pool, 7, frame) ==
           DODO_OWNED_FRAMES_LEDGER_ERROR);
    assert(allocator.allocated_pages == 0 && allocator.free_pages == 8);

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
    assert(allocator.allocated_pages == 0 && allocator.free_pages == 2);
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
    test_ledger_capacity_failure_rolls_back_physical_frame();
    puts("owned frame tests: ok");
    return 0;
}
