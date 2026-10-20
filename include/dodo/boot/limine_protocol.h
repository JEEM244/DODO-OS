#ifndef DODO_BOOT_LIMINE_PROTOCOL_H
#define DODO_BOOT_LIMINE_PROTOCOL_H

#include <stdint.h>

/*
 * Minimal DODO-owned declarations for the Limine boot protocol.
 * These types describe the documented request/response ABI; they are not a
 * vendored implementation of the Limine bootloader.
 */

#define DODO_LIMINE_MEMMAP_USABLE UINT64_C(0)
#define DODO_LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE UINT64_C(5)

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

struct dodo_limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};

struct dodo_limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    struct dodo_limine_hhdm_response *response;
};

/* RGB framebuffer definitions from the documented Limine request ABI. */
#define DODO_LIMINE_FRAMEBUFFER_MEMORY_MODEL_RGB UINT8_C(1)

struct dodo_limine_video_mode;

struct dodo_limine_framebuffer {
    void *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t unused[7];
    uint64_t edid_size;
    void *edid;
    uint64_t mode_count;
    struct dodo_limine_video_mode **modes;
};

struct dodo_limine_framebuffer_response {
    uint64_t revision;
    uint64_t framebuffer_count;
    struct dodo_limine_framebuffer **framebuffers;
};

struct dodo_limine_framebuffer_request {
    uint64_t id[4];
    uint64_t revision;
    struct dodo_limine_framebuffer_response *response;
};

#endif
