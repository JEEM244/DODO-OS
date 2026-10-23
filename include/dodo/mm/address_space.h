#ifndef DODO_MM_ADDRESS_SPACE_H
#define DODO_MM_ADDRESS_SPACE_H

#include <stdint.h>
#include "dodo/arch/x86_64/paging.h"
#include "dodo/mm/owned_frames.h"
#include "dodo/mm/vaddr.h"

#define DODO_ADDRESS_SPACE_MAX_MAPPINGS DODO_VADDR_MAX_RANGES

enum dodo_address_space_status {
    DODO_ADDRESS_SPACE_OK = 0,
    DODO_ADDRESS_SPACE_INVALID_ARGUMENT,
    DODO_ADDRESS_SPACE_INVALID_STATE,
    DODO_ADDRESS_SPACE_RANGE_ERROR,
    DODO_ADDRESS_SPACE_CAPACITY,
    DODO_ADDRESS_SPACE_ALREADY_MAPPED,
    DODO_ADDRESS_SPACE_NOT_MAPPED,
    DODO_ADDRESS_SPACE_OWNERSHIP_ERROR
};

enum dodo_address_space_frame_policy {
    DODO_ADDRESS_SPACE_BORROWED_FRAME = 0,
    DODO_ADDRESS_SPACE_OWNED_FRAME = 1
};

struct dodo_address_space_mapping {
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t flags;
    enum dodo_address_space_frame_policy frame_policy;
};

/*
 * Metadata-only first stage. This object does not modify hardware page tables.
 * record_mapped_page() is called only after the low-level mapper succeeds;
 * forget_unmapped_page() is called only after the low-level unmapper succeeds.
 * The object and frame pool are single-CPU bootstrap structures, not SMP-safe.
 */
struct dodo_address_space {
    uint32_t owner_id;
    uint32_t initialized;
    struct dodo_owned_frames *frames;
    struct dodo_vaddr_space virtual_space;
    uint32_t mapping_count;
    struct dodo_address_space_mapping mappings[DODO_ADDRESS_SPACE_MAX_MAPPINGS];
};

enum dodo_address_space_status dodo_address_space_init(
    struct dodo_address_space *space, struct dodo_owned_frames *frames,
    uint32_t owner_id, uint64_t virtual_start, uint64_t virtual_end);

enum dodo_address_space_status dodo_address_space_validate(
    const struct dodo_address_space *space);

/* Record an already-installed user mapping and reserve its virtual page. */
enum dodo_address_space_status dodo_address_space_record_mapped_page(
    struct dodo_address_space *space, uint64_t virtual_address,
    uint64_t physical_address, uint64_t flags,
    enum dodo_address_space_frame_policy frame_policy);

/* Record removal after hardware unmap; owned user frames are released here. */
enum dodo_address_space_status dodo_address_space_forget_unmapped_page(
    struct dodo_address_space *space, uint64_t virtual_address);

/* Return a mapping only when the complete metadata object validates. */
enum dodo_address_space_status dodo_address_space_lookup(
    const struct dodo_address_space *space, uint64_t virtual_address,
    struct dodo_address_space_mapping *mapping_out);

#endif
