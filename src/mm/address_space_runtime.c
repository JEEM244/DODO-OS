#include <stddef.h>
#include <stdint.h>
#include "dodo/mm/address_space_runtime.h"

static enum dodo_address_space_runtime_status map_paging_status(
    enum dodo_paging_status status) {
    return status == DODO_PAGING_OK ? DODO_ADDRESS_SPACE_RUNTIME_OK :
        DODO_ADDRESS_SPACE_RUNTIME_PAGING_ERROR;
}

static int valid_runtime_mapping_flags(uint64_t flags) {
    const uint64_t allowed = DODO_PAGE_WRITABLE |
        DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE;
    return (flags & ~allowed) == 0u &&
        (flags & DODO_PAGE_USER_ACCESSIBLE) != 0u &&
        (flags & DODO_PAGE_GLOBAL) == 0u &&
        ((flags & DODO_PAGE_WRITABLE) == 0u ||
         (flags & DODO_PAGE_NO_EXECUTE) != 0u);
}

static enum dodo_address_space_runtime_status map_owned_status(
    enum dodo_owned_frames_status status) {
    return status == DODO_OWNED_FRAMES_OK ? DODO_ADDRESS_SPACE_RUNTIME_OK :
        DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR;
}

static enum dodo_address_space_runtime_status release_frame(
    struct dodo_owned_frames *frames, uint32_t owner_id, uint64_t physical) {
    return map_owned_status(dodo_owned_frames_release(frames, owner_id, physical));
}

static enum dodo_address_space_runtime_status allocate_frame(
    struct dodo_owned_frames *frames, uint32_t owner_id,
    enum dodo_frame_kind kind, uint64_t *physical) {
    return map_owned_status(dodo_owned_frames_alloc(frames, owner_id, kind, physical));
}

enum dodo_address_space_runtime_status dodo_address_space_runtime_create(
    struct dodo_address_space_runtime *runtime,
    struct dodo_owned_frames *frames,
    uint32_t owner_id,
    uint64_t virtual_start,
    uint64_t virtual_end) {
    if (runtime == NULL || frames == NULL || owner_id == DODO_FRAME_OWNER_INVALID) {
        return DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT;
    }
    struct dodo_vaddr_space range_probe;
    if (virtual_start < DODO_VADDR_PAGE_SIZE ||
        dodo_vaddr_init(&range_probe, virtual_start, virtual_end) != DODO_VADDR_OK) {
        return DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT;
    }
    if (dodo_owned_frames_validate(frames) != DODO_OWNED_FRAMES_OK) {
        return DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR;
    }
    uint32_t existing_owner_frames = 0u;
    if (dodo_owned_frames_count_owner(frames, owner_id, &existing_owner_frames) !=
            DODO_OWNED_FRAMES_OK || existing_owner_frames != 0u) {
        return DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR;
    }

    uint64_t root = 0u;
    enum dodo_address_space_runtime_status status =
        allocate_frame(frames, owner_id, DODO_FRAME_KIND_ROOT, &root);
    if (status != DODO_ADDRESS_SPACE_RUNTIME_OK) {
        return status;
    }

    if (dodo_x86_64_paging_create_user_root(root) != DODO_PAGING_OK) {
        (void)release_frame(frames, owner_id, root);
        return DODO_ADDRESS_SPACE_RUNTIME_PAGING_ERROR;
    }

    const enum dodo_address_space_status metadata_status =
        dodo_address_space_init(&runtime->metadata, frames, owner_id,
                                virtual_start, virtual_end);
    if (metadata_status != DODO_ADDRESS_SPACE_OK) {
        (void)release_frame(frames, owner_id, root);
        return metadata_status == DODO_ADDRESS_SPACE_OWNERSHIP_ERROR ?
            DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR :
            DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT;
    }

    runtime->root_physical = root;
    runtime->table_frame_count = 0u;
    for (uint32_t i = 0u; i < DODO_ADDRESS_SPACE_RUNTIME_MAX_TABLE_FRAMES; ++i) {
        runtime->table_frames[i] = 0u;
    }
    runtime->initialized = 1u;
    return DODO_ADDRESS_SPACE_RUNTIME_OK;
}

enum dodo_address_space_runtime_status dodo_address_space_runtime_validate(
    const struct dodo_address_space_runtime *runtime) {
    if (runtime == NULL || runtime->initialized != 1u ||
        runtime->root_physical == 0u ||
        (runtime->root_physical & (DODO_PAGE_SIZE - 1u)) != 0u ||
        runtime->table_frame_count > DODO_ADDRESS_SPACE_RUNTIME_MAX_TABLE_FRAMES) {
        return DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE;
    }
    if (dodo_address_space_validate(&runtime->metadata) != DODO_ADDRESS_SPACE_OK) {
        return DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR;
    }

    enum dodo_frame_kind kind = DODO_FRAME_KIND_UNUSED;
    if (dodo_owned_frames_get_kind(runtime->metadata.frames,
            runtime->metadata.owner_id, runtime->root_physical, &kind) !=
            DODO_OWNED_FRAMES_OK || kind != DODO_FRAME_KIND_ROOT) {
        return DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR;
    }

    for (uint32_t i = runtime->table_frame_count;
         i < DODO_ADDRESS_SPACE_RUNTIME_MAX_TABLE_FRAMES; ++i) {
        if (runtime->table_frames[i] != 0u) {
            return DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE;
        }
    }

    for (uint32_t i = 0u; i < runtime->table_frame_count; ++i) {
        if (runtime->table_frames[i] == 0u ||
            runtime->table_frames[i] == runtime->root_physical ||
            (runtime->table_frames[i] & (DODO_PAGE_SIZE - 1u)) != 0u) {
            return DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE;
        }
        for (uint32_t j = 0u; j < i; ++j) {
            if (runtime->table_frames[i] == runtime->table_frames[j]) {
                return DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE;
            }
        }
        kind = DODO_FRAME_KIND_UNUSED;
        if (dodo_owned_frames_get_kind(runtime->metadata.frames,
                runtime->metadata.owner_id, runtime->table_frames[i], &kind) !=
                DODO_OWNED_FRAMES_OK || kind != DODO_FRAME_KIND_PAGE_TABLE) {
            return DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR;
        }
    }

    for (uint32_t i = 0u; i < runtime->metadata.mapping_count; ++i) {
        if (runtime->metadata.mappings[i].frame_policy !=
                DODO_ADDRESS_SPACE_OWNED_FRAME) {
            return DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE;
        }
    }

    uint32_t owner_count = 0u;
    if (dodo_owned_frames_count_owner(runtime->metadata.frames,
            runtime->metadata.owner_id, &owner_count) != DODO_OWNED_FRAMES_OK ||
        owner_count != 1u + runtime->table_frame_count +
                       runtime->metadata.mapping_count) {
        return DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR;
    }
    return DODO_ADDRESS_SPACE_RUNTIME_OK;
}

enum dodo_address_space_runtime_status dodo_address_space_runtime_activate(
    struct dodo_address_space_runtime *runtime) {
    const enum dodo_address_space_runtime_status status =
        dodo_address_space_runtime_validate(runtime);
    if (status != DODO_ADDRESS_SPACE_RUNTIME_OK) {
        return status;
    }
    return dodo_x86_64_paging_switch_root(runtime->root_physical) == DODO_PAGING_OK
        ? DODO_ADDRESS_SPACE_RUNTIME_OK
        : DODO_ADDRESS_SPACE_RUNTIME_ROOT_SWITCH_ERROR;
}

enum dodo_address_space_runtime_status dodo_address_space_runtime_map(
    struct dodo_address_space_runtime *runtime,
    uint64_t virtual_address,
    uint64_t flags) {
    enum dodo_address_space_runtime_status status =
        dodo_address_space_runtime_validate(runtime);
    if (status != DODO_ADDRESS_SPACE_RUNTIME_OK) {
        return status;
    }
    if (runtime->metadata.mapping_count >= DODO_ADDRESS_SPACE_MAX_MAPPINGS) {
        return DODO_ADDRESS_SPACE_RUNTIME_CAPACITY;
    }
    if (runtime->table_frame_count >
        DODO_ADDRESS_SPACE_RUNTIME_MAX_TABLE_FRAMES - 3u) {
        return DODO_ADDRESS_SPACE_RUNTIME_CAPACITY;
    }
    if (!valid_runtime_mapping_flags(flags) ||
        (virtual_address & (DODO_VADDR_PAGE_SIZE - 1u)) != 0u) {
        return DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT;
    }
    struct dodo_vaddr_space virtual_probe = runtime->metadata.virtual_space;
    const enum dodo_vaddr_status reserve_probe = dodo_vaddr_reserve(
        &virtual_probe, virtual_address, DODO_VADDR_PAGE_SIZE);
    if (reserve_probe == DODO_VADDR_CAPACITY) {
        return DODO_ADDRESS_SPACE_RUNTIME_CAPACITY;
    }
    if (reserve_probe != DODO_VADDR_OK) {
        return DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT;
    }

    uint64_t data_frame = 0u;
    status = allocate_frame(runtime->metadata.frames, runtime->metadata.owner_id,
                            DODO_FRAME_KIND_USER_DATA, &data_frame);
    if (status != DODO_ADDRESS_SPACE_RUNTIME_OK) {
        return status;
    }

    const enum dodo_address_space_status recorded =
        dodo_address_space_record_mapped_page(&runtime->metadata, virtual_address,
            data_frame, flags, DODO_ADDRESS_SPACE_OWNED_FRAME);
    if (recorded != DODO_ADDRESS_SPACE_OK) {
        (void)release_frame(runtime->metadata.frames, runtime->metadata.owner_id,
                            data_frame);
        return recorded == DODO_ADDRESS_SPACE_CAPACITY ?
            DODO_ADDRESS_SPACE_RUNTIME_CAPACITY :
            (recorded == DODO_ADDRESS_SPACE_OWNERSHIP_ERROR ?
                DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR :
                DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT);
    }

    uint64_t reserved_tables[3] = { 0u, 0u, 0u };
    uint64_t allocated_tables = 0u;
    for (; allocated_tables < 3u; ++allocated_tables) {
        status = allocate_frame(runtime->metadata.frames, runtime->metadata.owner_id,
                                DODO_FRAME_KIND_PAGE_TABLE,
                                &reserved_tables[allocated_tables]);
        if (status != DODO_ADDRESS_SPACE_RUNTIME_OK) {
            while (allocated_tables > 0u) {
                --allocated_tables;
                (void)release_frame(runtime->metadata.frames,
                    runtime->metadata.owner_id, reserved_tables[allocated_tables]);
            }
            (void)dodo_address_space_forget_unmapped_page(&runtime->metadata,
                                                          virtual_address);
            return status;
        }
    }

    const uint64_t previous_root = dodo_x86_64_active_root_physical();
    if (previous_root == 0u ||
        dodo_x86_64_paging_switch_root(runtime->root_physical) != DODO_PAGING_OK) {
        for (uint64_t i = 0u; i < allocated_tables; ++i) {
            (void)release_frame(runtime->metadata.frames, runtime->metadata.owner_id,
                                reserved_tables[i]);
        }
        (void)dodo_address_space_forget_unmapped_page(&runtime->metadata,
                                                      virtual_address);
        return DODO_ADDRESS_SPACE_RUNTIME_ROOT_SWITCH_ERROR;
    }

    uint64_t tables_used = 0u;
    const enum dodo_paging_status map_status = dodo_x86_64_map_page(
        virtual_address, data_frame, flags, reserved_tables, allocated_tables,
        &tables_used);

    if (map_status == DODO_PAGING_OK && tables_used <= allocated_tables &&
        tables_used <= DODO_ADDRESS_SPACE_RUNTIME_MAX_TABLE_FRAMES -
                       runtime->table_frame_count) {
        for (uint64_t i = 0u; i < tables_used; ++i) {
            runtime->table_frames[runtime->table_frame_count++] = reserved_tables[i];
        }
        for (uint64_t i = tables_used; i < allocated_tables; ++i) {
            (void)release_frame(runtime->metadata.frames, runtime->metadata.owner_id,
                                reserved_tables[i]);
        }
    } else {
        /* A failed low-level map rolls back all page-table links it created. */
        for (uint64_t i = 0u; i < allocated_tables; ++i) {
            (void)release_frame(runtime->metadata.frames, runtime->metadata.owner_id,
                                reserved_tables[i]);
        }
        (void)dodo_address_space_forget_unmapped_page(&runtime->metadata,
                                                      virtual_address);
    }

    const int restore_failed =
        dodo_x86_64_paging_switch_root(previous_root) != DODO_PAGING_OK;
    if (restore_failed) {
        return DODO_ADDRESS_SPACE_RUNTIME_ROOT_SWITCH_ERROR;
    }
    if (map_status != DODO_PAGING_OK) {
        return map_paging_status(map_status);
    }
    if (tables_used > allocated_tables) {
        return DODO_ADDRESS_SPACE_RUNTIME_PAGING_ERROR;
    }
    return DODO_ADDRESS_SPACE_RUNTIME_OK;
}

enum dodo_address_space_runtime_status dodo_address_space_runtime_unmap(
    struct dodo_address_space_runtime *runtime,
    uint64_t virtual_address) {
    enum dodo_address_space_runtime_status status =
        dodo_address_space_runtime_validate(runtime);
    if (status != DODO_ADDRESS_SPACE_RUNTIME_OK) {
        return status;
    }

    struct dodo_address_space_mapping mapping;
    if (dodo_address_space_lookup(&runtime->metadata, virtual_address, &mapping) !=
        DODO_ADDRESS_SPACE_OK) {
        return DODO_ADDRESS_SPACE_RUNTIME_NOT_MAPPED;
    }
    const uint64_t previous_root = dodo_x86_64_active_root_physical();
    if (previous_root == 0u ||
        dodo_x86_64_paging_switch_root(runtime->root_physical) != DODO_PAGING_OK) {
        return DODO_ADDRESS_SPACE_RUNTIME_ROOT_SWITCH_ERROR;
    }

    uint64_t actual_physical = 0u;
    uint64_t actual_flags = 0u;
    enum dodo_paging_status paging_status = dodo_x86_64_query_page(
        virtual_address, &actual_physical, &actual_flags);
    if (paging_status != DODO_PAGING_OK ||
        actual_physical != mapping.physical_address || actual_flags != mapping.flags) {
        (void)dodo_x86_64_paging_switch_root(previous_root);
        return DODO_ADDRESS_SPACE_RUNTIME_PAGING_ERROR;
    }

    paging_status = dodo_x86_64_unmap_page(virtual_address,
                                           &actual_physical, &actual_flags);
    if (paging_status != DODO_PAGING_OK) {
        const int restore_failed =
            dodo_x86_64_paging_switch_root(previous_root) != DODO_PAGING_OK;
        return restore_failed ? DODO_ADDRESS_SPACE_RUNTIME_ROOT_SWITCH_ERROR :
            DODO_ADDRESS_SPACE_RUNTIME_PAGING_ERROR;
    }
    if (actual_physical != mapping.physical_address ||
        actual_flags != mapping.flags) {
        const int restore_failed =
            dodo_x86_64_paging_switch_root(previous_root) != DODO_PAGING_OK;
        return restore_failed ? DODO_ADDRESS_SPACE_RUNTIME_ROOT_SWITCH_ERROR :
            DODO_ADDRESS_SPACE_RUNTIME_PAGING_ERROR;
    }

    const enum dodo_address_space_status forgotten =
        dodo_address_space_forget_unmapped_page(&runtime->metadata, virtual_address);
    const int restore_failed =
        dodo_x86_64_paging_switch_root(previous_root) != DODO_PAGING_OK;
    if (forgotten != DODO_ADDRESS_SPACE_OK) {
        return DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR;
    }
    return restore_failed ? DODO_ADDRESS_SPACE_RUNTIME_ROOT_SWITCH_ERROR :
        DODO_ADDRESS_SPACE_RUNTIME_OK;
}

enum dodo_address_space_runtime_status dodo_address_space_runtime_destroy(
    struct dodo_address_space_runtime *runtime) {
    enum dodo_address_space_runtime_status status =
        dodo_address_space_runtime_validate(runtime);
    if (status != DODO_ADDRESS_SPACE_RUNTIME_OK) {
        return status;
    }

    const uint64_t previous_root = dodo_x86_64_active_root_physical();
    if (previous_root == 0u || previous_root == runtime->root_physical) {
        return DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE;
    }
    if (dodo_x86_64_paging_switch_root(runtime->root_physical) != DODO_PAGING_OK) {
        return DODO_ADDRESS_SPACE_RUNTIME_ROOT_SWITCH_ERROR;
    }

    while (runtime->metadata.mapping_count > 0u) {
        const uint64_t virtual_address =
            runtime->metadata.mappings[0].virtual_address;
        uint64_t physical = 0u;
        uint64_t flags = 0u;
        if (dodo_x86_64_query_page(virtual_address, &physical, &flags) !=
                DODO_PAGING_OK ||
            physical != runtime->metadata.mappings[0].physical_address ||
            flags != runtime->metadata.mappings[0].flags ||
            dodo_x86_64_unmap_page(virtual_address, &physical, &flags) !=
                DODO_PAGING_OK ||
            dodo_address_space_forget_unmapped_page(&runtime->metadata,
                virtual_address) != DODO_ADDRESS_SPACE_OK) {
            (void)dodo_x86_64_paging_switch_root(previous_root);
            return DODO_ADDRESS_SPACE_RUNTIME_PAGING_ERROR;
        }
    }

    if (dodo_x86_64_paging_switch_root(previous_root) != DODO_PAGING_OK) {
        return DODO_ADDRESS_SPACE_RUNTIME_ROOT_SWITCH_ERROR;
    }
    status = dodo_address_space_runtime_validate(runtime);
    if (status != DODO_ADDRESS_SPACE_RUNTIME_OK) {
        return status;
    }

    /* Preflight every retained table frame before releasing any of them. */
    for (uint32_t i = 0u; i < runtime->table_frame_count; ++i) {
        enum dodo_frame_kind kind = DODO_FRAME_KIND_UNUSED;
        if (dodo_owned_frames_get_kind(runtime->metadata.frames,
                runtime->metadata.owner_id, runtime->table_frames[i], &kind) !=
                DODO_OWNED_FRAMES_OK || kind != DODO_FRAME_KIND_PAGE_TABLE) {
            return DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR;
        }
    }
    enum dodo_frame_kind root_kind = DODO_FRAME_KIND_UNUSED;
    if (dodo_owned_frames_get_kind(runtime->metadata.frames,
            runtime->metadata.owner_id, runtime->root_physical, &root_kind) !=
            DODO_OWNED_FRAMES_OK || root_kind != DODO_FRAME_KIND_ROOT) {
        return DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR;
    }

    for (uint32_t i = runtime->table_frame_count; i > 0u; --i) {
        if (release_frame(runtime->metadata.frames, runtime->metadata.owner_id,
                          runtime->table_frames[i - 1u]) !=
            DODO_ADDRESS_SPACE_RUNTIME_OK) {
            return DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR;
        }
    }
    if (release_frame(runtime->metadata.frames, runtime->metadata.owner_id,
                      runtime->root_physical) != DODO_ADDRESS_SPACE_RUNTIME_OK) {
        return DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR;
    }

    for (uint32_t i = 0u; i < DODO_ADDRESS_SPACE_RUNTIME_MAX_TABLE_FRAMES; ++i) {
        runtime->table_frames[i] = 0u;
    }
    runtime->metadata.initialized = 0u;
    runtime->root_physical = 0u;
    runtime->table_frame_count = 0u;
    runtime->initialized = 0u;
    return DODO_ADDRESS_SPACE_RUNTIME_OK;
}
