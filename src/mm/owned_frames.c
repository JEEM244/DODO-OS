#include <stddef.h>
#include "dodo/mm/owned_frames.h"

void dodo_owned_frames_init(struct dodo_owned_frames *pool,
                            struct dodo_phys_allocator *allocator) {
    if (pool == NULL) {
        return;
    }
    pool->allocator = allocator;
    dodo_frame_ledger_init(&pool->ledger);
}

enum dodo_owned_frames_status dodo_owned_frames_validate(
    const struct dodo_owned_frames *pool) {
    if (pool == NULL || pool->allocator == NULL ||
        pool->allocator->initialized == 0u ||
        pool->allocator->allocated_pages > DODO_PHYS_MAX_TRACKED_PAGES) {
        return DODO_OWNED_FRAMES_INVALID_ARGUMENT;
    }

    if (dodo_frame_ledger_validate(&pool->ledger) != DODO_FRAME_LEDGER_OK) {
        return DODO_OWNED_FRAMES_LEDGER_ERROR;
    }

    /*
     * The physical allocator may also contain frames owned by other bootstrap
     * subsystems, so validate the ledger-to-allocator direction only.
     */
    for (uint32_t i = 0u; i < DODO_FRAME_LEDGER_CAPACITY; ++i) {
        const struct dodo_frame_record *record = &pool->ledger.records[i];
        if (record->kind == DODO_FRAME_KIND_UNUSED) {
            continue;
        }

        int found = 0;
        for (uint64_t j = 0u; j < pool->allocator->allocated_pages; ++j) {
            if (pool->allocator->allocated_frames[j] ==
                record->physical_address) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return DODO_OWNED_FRAMES_ALLOCATOR_ERROR;
        }
    }

    return DODO_OWNED_FRAMES_OK;
}

enum dodo_owned_frames_status dodo_owned_frames_count_owner(
    const struct dodo_owned_frames *pool, uint32_t owner_id,
    uint32_t *count_out) {
    if (pool == NULL || owner_id == DODO_FRAME_OWNER_INVALID ||
        count_out == NULL) {
        return DODO_OWNED_FRAMES_INVALID_ARGUMENT;
    }

    const enum dodo_owned_frames_status validation =
        dodo_owned_frames_validate(pool);
    if (validation != DODO_OWNED_FRAMES_OK) {
        return validation;
    }

    if (dodo_frame_ledger_count_owner(&pool->ledger, owner_id, count_out) !=
        DODO_FRAME_LEDGER_OK) {
        return DODO_OWNED_FRAMES_LEDGER_ERROR;
    }
    return DODO_OWNED_FRAMES_OK;
}

enum dodo_owned_frames_status dodo_owned_frames_alloc(
    struct dodo_owned_frames *pool, uint32_t owner_id,
    enum dodo_frame_kind kind, uint64_t *physical_address) {
    if (pool == NULL || pool->allocator == NULL ||
        physical_address == NULL || owner_id == DODO_FRAME_OWNER_INVALID ||
        kind < DODO_FRAME_KIND_USER_DATA || kind > DODO_FRAME_KIND_ROOT) {
        return DODO_OWNED_FRAMES_INVALID_ARGUMENT;
    }

    /*
     * Fail closed if ownership metadata is already corrupt. Do not allocate a
     * physical page when the ledger cannot safely accept or describe it.
     */
    const enum dodo_owned_frames_status validation =
        dodo_owned_frames_validate(pool);
    if (validation != DODO_OWNED_FRAMES_OK) {
        return validation;
    }

    uint64_t frame = 0;
    if (dodo_phys_alloc_page(pool->allocator, &frame) != DODO_PHYS_OK) {
        return DODO_OWNED_FRAMES_ALLOCATOR_ERROR;
    }

    /* Physical zero is valid to the PMM but reserved as an invalid ledger key. */
    if (frame == 0u ||
        dodo_frame_ledger_add(&pool->ledger, frame, owner_id, kind) !=
            DODO_FRAME_LEDGER_OK) {
        if (dodo_phys_free_page(pool->allocator, frame) != DODO_PHYS_OK) {
            return DODO_OWNED_FRAMES_ROLLBACK_ERROR;
        }
        return DODO_OWNED_FRAMES_LEDGER_ERROR;
    }

    *physical_address = frame;
    return DODO_OWNED_FRAMES_OK;
}

enum dodo_owned_frames_status dodo_owned_frames_release(
    struct dodo_owned_frames *pool, uint32_t owner_id,
    uint64_t physical_address) {
    if (pool == NULL || pool->allocator == NULL ||
        owner_id == DODO_FRAME_OWNER_INVALID || physical_address == 0u ||
        (physical_address & (DODO_PHYS_PAGE_SIZE - 1u)) != 0u) {
        return DODO_OWNED_FRAMES_INVALID_ARGUMENT;
    }

    const enum dodo_owned_frames_status validation =
        dodo_owned_frames_validate(pool);
    if (validation != DODO_OWNED_FRAMES_OK) {
        return validation;
    }

    enum dodo_frame_kind kind;
    if (dodo_frame_ledger_remove(&pool->ledger, physical_address, owner_id,
                                 &kind) != DODO_FRAME_LEDGER_OK) {
        return DODO_OWNED_FRAMES_LEDGER_ERROR;
    }

    if (dodo_phys_free_page(pool->allocator, physical_address) != DODO_PHYS_OK) {
        /* Restore ownership if the PMM refuses to release the frame. */
        if (dodo_frame_ledger_add(&pool->ledger, physical_address, owner_id,
                                  kind) != DODO_FRAME_LEDGER_OK) {
            return DODO_OWNED_FRAMES_ROLLBACK_ERROR;
        }
        return DODO_OWNED_FRAMES_ALLOCATOR_ERROR;
    }
    return DODO_OWNED_FRAMES_OK;
}
