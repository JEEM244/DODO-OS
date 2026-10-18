#include "dodo/mm/phys.h"

#define DODO_MAX_MEMMAP_ENTRIES UINT64_C(4096)
#define DODO_PAGE_MASK (DODO_PHYS_PAGE_SIZE - UINT64_C(1))

static void reset_allocator(struct dodo_phys_allocator *allocator) {
    allocator->region_count = 0;
    allocator->current_region = 0;
    allocator->free_pages = 0;
    allocator->allocated_pages = 0;
    allocator->initialized = 0;

    for (uint64_t i = 0; i < DODO_PHYS_MAX_REGIONS; ++i) {
        allocator->regions[i].start = 0;
        allocator->regions[i].cursor = 0;
        allocator->regions[i].end = 0;
    }
}

enum dodo_phys_status dodo_phys_init(
    struct dodo_phys_allocator *allocator,
    const struct dodo_limine_memmap_response *response) {
    if (allocator == 0 || response == 0) {
        return DODO_PHYS_INVALID_ARGUMENT;
    }

    reset_allocator(allocator);

    if (response->entry_count > DODO_MAX_MEMMAP_ENTRIES) {
        return DODO_PHYS_CAPACITY_EXCEEDED;
    }

    if (response->entry_count != 0 && response->entries == 0) {
        return DODO_PHYS_INVALID_MAP;
    }

    for (uint64_t i = 0; i < response->entry_count; ++i) {
        const struct dodo_limine_memmap_entry *entry = response->entries[i];

        if (entry == 0) {
            reset_allocator(allocator);
            return DODO_PHYS_INVALID_MAP;
        }

        if (entry->type != DODO_LIMINE_MEMMAP_USABLE || entry->length == 0) {
            continue;
        }

        if (entry->length > UINT64_MAX - entry->base) {
            reset_allocator(allocator);
            return DODO_PHYS_INVALID_MAP;
        }

        if (entry->base > UINT64_MAX - DODO_PAGE_MASK) {
            reset_allocator(allocator);
            return DODO_PHYS_INVALID_MAP;
        }

        const uint64_t end = entry->base + entry->length;
        const uint64_t start =
            (entry->base + DODO_PAGE_MASK) & ~DODO_PAGE_MASK;
        const uint64_t aligned_end = end & ~DODO_PAGE_MASK;

        if (aligned_end <= start) {
            continue;
        }

        if (allocator->region_count >= DODO_PHYS_MAX_REGIONS) {
            reset_allocator(allocator);
            return DODO_PHYS_CAPACITY_EXCEEDED;
        }

        /* Reject overlapping usable ranges to prevent duplicate allocation. */
        for (uint64_t j = 0; j < allocator->region_count; ++j) {
            const struct dodo_phys_region *previous = &allocator->regions[j];
            if (start < previous->end && previous->start < aligned_end) {
                reset_allocator(allocator);
                return DODO_PHYS_REGION_OVERLAP;
            }
        }

        const uint64_t page_count =
            (aligned_end - start) / DODO_PHYS_PAGE_SIZE;

        if (page_count > UINT64_MAX - allocator->free_pages) {
            reset_allocator(allocator);
            return DODO_PHYS_INVALID_MAP;
        }

        struct dodo_phys_region *region =
            &allocator->regions[allocator->region_count];
        region->start = start;
        region->cursor = start;
        region->end = aligned_end;

        allocator->region_count++;
        allocator->free_pages += page_count;
    }

    allocator->initialized = 1;
    return DODO_PHYS_OK;
}

enum dodo_phys_status dodo_phys_alloc_page(
    struct dodo_phys_allocator *allocator,
    uint64_t *physical_address) {
    if (allocator == 0 || physical_address == 0) {
        return DODO_PHYS_INVALID_ARGUMENT;
    }

    if (allocator->initialized == 0) {
        return DODO_PHYS_INVALID_ARGUMENT;
    }

    while (allocator->current_region < allocator->region_count) {
        struct dodo_phys_region *region =
            &allocator->regions[allocator->current_region];

        if (region->cursor >= region->end) {
            allocator->current_region++;
            continue;
        }

        if (region->cursor % DODO_PHYS_PAGE_SIZE != 0 ||
            region->end - region->cursor < DODO_PHYS_PAGE_SIZE) {
            /* A violated invariant must not result in an unsafe allocation. */
            allocator->initialized = 0;
            return DODO_PHYS_INVALID_MAP;
        }

        *physical_address = region->cursor;
        region->cursor += DODO_PHYS_PAGE_SIZE;
        allocator->free_pages--;
        allocator->allocated_pages++;
        return DODO_PHYS_OK;
    }

    return DODO_PHYS_NO_MEMORY;
}
