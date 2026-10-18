#ifndef DODO_MM_PHYS_H
#define DODO_MM_PHYS_H

#include <stdint.h>

#include "dodo/boot/limine_protocol.h"

#define DODO_PHYS_PAGE_SIZE UINT64_C(4096)
#define DODO_PHYS_MAX_REGIONS UINT64_C(4096)

enum dodo_phys_status {
    DODO_PHYS_OK = 0,
    DODO_PHYS_INVALID_ARGUMENT = 1,
    DODO_PHYS_INVALID_MAP = 2,
    DODO_PHYS_REGION_OVERLAP = 3,
    DODO_PHYS_CAPACITY_EXCEEDED = 4,
    DODO_PHYS_NO_MEMORY = 5
};

struct dodo_phys_region {
    uint64_t start;
    uint64_t cursor;
    uint64_t end; /* Exclusive, page-aligned physical address. */
};

struct dodo_phys_allocator {
    struct dodo_phys_region regions[DODO_PHYS_MAX_REGIONS];
    uint64_t region_count;
    uint64_t current_region;
    uint64_t free_pages;
    uint64_t allocated_pages;
    uint64_t initialized;
};

/*
 * Prepare a one-shot physical page-frame allocator from the bootloader map.
 * Only whole 4 KiB pages wholly contained in usable ranges are included.
 * This allocator returns physical addresses; it does not map or dereference
 * them, and must be connected to DODO's own virtual-memory manager later.
 */
enum dodo_phys_status dodo_phys_init(
    struct dodo_phys_allocator *allocator,
    const struct dodo_limine_memmap_response *response);

/* Return one 4 KiB physical page address, or NO_MEMORY when exhausted. */
enum dodo_phys_status dodo_phys_alloc_page(
    struct dodo_phys_allocator *allocator,
    uint64_t *physical_address);

#endif
