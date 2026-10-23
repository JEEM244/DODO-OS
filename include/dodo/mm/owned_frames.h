#ifndef DODO_MM_OWNED_FRAMES_H
#define DODO_MM_OWNED_FRAMES_H

#include <stdint.h>
#include "dodo/mm/frame_ledger.h"
#include "dodo/mm/phys.h"

enum dodo_owned_frames_status {
    DODO_OWNED_FRAMES_OK = 0,
    DODO_OWNED_FRAMES_INVALID_ARGUMENT,
    DODO_OWNED_FRAMES_ALLOCATOR_ERROR,
    DODO_OWNED_FRAMES_LEDGER_ERROR,
    DODO_OWNED_FRAMES_ROLLBACK_ERROR
};

/*
 * Single-CPU bootstrap ownership adapter. A successful allocation is recorded
 * in both the physical allocator and the owner ledger. Not SMP-safe.
 */
struct dodo_owned_frames {
    struct dodo_phys_allocator *allocator;
    struct dodo_frame_ledger ledger;
};

void dodo_owned_frames_init(struct dodo_owned_frames *pool,
                            struct dodo_phys_allocator *allocator);

/* Verify ledger integrity and that every recorded frame is still allocated. */
enum dodo_owned_frames_status dodo_owned_frames_validate(
    const struct dodo_owned_frames *pool);

/* Count a task/address-space owner's live frames after consistency checks. */
enum dodo_owned_frames_status dodo_owned_frames_count_owner(
    const struct dodo_owned_frames *pool, uint32_t owner_id,
    uint32_t *count_out);

enum dodo_owned_frames_status dodo_owned_frames_alloc(
    struct dodo_owned_frames *pool, uint32_t owner_id,
    enum dodo_frame_kind kind, uint64_t *physical_address);

enum dodo_owned_frames_status dodo_owned_frames_release(
    struct dodo_owned_frames *pool, uint32_t owner_id,
    uint64_t physical_address);

#endif
