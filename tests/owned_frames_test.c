#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "dodo/mm/owned_frames.h"

static void test_owned_allocation_lifecycle(void) {
    const struct dodo_limine_memmap_entry usable = {
        .base = UINT64_C(0x1000),
        .length = UINT64_C(0x8000),
        .type = DODO_LIMINE_MEMMAP_USABLE
    };
    struct dodo_limine_memmap_entry *entries[] = {
        (struct dodo_limine_memmap_entry *)&usable
    };
    const struct dodo_limine_memmap_response map = {
        .revision = 0, .entry_count = 1, .entries = entries
    };
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

int main(void) {
    test_owned_allocation_lifecycle();
    puts("owned frame tests: ok");
    return 0;
}
