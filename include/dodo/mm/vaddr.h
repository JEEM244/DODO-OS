#ifndef DODO_MM_VADDR_H
#define DODO_MM_VADDR_H

#include <stdint.h>

#define DODO_VADDR_PAGE_SIZE UINT64_C(4096)
#define DODO_VADDR_MAX_RANGES 64u

enum dodo_vaddr_status {
    DODO_VADDR_OK = 0,
    DODO_VADDR_INVALID_ARGUMENT = 1,
    DODO_VADDR_INVALID_RANGE = 2,
    DODO_VADDR_NO_SPACE = 3,
    DODO_VADDR_CAPACITY = 4,
    DODO_VADDR_OVERLAP = 5,
    DODO_VADDR_NOT_FOUND = 6
};

struct dodo_vaddr_range {
    uint64_t base;
    uint64_t length;
};

/*
 * Bounded metadata for a canonical low-half interval. This structure tracks
 * reservations only: it does not create page mappings or own physical frames.
 * Callers must initialize it before any other operation and synchronize it
 * with mapping/unmapping themselves.
 */
struct dodo_vaddr_space {
    uint64_t start;
    uint64_t end; /* Exclusive upper bound. */
    uint64_t count;
    struct dodo_vaddr_range allocated[DODO_VADDR_MAX_RANGES];
};

/* Validate all bounds, reservations, ordering, and non-overlap invariants. */
enum dodo_vaddr_status dodo_vaddr_validate(
    const struct dodo_vaddr_space *space);

/* Initialize an aligned, non-empty interval wholly below the user-half limit. */
enum dodo_vaddr_status dodo_vaddr_init(
    struct dodo_vaddr_space *space, uint64_t start, uint64_t end);

/* Reserve an exact page-aligned range, rejecting overlap and capacity overflow. */
enum dodo_vaddr_status dodo_vaddr_reserve(
    struct dodo_vaddr_space *space, uint64_t base, uint64_t length);

/*
 * Allocate the first suitable free range. Alignment must be a power of two
 * and at least one page. On failure, *base is left unchanged.
 */
enum dodo_vaddr_status dodo_vaddr_allocate(
    struct dodo_vaddr_space *space, uint64_t length, uint64_t alignment,
    uint64_t *base);

/* Release only an exact reservation; partial releases are not supported. */
enum dodo_vaddr_status dodo_vaddr_release(
    struct dodo_vaddr_space *space, uint64_t base, uint64_t length);

#endif
