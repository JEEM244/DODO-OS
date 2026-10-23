#ifndef DODO_MM_ADDRESS_SPACE_RUNTIME_H
#define DODO_MM_ADDRESS_SPACE_RUNTIME_H

#include <stdint.h>
#include "dodo/mm/address_space.h"

#define DODO_ADDRESS_SPACE_RUNTIME_MAX_TABLE_FRAMES DODO_FRAME_LEDGER_CAPACITY

enum dodo_address_space_runtime_status {
    DODO_ADDRESS_SPACE_RUNTIME_OK = 0,
    DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT,
    DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE,
    DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR,
    DODO_ADDRESS_SPACE_RUNTIME_PAGING_ERROR,
    DODO_ADDRESS_SPACE_RUNTIME_CAPACITY,
    DODO_ADDRESS_SPACE_RUNTIME_NOT_MAPPED,
    DODO_ADDRESS_SPACE_RUNTIME_ROOT_SWITCH_ERROR
};

/*
 * Single-CPU runtime wrapper joining address-space metadata, frame ownership,
 * and the active x86-64 mapper. Intermediate page-table frames are retained
 * until destroy because the low-level unmapper does not prune empty tables.
 * The caller supplies fresh storage; owner_id must be unique and own no frames
 * before create. Calls are not reentrant or SMP-safe.
 */
struct dodo_address_space_runtime {
    struct dodo_address_space metadata;
    uint64_t root_physical;
    uint32_t table_frame_count;
    uint32_t initialized;
    uint64_t table_frames[DODO_ADDRESS_SPACE_RUNTIME_MAX_TABLE_FRAMES];
};

enum dodo_address_space_runtime_status dodo_address_space_runtime_create(
    struct dodo_address_space_runtime *runtime,
    struct dodo_owned_frames *frames,
    uint32_t owner_id,
    uint64_t virtual_start,
    uint64_t virtual_end);

enum dodo_address_space_runtime_status dodo_address_space_runtime_validate(
    const struct dodo_address_space_runtime *runtime);

/* Validate and activate this address space by loading its root into CR3. */
enum dodo_address_space_runtime_status dodo_address_space_runtime_activate(
    struct dodo_address_space_runtime *runtime);

/* Switch an active user root back to the kernel root. */
enum dodo_address_space_runtime_status dodo_address_space_runtime_deactivate(
    struct dodo_address_space_runtime *runtime,
    uint64_t fallback_root_physical);

/* Allocate and map one owned user-data frame. Writable mappings must be NX. */
enum dodo_address_space_runtime_status dodo_address_space_runtime_map(
    struct dodo_address_space_runtime *runtime,
    uint64_t virtual_address,
    uint64_t flags);

/* Unmap one page and release its owned data frame after hardware removal. */
enum dodo_address_space_runtime_status dodo_address_space_runtime_unmap(
    struct dodo_address_space_runtime *runtime,
    uint64_t virtual_address);

/*
 * Destroy only while this root is inactive. All mappings are unmapped first;
 * owned user data, retained intermediate tables, and root are then released.
 */
enum dodo_address_space_runtime_status dodo_address_space_runtime_destroy(
    struct dodo_address_space_runtime *runtime);

#endif
