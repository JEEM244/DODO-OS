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

enum dodo_owned_frames_status dodo_owned_frames_alloc(
    struct dodo_owned_frames *pool, uint32_t owner_id,
    enum dodo_frame_kind kind, uint64_t *physical_address) {
    if (pool == NULL || pool->allocator == NULL ||
        physical_address == NULL || owner_id == DODO_FRAME_OWNER_INVALID) {
        return DODO_OWNED_FRAMES_INVALID_ARGUMENT;
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
