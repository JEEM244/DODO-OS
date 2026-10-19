#ifndef DODO_MM_VADDR_H
#define DODO_MM_VADDR_H
#include <stdint.h>
#define DODO_VADDR_PAGE_SIZE UINT64_C(4096)
#define DODO_VADDR_MAX_RANGES 64u
enum dodo_vaddr_status { DODO_VADDR_OK=0, DODO_VADDR_INVALID_ARGUMENT=1, DODO_VADDR_INVALID_RANGE=2, DODO_VADDR_NO_SPACE=3, DODO_VADDR_CAPACITY=4, DODO_VADDR_OVERLAP=5, DODO_VADDR_NOT_FOUND=6 };
struct dodo_vaddr_range { uint64_t base; uint64_t length; };
/* Metadata-only low-half allocator; it does not map pages or own page tables. */
struct dodo_vaddr_space { uint64_t start; uint64_t end; uint64_t count; struct dodo_vaddr_range allocated[DODO_VADDR_MAX_RANGES]; };
enum dodo_vaddr_status dodo_vaddr_init(struct dodo_vaddr_space *space, uint64_t start, uint64_t end);
enum dodo_vaddr_status dodo_vaddr_reserve(struct dodo_vaddr_space *space, uint64_t base, uint64_t length);
enum dodo_vaddr_status dodo_vaddr_allocate(struct dodo_vaddr_space *space, uint64_t length, uint64_t alignment, uint64_t *base);
enum dodo_vaddr_status dodo_vaddr_release(struct dodo_vaddr_space *space, uint64_t base, uint64_t length);
#endif
