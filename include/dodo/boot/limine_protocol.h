#ifndef DODO_BOOT_LIMINE_PROTOCOL_H
#define DODO_BOOT_LIMINE_PROTOCOL_H

#include <stdint.h>

/*
 * Minimal DODO-owned declarations for the Limine boot protocol.
 * These types describe the documented wire/ABI layout; they are not a
 * vendored implementation of the Limine bootloader.
 */

#define DODO_LIMINE_MEMMAP_USABLE UINT64_C(0)

struct dodo_limine_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct dodo_limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    struct dodo_limine_memmap_entry **entries;
};

struct dodo_limine_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    struct dodo_limine_memmap_response *response;
};

#endif
