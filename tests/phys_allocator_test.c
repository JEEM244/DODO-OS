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
    test_invalid_arguments_and_map();
    test_overlap_rejected();
    puts("DODO physical allocator tests: PASS");
    return 0;
}
