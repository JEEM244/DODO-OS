#include <stddef.h>
#include <stdint.h>
#include "dodo/mm/address_space.h"

#define DODO_ADDRESS_SPACE_USER_FLAGS (DODO_PAGE_WRITABLE | DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE)
#define DODO_ADDRESS_SPACE_PHYSICAL_MASK UINT64_C(0x000ffffffffff000)

static int valid_frame_policy(enum dodo_address_space_frame_policy policy) {
    return policy == DODO_ADDRESS_SPACE_BORROWED_FRAME ||
           policy == DODO_ADDRESS_SPACE_OWNED_FRAME;
}

static int owns_user_data_frame(const struct dodo_address_space *space,
                                  uint64_t physical_address) {
    for (uint32_t i = 0u; i < DODO_FRAME_LEDGER_CAPACITY; ++i) {
        const struct dodo_frame_record *record = &space->frames->ledger.records[i];
        if (record->physical_address == physical_address) {
            return record->owner_id == space->owner_id &&
                   record->kind == DODO_FRAME_KIND_USER_DATA;
        }
    }
    return 0;
}

static int valid_mapping_values(uint64_t virtual_address, uint64_t physical_address,
                                uint64_t flags,
                                enum dodo_address_space_frame_policy policy) {
    if ((virtual_address & (DODO_VADDR_PAGE_SIZE - 1u)) != 0u ||
        virtual_address >= DODO_X86_64_USER_ADDRESS_LIMIT ||
        physical_address == 0u ||
        (physical_address & (DODO_VADDR_PAGE_SIZE - 1u)) != 0u ||
        physical_address > DODO_ADDRESS_SPACE_PHYSICAL_MASK ||
        (flags & ~DODO_ADDRESS_SPACE_USER_FLAGS) != 0u ||
        (flags & DODO_PAGE_USER_ACCESSIBLE) == 0u ||
        (flags & DODO_PAGE_GLOBAL) != 0u ||
        ((flags & DODO_PAGE_WRITABLE) != 0u &&
         (flags & DODO_PAGE_NO_EXECUTE) == 0u) ||
        !valid_frame_policy(policy)) {
        return 0;
    }
    return 1;
}

enum dodo_address_space_status dodo_address_space_init(
    struct dodo_address_space *space, struct dodo_owned_frames *frames,
    uint32_t owner_id, uint64_t virtual_start, uint64_t virtual_end) {
    if (space == NULL || frames == NULL || owner_id == DODO_FRAME_OWNER_INVALID) {
        return DODO_ADDRESS_SPACE_INVALID_ARGUMENT;
    }
    if (dodo_owned_frames_validate(frames) != DODO_OWNED_FRAMES_OK) {
        return DODO_ADDRESS_SPACE_OWNERSHIP_ERROR;
    }
    /*
     * Keep the null page unmappable in every user address space. The generic
     * virtual-range allocator may represent address zero, but process mappings
     * must not make a null dereference silently access user data.
     */
    if (virtual_start < DODO_VADDR_PAGE_SIZE) {
        return DODO_ADDRESS_SPACE_RANGE_ERROR;
    }
    const enum dodo_vaddr_status range_status =
        dodo_vaddr_init(&space->virtual_space, virtual_start, virtual_end);
    if (range_status != DODO_VADDR_OK) {
        return DODO_ADDRESS_SPACE_RANGE_ERROR;
    }
    space->owner_id = owner_id;
    space->frames = frames;
    space->mapping_count = 0u;
    space->initialized = 1u;
    for (uint32_t i = 0u; i < DODO_ADDRESS_SPACE_MAX_MAPPINGS; ++i) {
        space->mappings[i] = (struct dodo_address_space_mapping){0};
    }
    return DODO_ADDRESS_SPACE_OK;
}

enum dodo_address_space_status dodo_address_space_validate(
    const struct dodo_address_space *space) {
    if (space == NULL || space->initialized != 1u || space->frames == NULL ||
        space->owner_id == DODO_FRAME_OWNER_INVALID ||
        space->mapping_count > DODO_ADDRESS_SPACE_MAX_MAPPINGS ||
        dodo_vaddr_validate(&space->virtual_space) != DODO_VADDR_OK ||
        space->virtual_space.count != space->mapping_count) {
        return DODO_ADDRESS_SPACE_INVALID_STATE;
    }
    if (dodo_owned_frames_validate(space->frames) != DODO_OWNED_FRAMES_OK) {
        return DODO_ADDRESS_SPACE_OWNERSHIP_ERROR;
    }
    for (uint32_t i = 0u; i < space->mapping_count; ++i) {
        const struct dodo_address_space_mapping *mapping = &space->mappings[i];
        if (!valid_mapping_values(mapping->virtual_address, mapping->physical_address,
                                  mapping->flags, mapping->frame_policy) ||
            mapping->virtual_address != space->virtual_space.allocated[i].base ||
            space->virtual_space.allocated[i].length != DODO_VADDR_PAGE_SIZE ||
            (i > 0u && space->mappings[i - 1u].virtual_address >=
                       mapping->virtual_address)) {
            return DODO_ADDRESS_SPACE_INVALID_STATE;
        }
        if (mapping->frame_policy == DODO_ADDRESS_SPACE_OWNED_FRAME) {
            if (!owns_user_data_frame(space, mapping->physical_address)) {
                return DODO_ADDRESS_SPACE_OWNERSHIP_ERROR;
            }
            for (uint32_t j = 0u; j < space->mapping_count; ++j) {
                if (j != i &&
                    space->mappings[j].physical_address == mapping->physical_address) {
                    /*
                     * The owned mapping is the sole lifetime authority for this
                     * frame. Any alias would outlive or depend on its release.
                     * Alias management needs an explicit shared-reference model.
                     */
                    return DODO_ADDRESS_SPACE_OWNERSHIP_ERROR;
                }
            }
        }
    }
    for (uint32_t i = space->mapping_count;
         i < DODO_ADDRESS_SPACE_MAX_MAPPINGS; ++i) {
        const struct dodo_address_space_mapping *unused = &space->mappings[i];
        if (unused->virtual_address != 0u || unused->physical_address != 0u ||
            unused->flags != 0u ||
            unused->frame_policy != DODO_ADDRESS_SPACE_BORROWED_FRAME) {
            return DODO_ADDRESS_SPACE_INVALID_STATE;
        }
    }
    return DODO_ADDRESS_SPACE_OK;
}

enum dodo_address_space_status dodo_address_space_record_mapped_page(
    struct dodo_address_space *space, uint64_t virtual_address,
    uint64_t physical_address, uint64_t flags,
    enum dodo_address_space_frame_policy frame_policy) {
    if (space == NULL ||
        !valid_mapping_values(virtual_address, physical_address, flags, frame_policy)) {
        return DODO_ADDRESS_SPACE_INVALID_ARGUMENT;
    }
    const enum dodo_address_space_status validation =
        dodo_address_space_validate(space);
    if (validation != DODO_ADDRESS_SPACE_OK) {
        return validation;
    }
    if (space->mapping_count >= DODO_ADDRESS_SPACE_MAX_MAPPINGS) {
        return DODO_ADDRESS_SPACE_CAPACITY;
    }
    if (frame_policy == DODO_ADDRESS_SPACE_OWNED_FRAME) {
        if (!owns_user_data_frame(space, physical_address)) {
            return DODO_ADDRESS_SPACE_OWNERSHIP_ERROR;
        }
        /*
         * One owned frame must have one owning mapping. Additional aliases may
         * be recorded as borrowed, so teardown can never release the same frame
         * twice through two ownership records.
         */
        for (uint32_t i = 0u; i < space->mapping_count; ++i) {
            if (space->mappings[i].physical_address == physical_address) {
                return DODO_ADDRESS_SPACE_OWNERSHIP_ERROR;
            }
        }
    }
    const enum dodo_vaddr_status reserve_status =
        dodo_vaddr_reserve(&space->virtual_space, virtual_address, DODO_VADDR_PAGE_SIZE);
    if (reserve_status == DODO_VADDR_OVERLAP) {
        return DODO_ADDRESS_SPACE_ALREADY_MAPPED;
    }
    if (reserve_status != DODO_VADDR_OK) {
        return reserve_status == DODO_VADDR_CAPACITY ? DODO_ADDRESS_SPACE_CAPACITY :
            DODO_ADDRESS_SPACE_RANGE_ERROR;
    }
    uint32_t position = 0u;
    while (position < space->mapping_count &&
           space->mappings[position].virtual_address < virtual_address) {
        ++position;
    }
    for (uint32_t i = space->mapping_count; i > position; --i) {
        space->mappings[i] = space->mappings[i - 1u];
    }
    space->mappings[position] = (struct dodo_address_space_mapping){
        .virtual_address = virtual_address,
        .physical_address = physical_address,
        .flags = flags,
        .frame_policy = frame_policy
    };
    ++space->mapping_count;
    return DODO_ADDRESS_SPACE_OK;
}

enum dodo_address_space_status dodo_address_space_forget_unmapped_page(
    struct dodo_address_space *space, uint64_t virtual_address) {
    if (space == NULL || (virtual_address & (DODO_VADDR_PAGE_SIZE - 1u)) != 0u) {
        return DODO_ADDRESS_SPACE_INVALID_ARGUMENT;
    }
    const enum dodo_address_space_status validation =
        dodo_address_space_validate(space);
    if (validation != DODO_ADDRESS_SPACE_OK) {
        return validation;
    }
    uint32_t position = 0u;
    while (position < space->mapping_count &&
           space->mappings[position].virtual_address < virtual_address) {
        ++position;
    }
    if (position == space->mapping_count ||
        space->mappings[position].virtual_address != virtual_address) {
        return DODO_ADDRESS_SPACE_NOT_MAPPED;
    }
    const struct dodo_address_space_mapping mapping = space->mappings[position];
    if (dodo_vaddr_release(&space->virtual_space, virtual_address,
                           DODO_VADDR_PAGE_SIZE) != DODO_VADDR_OK) {
        return DODO_ADDRESS_SPACE_INVALID_STATE;
    }
    if (mapping.frame_policy == DODO_ADDRESS_SPACE_OWNED_FRAME &&
        dodo_owned_frames_release(space->frames, space->owner_id,
                                 mapping.physical_address) != DODO_OWNED_FRAMES_OK) {
        if (dodo_vaddr_reserve(&space->virtual_space, virtual_address,
                               DODO_VADDR_PAGE_SIZE) != DODO_VADDR_OK) {
            return DODO_ADDRESS_SPACE_INVALID_STATE;
        }
        return DODO_ADDRESS_SPACE_OWNERSHIP_ERROR;
    }
    for (uint32_t i = position + 1u; i < space->mapping_count; ++i) {
        space->mappings[i - 1u] = space->mappings[i];
    }
    --space->mapping_count;
    space->mappings[space->mapping_count] = (struct dodo_address_space_mapping){0};
    return DODO_ADDRESS_SPACE_OK;
}

enum dodo_address_space_status dodo_address_space_lookup(
    const struct dodo_address_space *space, uint64_t virtual_address,
    struct dodo_address_space_mapping *mapping_out) {
    if (space == NULL || mapping_out == NULL ||
        (virtual_address & (DODO_VADDR_PAGE_SIZE - 1u)) != 0u) {
        return DODO_ADDRESS_SPACE_INVALID_ARGUMENT;
    }
    const enum dodo_address_space_status validation =
        dodo_address_space_validate(space);
    if (validation != DODO_ADDRESS_SPACE_OK) {
        return validation;
    }
    for (uint32_t i = 0u; i < space->mapping_count; ++i) {
        if (space->mappings[i].virtual_address == virtual_address) {
            *mapping_out = space->mappings[i];
            return DODO_ADDRESS_SPACE_OK;
        }
    }
    return DODO_ADDRESS_SPACE_NOT_MAPPED;
}
