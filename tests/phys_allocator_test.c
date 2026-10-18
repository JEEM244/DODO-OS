#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "dodo/mm/phys.h"

static void test_basic_allocation(void) {
    const struct dodo_limine_memmap_entry usable_a = {
        .base = UINT64_C(0x1003),
        .length = UINT64_C(0x9000),
        .type = DODO_LIMINE_MEMMAP_USABLE
    };
    const struct dodo_limine_memmap_entry reserved = {
        .base = UINT64_C(0x20000),
        .length = UINT64_C(0x10000),
        .type = UINT64_C(1)
    };
    const struct dodo_limine_memmap_entry usable_b = {
        .base = UINT64_C(0x30003),
        .length = UINT64_C(0x5000),
        .type = DODO_LIMINE_MEMMAP_USABLE
    };

    struct dodo_limine_memmap_entry *entries[] = {
        (struct dodo_limine_memmap_entry *)&usable_a,
        (struct dodo_limine_memmap_entry *)&reserved,
        (struct dodo_limine_memmap_entry *)&usable_b
    };
    const struct dodo_limine_memmap_response map = {
        .revision = 0,
        .entry_count = 3,
        .entries = entries
    };

    struct dodo_phys_allocator allocator;
    assert(dodo_phys_init(&allocator, &map) == DODO_PHYS_OK);
    assert(allocator.initialized == 1);
    assert(allocator.free_pages == 12);
    assert(allocator.allocated_pages == 0);

    uint64_t previous = 0;
    for (uint64_t i = 0; i < 12; ++i) {
        uint64_t page = UINT64_MAX;
        assert(dodo_phys_alloc_page(&allocator, &page) == DODO_PHYS_OK);
        assert(page % DODO_PHYS_PAGE_SIZE == 0);
        assert(page != previous);
        assert(page >= UINT64_C(0x2000));
        assert(page < UINT64_C(0xa000) ||
               (page >= UINT64_C(0x31000) && page < UINT64_C(0x35000)));
        previous = page;
    }

    assert(allocator.free_pages == 0);
    assert(allocator.allocated_pages == 12);

    uint64_t exhausted = 0;
    assert(dodo_phys_alloc_page(&allocator, &exhausted) == DODO_PHYS_NO_MEMORY);
    assert(allocator.allocated_pages == 12);
}

static void test_free_reallocate_and_duplicate_free(void) {
    const struct dodo_limine_memmap_entry usable = {
        .base = UINT64_C(0x1000),
        .length = UINT64_C(0x4000),
        .type = DODO_LIMINE_MEMMAP_USABLE
    };
    struct dodo_limine_memmap_entry *entries[] = {
        (struct dodo_limine_memmap_entry *)&usable
    };
    const struct dodo_limine_memmap_response map = {
        .revision = 0,
        .entry_count = 1,
        .entries = entries
    };

    struct dodo_phys_allocator allocator;
    assert(dodo_phys_init(&allocator, &map) == DODO_PHYS_OK);
    assert(allocator.free_pages == 4);
    assert(allocator.allocated_pages == 0);

    uint64_t first = 0;
    uint64_t second = 0;
    uint64_t third = 0;
    assert(dodo_phys_alloc_page(&allocator, &first) == DODO_PHYS_OK);
    assert(dodo_phys_alloc_page(&allocator, &second) == DODO_PHYS_OK);
    assert(dodo_phys_alloc_page(&allocator, &third) == DODO_PHYS_OK);
    assert(first != second && second != third && first != third);
    assert(allocator.free_pages == 1);
    assert(allocator.allocated_pages == 3);

    assert(dodo_phys_free_page(&allocator, second) == DODO_PHYS_OK);
    assert(allocator.free_pages == 2);
    assert(allocator.allocated_pages == 2);

    assert(dodo_phys_free_page(&allocator, second) == DODO_PHYS_DOUBLE_FREE);
    assert(allocator.free_pages == 2);
    assert(allocator.allocated_pages == 2);

    assert(dodo_phys_free_page(&allocator, UINT64_C(0x9000)) ==
           DODO_PHYS_PAGE_NOT_ALLOCATED);
    assert(dodo_phys_free_page(&allocator, first + 1u) ==
           DODO_PHYS_INVALID_ARGUMENT);
    assert(allocator.free_pages == 2);
    assert(allocator.allocated_pages == 2);

    uint64_t reused = 0;
    assert(dodo_phys_alloc_page(&allocator, &reused) == DODO_PHYS_OK);
    assert(reused == second);
    assert(allocator.free_pages == 1);
    assert(allocator.allocated_pages == 3);

    assert(dodo_phys_free_page(&allocator, reused) == DODO_PHYS_OK);
    assert(dodo_phys_free_page(&allocator, first) == DODO_PHYS_OK);
    assert(dodo_phys_free_page(&allocator, third) == DODO_PHYS_OK);
    assert(allocator.free_pages == 4);
    assert(allocator.allocated_pages == 0);

    /* All four frames can be allocated again without creating duplicate live pages. */
    uint64_t pages[4] = { 0, 0, 0, 0 };
    for (uint64_t i = 0; i < 4; ++i) {
        assert(dodo_phys_alloc_page(&allocator, &pages[i]) == DODO_PHYS_OK);
        assert(pages[i] % DODO_PHYS_PAGE_SIZE == 0);
        for (uint64_t j = 0; j < i; ++j) {
            assert(pages[i] != pages[j]);
        }
    }
    assert(allocator.free_pages == 0);
    assert(allocator.allocated_pages == 4);
    assert(dodo_phys_alloc_page(&allocator, &first) == DODO_PHYS_NO_MEMORY);
}

static void test_zero_physical_page_can_be_allocated_and_freed(void) {
    const struct dodo_limine_memmap_entry usable = {
        .base = 0,
        .length = DODO_PHYS_PAGE_SIZE,
        .type = DODO_LIMINE_MEMMAP_USABLE
    };
    struct dodo_limine_memmap_entry *entries[] = {
        (struct dodo_limine_memmap_entry *)&usable
    };
    const struct dodo_limine_memmap_response map = {
        .revision = 0,
        .entry_count = 1,
        .entries = entries
    };

    struct dodo_phys_allocator allocator;
    uint64_t page = UINT64_MAX;
    assert(dodo_phys_init(&allocator, &map) == DODO_PHYS_OK);
    assert(dodo_phys_alloc_page(&allocator, &page) == DODO_PHYS_OK);
    assert(page == 0);
    assert(dodo_phys_alloc_page(&allocator, &page) == DODO_PHYS_NO_MEMORY);
    assert(dodo_phys_free_page(&allocator, 0) == DODO_PHYS_OK);
    assert(dodo_phys_free_page(&allocator, 0) == DODO_PHYS_DOUBLE_FREE);
    assert(dodo_phys_alloc_page(&allocator, &page) == DODO_PHYS_OK);
    assert(page == 0);
}

static void test_tracking_capacity_is_bounded(void) {
    const struct dodo_limine_memmap_entry usable = {
        .base = UINT64_C(0x1000),
        .length = (DODO_PHYS_MAX_TRACKED_PAGES + UINT64_C(1)) *
                  DODO_PHYS_PAGE_SIZE,
        .type = DODO_LIMINE_MEMMAP_USABLE
    };
    struct dodo_limine_memmap_entry *entries[] = {
        (struct dodo_limine_memmap_entry *)&usable
    };
    const struct dodo_limine_memmap_response map = {
        .revision = 0,
        .entry_count = 1,
        .entries = entries
    };

    struct dodo_phys_allocator allocator;
    assert(dodo_phys_init(&allocator, &map) == DODO_PHYS_OK);

    uint64_t first_page = UINT64_MAX;
    for (uint64_t i = 0; i < DODO_PHYS_MAX_TRACKED_PAGES; ++i) {
        uint64_t page = UINT64_MAX;
        assert(dodo_phys_alloc_page(&allocator, &page) == DODO_PHYS_OK);
        assert(page % DODO_PHYS_PAGE_SIZE == 0);
        if (i == 0) {
            first_page = page;
        } else {
            assert(page != first_page);
        }
    }

    assert(allocator.allocated_pages == DODO_PHYS_MAX_TRACKED_PAGES);
    assert(allocator.free_pages == 1);

    uint64_t untouched_output = UINT64_C(0x1234);
    assert(dodo_phys_alloc_page(&allocator, &untouched_output) ==
           DODO_PHYS_TRACKING_CAPACITY_EXCEEDED);
    assert(untouched_output == UINT64_C(0x1234));
    assert(allocator.allocated_pages == DODO_PHYS_MAX_TRACKED_PAGES);
    assert(allocator.free_pages == 1);

    assert(dodo_phys_free_page(&allocator, first_page) == DODO_PHYS_OK);
    assert(allocator.allocated_pages == DODO_PHYS_MAX_TRACKED_PAGES - 1u);
    assert(allocator.free_pages == 2);

    uint64_t recycled_page = 0;
    assert(dodo_phys_alloc_page(&allocator, &recycled_page) == DODO_PHYS_OK);
    assert(recycled_page == first_page);
    assert(allocator.allocated_pages == DODO_PHYS_MAX_TRACKED_PAGES);
    assert(allocator.free_pages == 1);
}

static void test_deterministic_allocate_free_stress(void) {
    enum { PAGE_COUNT = 512, ITERATION_COUNT = 20000 };
    const struct dodo_limine_memmap_entry usable = {
        .base = UINT64_C(0x1000),
        .length = (uint64_t)PAGE_COUNT * DODO_PHYS_PAGE_SIZE,
        .type = DODO_LIMINE_MEMMAP_USABLE
    };
    struct dodo_limine_memmap_entry *entries[] = {
        (struct dodo_limine_memmap_entry *)&usable
    };
    const struct dodo_limine_memmap_response map = {
        .revision = 0,
        .entry_count = 1,
        .entries = entries
    };

    struct dodo_phys_allocator allocator;
    uint64_t live[PAGE_COUNT];
    uint64_t live_count = 0;
    uint32_t random_state = UINT32_C(0xD0D05EED);

    assert(dodo_phys_init(&allocator, &map) == DODO_PHYS_OK);
    for (uint64_t step = 0; step < ITERATION_COUNT; ++step) {
        /* Fixed LCG seed makes a failure reproducible in every environment. */
        random_state = random_state * UINT32_C(1664525) +
                       UINT32_C(1013904223);

        const int should_allocate =
            live_count == 0 ||
            (live_count < PAGE_COUNT && ((random_state >> 8) & 1u) != 0u);

        if (should_allocate) {
            uint64_t page = UINT64_MAX;
            assert(dodo_phys_alloc_page(&allocator, &page) == DODO_PHYS_OK);
            assert(page % DODO_PHYS_PAGE_SIZE == 0);
            for (uint64_t i = 0; i < live_count; ++i) {
                assert(live[i] != page);
            }
            live[live_count++] = page;
        } else {
            const uint64_t index = (uint64_t)random_state % live_count;
            const uint64_t page = live[index];
            assert(dodo_phys_free_page(&allocator, page) == DODO_PHYS_OK);
            live[index] = live[live_count - 1u];
            live_count--;
        }

        assert(allocator.allocated_pages == live_count);
        assert(allocator.free_pages + live_count == PAGE_COUNT);
    }

    while (live_count != 0) {
        assert(dodo_phys_free_page(&allocator, live[live_count - 1u]) ==
               DODO_PHYS_OK);
        live_count--;
    }
    assert(allocator.allocated_pages == 0);
    assert(allocator.free_pages == PAGE_COUNT);

    uint64_t all_pages[PAGE_COUNT];
    for (uint64_t i = 0; i < PAGE_COUNT; ++i) {
        assert(dodo_phys_alloc_page(&allocator, &all_pages[i]) == DODO_PHYS_OK);
        for (uint64_t j = 0; j < i; ++j) {
            assert(all_pages[j] != all_pages[i]);
        }
    }
    assert(allocator.free_pages == 0);
    assert(allocator.allocated_pages == PAGE_COUNT);
}

static void test_add_region_after_reclamation(void) {
    const struct dodo_limine_memmap_entry usable = {
        .base = UINT64_C(0x1000),
        .length = UINT64_C(0x4000),
        .type = DODO_LIMINE_MEMMAP_USABLE
    };
    struct dodo_limine_memmap_entry *entries[] = {
        (struct dodo_limine_memmap_entry *)&usable
    };
    const struct dodo_limine_memmap_response map = {
        .revision = 0,
        .entry_count = 1,
        .entries = entries
    };

    struct dodo_phys_allocator allocator;
    assert(dodo_phys_init(&allocator, &map) == DODO_PHYS_OK);
    assert(allocator.region_count == 1);
    assert(allocator.free_pages == 4);

    assert(dodo_phys_add_region(
        &allocator, UINT64_C(0x10000), UINT64_C(0x3000)) == DODO_PHYS_OK);
    assert(allocator.region_count == 2);
    assert(allocator.free_pages == 7);

    /* Bad requests must not mutate the free-page count or region list. */
    assert(dodo_phys_add_region(
        &allocator, UINT64_C(0x11000), DODO_PHYS_PAGE_SIZE) ==
        DODO_PHYS_REGION_OVERLAP);
    assert(dodo_phys_add_region(
        &allocator, UINT64_C(0x12001), DODO_PHYS_PAGE_SIZE) ==
        DODO_PHYS_INVALID_ARGUMENT);
    assert(dodo_phys_add_region(
        &allocator, UINT64_C(0x12000), UINT64_C(0x1001)) ==
        DODO_PHYS_INVALID_ARGUMENT);
    assert(dodo_phys_add_region(
        &allocator, UINT64_MAX - UINT64_C(0xfff), DODO_PHYS_PAGE_SIZE) ==
        DODO_PHYS_INVALID_MAP);
    assert(dodo_phys_add_region(
        &allocator, UINT64_C(0x20000), 0) ==
        DODO_PHYS_INVALID_ARGUMENT);
    assert(allocator.region_count == 2);
    assert(allocator.free_pages == 7);

    for (uint64_t i = 0; i < 5; ++i) {
        uint64_t page = UINT64_MAX;
        assert(dodo_phys_alloc_page(&allocator, &page) == DODO_PHYS_OK);
        if (i < 4) {
            assert(page == UINT64_C(0x1000) + i * DODO_PHYS_PAGE_SIZE);
        } else {
            assert(page == UINT64_C(0x10000));
        }
    }
    assert(allocator.allocated_pages == 5);
    assert(allocator.free_pages == 2);

    /*
     * Region-targeted allocation proves that the newly added range can supply
     * a frame even while older usable regions still have space left.
     */
    uint64_t reclaimed_page = UINT64_MAX;
    assert(dodo_phys_alloc_page_from_region(
        &allocator, 1, &reclaimed_page) == DODO_PHYS_OK);
    assert(reclaimed_page == UINT64_C(0x11000));
    assert(allocator.allocated_pages == 6);
    assert(allocator.free_pages == 1);

    assert(dodo_phys_free_page(&allocator, reclaimed_page) == DODO_PHYS_OK);
    assert(allocator.allocated_pages == 5);
    assert(allocator.free_pages == 2);

    /* The ordinary allocator should also reuse that returned reclaimed frame. */
    uint64_t reused_reclaimed_page = 0;
    assert(dodo_phys_alloc_page(&allocator, &reused_reclaimed_page) == DODO_PHYS_OK);
    assert(reused_reclaimed_page == reclaimed_page);
    assert(allocator.allocated_pages == 6);
    assert(allocator.free_pages == 1);
    assert(dodo_phys_free_page(&allocator, reused_reclaimed_page) == DODO_PHYS_OK);
    assert(allocator.allocated_pages == 5);
    assert(allocator.free_pages == 2);

    uint64_t invalid_output = UINT64_C(0xCAFE);
    assert(dodo_phys_alloc_page_from_region(
        &allocator, allocator.region_count, &invalid_output) ==
        DODO_PHYS_INVALID_ARGUMENT);
    assert(invalid_output == UINT64_C(0xCAFE));
}

static void test_invalid_arguments_and_map(void) {
    struct dodo_phys_allocator allocator;
    assert(dodo_phys_init(0, 0) == DODO_PHYS_INVALID_ARGUMENT);
    assert(dodo_phys_alloc_page(&allocator, 0) == DODO_PHYS_INVALID_ARGUMENT);

    const struct dodo_limine_memmap_response missing_entries = {
        .revision = 0,
        .entry_count = 1,
        .entries = 0
    };
    assert(dodo_phys_init(&allocator, &missing_entries) == DODO_PHYS_INVALID_MAP);
    assert(allocator.initialized == 0);

    struct dodo_limine_memmap_entry *null_entries[] = { 0 };
    const struct dodo_limine_memmap_response null_entry_map = {
        .revision = 0,
        .entry_count = 1,
        .entries = null_entries
    };
    assert(dodo_phys_init(&allocator, &null_entry_map) == DODO_PHYS_INVALID_MAP);
    assert(allocator.initialized == 0);

    const struct dodo_limine_memmap_entry overflow = {
        .base = UINT64_MAX - UINT64_C(0x7ff),
        .length = UINT64_C(0x1000),
        .type = DODO_LIMINE_MEMMAP_USABLE
    };
    struct dodo_limine_memmap_entry *overflow_entries[] = {
        (struct dodo_limine_memmap_entry *)&overflow
    };
    const struct dodo_limine_memmap_response overflow_map = {
        .revision = 0,
        .entry_count = 1,
        .entries = overflow_entries
    };
    assert(dodo_phys_init(&allocator, &overflow_map) == DODO_PHYS_INVALID_MAP);
    assert(allocator.initialized == 0);
}

static void test_overlap_rejected(void) {
    const struct dodo_limine_memmap_entry first = {
        .base = UINT64_C(0x1000),
        .length = UINT64_C(0x4000),
        .type = DODO_LIMINE_MEMMAP_USABLE
    };
    const struct dodo_limine_memmap_entry second = {
        .base = UINT64_C(0x3000),
        .length = UINT64_C(0x3000),
        .type = DODO_LIMINE_MEMMAP_USABLE
    };
    struct dodo_limine_memmap_entry *entries[] = {
        (struct dodo_limine_memmap_entry *)&first,
        (struct dodo_limine_memmap_entry *)&second
    };
    const struct dodo_limine_memmap_response map = {
        .revision = 0,
        .entry_count = 2,
        .entries = entries
    };

    struct dodo_phys_allocator allocator;
    assert(dodo_phys_init(&allocator, &map) == DODO_PHYS_REGION_OVERLAP);
    assert(allocator.initialized == 0);
    assert(allocator.free_pages == 0);
}

int main(void) {
    test_basic_allocation();
    test_free_reallocate_and_duplicate_free();
    test_zero_physical_page_can_be_allocated_and_freed();
    test_tracking_capacity_is_bounded();
    test_deterministic_allocate_free_stress();
    test_add_region_after_reclamation();
    test_invalid_arguments_and_map();
    test_overlap_rejected();
    puts("DODO physical allocator tests: PASS");
    return 0;
}
