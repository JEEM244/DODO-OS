#include "dodo/mm/phys.h"

#define DODO_MAX_MEMMAP_ENTRIES UINT64_C(4096)
#define DODO_PAGE_MASK (DODO_PHYS_PAGE_SIZE - UINT64_C(1))

static void reset_allocator(struct dodo_phys_allocator *allocator) {
    allocator->region_count = 0;
    allocator->current_region = 0;
    allocator->free_pages = 0;
    allocator->allocated_pages = 0;
    allocator->initialized = 0;
    allocator->free_stack_count = 0;

    for (uint64_t i = 0; i < DODO_PHYS_MAX_REGIONS; ++i) {
        allocator->regions[i].start = 0;
        allocator->regions[i].cursor = 0;
        allocator->regions[i].end = 0;
    }

    for (uint64_t i = 0; i < DODO_PHYS_MAX_TRACKED_PAGES; ++i) {
        allocator->allocated_frames[i] = 0;
        allocator->free_stack[i] = 0;
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

    if (allocator->allocated_pages >= DODO_PHYS_MAX_TRACKED_PAGES) {
        return DODO_PHYS_TRACKING_CAPACITY_EXCEEDED;
    }

    uint64_t page = 0;
    int page_found = 0;

    if (allocator->free_stack_count != 0) {
        page = allocator->free_stack[--allocator->free_stack_count];
        allocator->free_stack[allocator->free_stack_count] = 0;
        page_found = 1;
    } else {
        while (allocator->current_region < allocator->region_count) {
            struct dodo_phys_region *region =
                &allocator->regions[allocator->current_region];

            if (region->cursor >= region->end) {
                allocator->current_region++;
                continue;
            }

            if (region->cursor % DODO_PHYS_PAGE_SIZE != 0 ||
                region->end - region->cursor < DODO_PHYS_PAGE_SIZE) {
                /* A violated invariant must not result in unsafe allocation. */
                allocator->initialized = 0;
                return DODO_PHYS_INVALID_MAP;
            }

            page = region->cursor;
            region->cursor += DODO_PHYS_PAGE_SIZE;
            page_found = 1;
            break;
        }

        if (!page_found) {
            return DODO_PHYS_NO_MEMORY;
        }
    }

    if (allocator->free_pages == 0) {
        allocator->initialized = 0;
        return DODO_PHYS_INVALID_MAP;
    }

    allocator->allocated_frames[allocator->allocated_pages++] = page;
    allocator->free_pages--;
    *physical_address = page;
    return DODO_PHYS_OK;
}

enum dodo_phys_status dodo_phys_free_page(
    struct dodo_phys_allocator *allocator,
    uint64_t physical_address) {
    if (allocator == 0) {
        return DODO_PHYS_INVALID_ARGUMENT;
    }

    if (allocator->initialized == 0) {
        return DODO_PHYS_INVALID_ARGUMENT;
    }

    if ((physical_address & DODO_PAGE_MASK) != 0u) {
        return DODO_PHYS_INVALID_ARGUMENT;
    }

    uint64_t allocation_index = allocator->allocated_pages;
    for (uint64_t i = 0; i < allocator->allocated_pages; ++i) {
        if (allocator->allocated_frames[i] == physical_address) {
            allocation_index = i;
            break;
        }
    }

    if (allocation_index == allocator->allocated_pages) {
        for (uint64_t i = 0; i < allocator->free_stack_count; ++i) {
            if (allocator->free_stack[i] == physical_address) {
                return DODO_PHYS_DOUBLE_FREE;
            }
        }
        return DODO_PHYS_PAGE_NOT_ALLOCATED;
    }

    if (allocator->free_stack_count >= DODO_PHYS_MAX_TRACKED_PAGES) {
        return DODO_PHYS_TRACKING_CAPACITY_EXCEEDED;
    }

    /* Remove the active record in O(1) after lookup; order is not meaningful. */
    const uint64_t last_index = allocator->allocated_pages - 1u;
    allocator->allocated_frames[allocation_index] =
        allocator->allocated_frames[last_index];
    allocator->allocated_frames[last_index] = 0;
    allocator->allocated_pages--;

    allocator->free_stack[allocator->free_stack_count++] = physical_address;
    allocator->free_pages++;
    return DODO_PHYS_OK;
}


enum dodo_phys_status dodo_phys_add_region(
    struct dodo_phys_allocator *allocator,
    uint64_t base,
    uint64_t length) {
    if (allocator == 0 || allocator->initialized == 0) {
        return DODO_PHYS_INVALID_ARGUMENT;
    }

    if (length == 0 ||
        (base & DODO_PAGE_MASK) != 0u ||
        (length & DODO_PAGE_MASK) != 0u) {
        return DODO_PHYS_INVALID_ARGUMENT;
    }

    if (length > UINT64_MAX - base) {
        return DODO_PHYS_INVALID_MAP;
    }

    if (allocator->region_count >= DODO_PHYS_MAX_REGIONS) {
        return DODO_PHYS_CAPACITY_EXCEEDED;
    }

    const uint64_t end = base + length;
    for (uint64_t i = 0; i < allocator->region_count; ++i) {
        const struct dodo_phys_region *existing = &allocator->regions[i];
        if (base < existing->end && existing->start < end) {
            return DODO_PHYS_REGION_OVERLAP;
        }
    }

    const uint64_t page_count = length / DODO_PHYS_PAGE_SIZE;
    if (page_count > UINT64_MAX - allocator->free_pages) {
        return DODO_PHYS_INVALID_MAP;
    }

    struct dodo_phys_region *region = &allocator->regions[allocator->region_count];
    region->start = base;
    region->cursor = base;
    region->end = end;
    allocator->region_count++;
    allocator->free_pages += page_count;
    return DODO_PHYS_OK;
}
