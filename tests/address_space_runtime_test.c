#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "dodo/mm/address_space_runtime.h"

#define TEST_ROOT_CAPACITY 16u
#define TEST_MAPPING_CAPACITY 256u

struct test_root {
    uint64_t physical;
    uint32_t used;
    uint32_t has_tables;
};

struct test_mapping {
    uint64_t root;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t flags;
    uint32_t used;
};

static struct test_root roots[TEST_ROOT_CAPACITY];
static struct test_mapping mappings[TEST_MAPPING_CAPACITY];
static uint64_t active_root = UINT64_C(0x8000);
static const uint64_t kernel_root = UINT64_C(0x8000);
static uint64_t fail_switch_to;
static enum dodo_paging_status fail_map_status;
static uint64_t fail_create_root;

static struct test_root *find_root(uint64_t physical) {
    for (uint32_t i = 0u; i < TEST_ROOT_CAPACITY; ++i) {
        if (roots[i].used && roots[i].physical == physical) {
            return &roots[i];
        }
    }
    return NULL;
}

static struct test_mapping *find_mapping(uint64_t root, uint64_t address) {
    for (uint32_t i = 0u; i < TEST_MAPPING_CAPACITY; ++i) {
        if (mappings[i].used && mappings[i].root == root &&
            mappings[i].virtual_address == address) {
            return &mappings[i];
        }
    }
    return NULL;
}

uint64_t dodo_x86_64_active_root_physical(void) {
    return active_root;
}

uint64_t dodo_x86_64_kernel_root_physical(void) {
    return kernel_root;
}

enum dodo_paging_status dodo_x86_64_paging_create_user_root(uint64_t root) {
    if (root == 0u || root == active_root || root == kernel_root ||
        root == fail_create_root || (root & (DODO_PAGE_SIZE - 1u)) != 0u) {
        return DODO_PAGING_INVALID_ADDRESS;
    }
    struct test_root *entry = find_root(root);
    if (entry == NULL) {
        for (uint32_t i = 0u; i < TEST_ROOT_CAPACITY; ++i) {
            if (!roots[i].used) {
                entry = &roots[i];
                break;
            }
        }
    }
    if (entry == NULL) {
        return DODO_PAGING_NO_FREE_USER_SLOT;
    }
    *entry = (struct test_root){ .physical = root, .used = 1u, .has_tables = 0u };
    for (uint32_t i = 0u; i < TEST_MAPPING_CAPACITY; ++i) {
        if (mappings[i].used && mappings[i].root == root) {
            mappings[i].used = 0u;
        }
    }
    return DODO_PAGING_OK;
}

enum dodo_paging_status dodo_x86_64_paging_switch_root(uint64_t root) {
    if (root == 0u || (root & (DODO_PAGE_SIZE - 1u)) != 0u ||
        (root != kernel_root && find_root(root) == NULL) ||
        root == fail_switch_to) {
        return DODO_PAGING_INVALID_ADDRESS;
    }
    active_root = root;
    return DODO_PAGING_OK;
}

enum dodo_paging_status dodo_x86_64_map_page(
    uint64_t virtual_address, uint64_t physical_address, uint64_t flags,
    const uint64_t *table_frames, uint64_t table_frame_count,
    uint64_t *frames_used) {
    if (frames_used == NULL || active_root == kernel_root ||
        (table_frame_count != 0u && table_frames == NULL)) {
        return DODO_PAGING_INVALID_ARGUMENT;
    }
    *frames_used = 0u;
    if (fail_map_status != DODO_PAGING_OK) {
        const enum dodo_paging_status injected = fail_map_status;
        fail_map_status = DODO_PAGING_OK;
        return injected;
    }
    if (find_mapping(active_root, virtual_address) != NULL) {
        return DODO_PAGING_ALREADY_MAPPED;
    }
    struct test_root *root = find_root(active_root);
    if (root == NULL) {
        return DODO_PAGING_INVALID_ADDRESS;
    }
    if (!root->has_tables) {
        if (table_frame_count == 0u) {
            return DODO_PAGING_NO_TABLE_FRAMES;
        }
        root->has_tables = 1u;
        *frames_used = 1u;
    }
    for (uint32_t i = 0u; i < TEST_MAPPING_CAPACITY; ++i) {
        if (!mappings[i].used) {
            mappings[i] = (struct test_mapping){
                .root = active_root, .virtual_address = virtual_address,
                .physical_address = physical_address, .flags = flags, .used = 1u
            };
            return DODO_PAGING_OK;
        }
    }
    return DODO_PAGING_CLONE_CAPACITY_EXCEEDED;
}

enum dodo_paging_status dodo_x86_64_query_page(
    uint64_t virtual_address, uint64_t *physical_address, uint64_t *flags) {
    if (physical_address == NULL || flags == NULL || physical_address == flags) {
        return DODO_PAGING_INVALID_ARGUMENT;
    }
    struct test_mapping *mapping = find_mapping(active_root, virtual_address);
    if (mapping == NULL) {
        return DODO_PAGING_NOT_MAPPED;
    }
    *physical_address = mapping->physical_address;
    *flags = mapping->flags;
    return DODO_PAGING_OK;
}

enum dodo_paging_status dodo_x86_64_unmap_page(
    uint64_t virtual_address, uint64_t *physical_address, uint64_t *flags) {
    if (physical_address == NULL || flags == NULL || physical_address == flags) {
        return DODO_PAGING_INVALID_ARGUMENT;
    }
    struct test_mapping *mapping = find_mapping(active_root, virtual_address);
    if (mapping == NULL) {
        return DODO_PAGING_NOT_MAPPED;
    }
    *physical_address = mapping->physical_address;
    *flags = mapping->flags;
    mapping->used = 0u;
    return DODO_PAGING_OK;
}

static void make_map(struct dodo_limine_memmap_entry *usable,
                     struct dodo_limine_memmap_entry **entries,
                     struct dodo_limine_memmap_response *map) {
    usable->base = UINT64_C(0x1000);
    usable->length = UINT64_C(2048) * DODO_PHYS_PAGE_SIZE;
    usable->type = DODO_LIMINE_MEMMAP_USABLE;
    entries[0] = usable;
    map->revision = 0u;
    map->entry_count = 1u;
    map->entries = entries;
}

struct fixture {
    struct dodo_limine_memmap_entry usable;
    struct dodo_limine_memmap_entry *entries[1];
    struct dodo_limine_memmap_response map;
    struct dodo_phys_allocator allocator;
    struct dodo_owned_frames frames;
    struct dodo_address_space_runtime runtime;
};

static void fixture_init(struct fixture *f, uint32_t owner) {
    make_map(&f->usable, f->entries, &f->map);
    assert(dodo_phys_init(&f->allocator, &f->map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&f->frames, &f->allocator);
    assert(dodo_address_space_runtime_create(&f->runtime, &f->frames, owner,
        UINT64_C(0x400000), UINT64_C(0x800000)) ==
        DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_create_map_unmap_destroy_reclaims_all_owned_frames(void) {
    struct fixture f;
    fixture_init(&f, 101u);
    const uint64_t total_pages = UINT64_C(2048);
    const uint64_t flags = DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_WRITABLE |
                           DODO_PAGE_NO_EXECUTE;

    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x401000), flags) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(f.runtime.metadata.mapping_count == 1u);
    assert(f.runtime.table_frame_count == 1u);
    uint32_t owner_count = 0u;
    assert(dodo_owned_frames_count_owner(&f.frames, 101u, &owner_count) ==
           DODO_OWNED_FRAMES_OK);
    assert(owner_count == 3u);

    assert(dodo_address_space_runtime_unmap(&f.runtime, UINT64_C(0x401000)) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_unmap(&f.runtime, UINT64_C(0x401000)) ==
           DODO_ADDRESS_SPACE_RUNTIME_NOT_MAPPED);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(f.runtime.metadata.mapping_count == 0u);
    assert(dodo_owned_frames_count_owner(&f.frames, 101u, &owner_count) ==
           DODO_OWNED_FRAMES_OK);
    assert(owner_count == 2u);

    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x402000), flags) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(f.runtime.table_frame_count == 1u);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(f.runtime.initialized == 0u);
    assert(dodo_owned_frames_count_owner(&f.frames, 101u, &owner_count) ==
           DODO_OWNED_FRAMES_OK);
    assert(owner_count == 0u);
    assert(f.allocator.free_pages == total_pages);
    assert(f.allocator.allocated_pages == 0u);
}

static void test_invalid_owner_id_allocates_nothing(void) {
    struct fixture f;
    make_map(&f.usable, f.entries, &f.map);
    assert(dodo_phys_init(&f.allocator, &f.map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&f.frames, &f.allocator);
    const uint64_t free_before = f.allocator.free_pages;
    assert(dodo_address_space_runtime_create(&f.runtime, &f.frames,
        DODO_FRAME_OWNER_INVALID, UINT64_C(0x400000),
        UINT64_C(0x800000)) == DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT);
    assert(f.allocator.free_pages == free_before);
    assert(f.allocator.allocated_pages == 0u);
}

static void test_invalid_runtime_range_allocates_nothing(void) {
    struct fixture f;
    make_map(&f.usable, f.entries, &f.map);
    assert(dodo_phys_init(&f.allocator, &f.map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&f.frames, &f.allocator);
    const uint64_t free_before = f.allocator.free_pages;
    assert(dodo_address_space_runtime_create(&f.runtime, &f.frames, 114u,
        UINT64_C(0x1000), UINT64_C(0x800000)) ==
        DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT);
    assert(f.allocator.free_pages == free_before);
    assert(f.allocator.allocated_pages == 0u);
}

static void test_owner_id_cannot_be_reused_for_live_runtime(void) {
    struct fixture f;
    fixture_init(&f, 113u);
    struct dodo_address_space_runtime second_runtime;
    assert(dodo_address_space_runtime_create(&second_runtime, &f.frames, 113u,
        UINT64_C(0x400000), UINT64_C(0x800000)) ==
        DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_user_root_creation_failure_releases_root_frame(void) {
    struct fixture f;
    make_map(&f.usable, f.entries, &f.map);
    assert(dodo_phys_init(&f.allocator, &f.map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&f.frames, &f.allocator);
    const uint64_t free_before = f.allocator.free_pages;
    fail_create_root = UINT64_C(0x1000);
    assert(dodo_address_space_runtime_create(&f.runtime, &f.frames, 110u,
        UINT64_C(0x400000), UINT64_C(0x800000)) ==
        DODO_ADDRESS_SPACE_RUNTIME_PAGING_ERROR);
    fail_create_root = 0u;
    uint32_t count = UINT32_MAX;
    assert(dodo_owned_frames_count_owner(&f.frames, 110u, &count) ==
           DODO_OWNED_FRAMES_OK);
    assert(count == 0u);
    assert(f.allocator.free_pages == free_before);
    assert(f.allocator.allocated_pages == 0u);
}

static void test_restore_switch_failure_keeps_mapping_owned(void) {
    struct fixture f;
    fixture_init(&f, 111u);
    fail_switch_to = kernel_root;
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x401000),
        DODO_PAGE_USER_ACCESSIBLE) ==
        DODO_ADDRESS_SPACE_RUNTIME_ROOT_SWITCH_ERROR);
    assert(active_root == f.runtime.root_physical);
    fail_switch_to = 0u;
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_x86_64_paging_switch_root(kernel_root) == DODO_PAGING_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_failed_root_switch_rolls_back_mapping_allocation(void) {
    struct fixture f;
    fixture_init(&f, 102u);
    uint32_t before = 0u;
    assert(dodo_owned_frames_count_owner(&f.frames, 102u, &before) ==
           DODO_OWNED_FRAMES_OK);
    fail_switch_to = f.runtime.root_physical;
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x401000),
        DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE) ==
        DODO_ADDRESS_SPACE_RUNTIME_ROOT_SWITCH_ERROR);
    fail_switch_to = 0u;
    uint32_t after = 0u;
    assert(dodo_owned_frames_count_owner(&f.frames, 102u, &after) ==
           DODO_OWNED_FRAMES_OK);
    assert(after == before);
    assert(f.runtime.metadata.mapping_count == 0u);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_unmap_restore_failure_keeps_metadata_consistent(void) {
    struct fixture f;
    fixture_init(&f, 112u);
    const uint64_t address = UINT64_C(0x401000);
    assert(dodo_address_space_runtime_map(&f.runtime, address,
        DODO_PAGE_USER_ACCESSIBLE) == DODO_ADDRESS_SPACE_RUNTIME_OK);
    fail_switch_to = kernel_root;
    assert(dodo_address_space_runtime_unmap(&f.runtime, address) ==
           DODO_ADDRESS_SPACE_RUNTIME_ROOT_SWITCH_ERROR);
    assert(active_root == f.runtime.root_physical);
    assert(f.runtime.metadata.mapping_count == 0u);
    fail_switch_to = 0u;
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_x86_64_paging_switch_root(kernel_root) == DODO_PAGING_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_duplicate_and_invalid_mapping_do_not_leak(void) {
    struct fixture f;
    fixture_init(&f, 103u);
    const uint64_t flags = DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_NO_EXECUTE;
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x401000), flags) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    uint32_t before = 0u;
    uint32_t after = 0u;
    assert(dodo_owned_frames_count_owner(&f.frames, 103u, &before) ==
           DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x401000), flags) ==
           DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT);
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x402000),
        DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_WRITABLE) ==
        DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT);
    assert(dodo_owned_frames_count_owner(&f.frames, 103u, &after) ==
           DODO_OWNED_FRAMES_OK);
    assert(after == before);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_low_level_mapping_failure_rolls_back_owned_frames(void) {
    struct fixture f;
    fixture_init(&f, 107u);
    uint32_t before = 0u;
    assert(dodo_owned_frames_count_owner(&f.frames, 107u, &before) ==
           DODO_OWNED_FRAMES_OK);
    fail_map_status = DODO_PAGING_NO_TABLE_FRAMES;
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x401000),
        DODO_PAGE_USER_ACCESSIBLE) == DODO_ADDRESS_SPACE_RUNTIME_PAGING_ERROR);
    uint32_t after = UINT32_MAX;
    assert(dodo_owned_frames_count_owner(&f.frames, 107u, &after) ==
           DODO_OWNED_FRAMES_OK);
    assert(after == before);
    assert(f.runtime.metadata.mapping_count == 0u);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_hardware_metadata_mismatch_fails_closed(void) {
    struct fixture f;
    fixture_init(&f, 105u);
    const uint64_t address = UINT64_C(0x401000);
    assert(dodo_address_space_runtime_map(&f.runtime, address,
        DODO_PAGE_USER_ACCESSIBLE) == DODO_ADDRESS_SPACE_RUNTIME_OK);
    struct test_mapping *mapping = find_mapping(f.runtime.root_physical, address);
    assert(mapping != NULL);
    const uint64_t expected_physical = mapping->physical_address;
    mapping->physical_address += DODO_PAGE_SIZE;
    assert(dodo_address_space_runtime_unmap(&f.runtime, address) ==
           DODO_ADDRESS_SPACE_RUNTIME_PAGING_ERROR);
    assert(f.runtime.metadata.mapping_count == 1u);
    mapping->physical_address = expected_physical;
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_mapping_capacity_is_bounded_without_leaks(void) {
    struct fixture f;
    fixture_init(&f, 106u);
    const uint64_t flags = DODO_PAGE_USER_ACCESSIBLE;
    for (uint32_t i = 0u; i < DODO_ADDRESS_SPACE_MAX_MAPPINGS; ++i) {
        const uint64_t address = UINT64_C(0x400000) +
            (uint64_t)i * DODO_PAGE_SIZE;
        assert(dodo_address_space_runtime_map(&f.runtime, address, flags) ==
               DODO_ADDRESS_SPACE_RUNTIME_OK);
    }
    assert(f.runtime.metadata.mapping_count == DODO_ADDRESS_SPACE_MAX_MAPPINGS);
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x700000), flags) ==
           DODO_ADDRESS_SPACE_RUNTIME_CAPACITY);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    uint32_t count = UINT32_MAX;
    assert(dodo_owned_frames_count_owner(&f.frames, 106u, &count) ==
           DODO_OWNED_FRAMES_OK);
    assert(count == 0u);
    assert(f.allocator.allocated_pages == 0u);
}

static void test_runtime_validator_checks_root_frame_kind(void) {
    struct fixture f;
    fixture_init(&f, 117u);
    const uint64_t root = f.runtime.root_physical;
    enum dodo_frame_kind original_kind = DODO_FRAME_KIND_UNUSED;
    assert(dodo_owned_frames_get_kind(&f.frames, 117u, root, &original_kind) ==
           DODO_OWNED_FRAMES_OK);
    for (uint32_t i = 0u; i < DODO_FRAME_LEDGER_CAPACITY; ++i) {
        if (f.frames.ledger.records[i].physical_address == root) {
            f.frames.ledger.records[i].kind = DODO_FRAME_KIND_PAGE_TABLE;
            break;
        }
    }
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR);
    for (uint32_t i = 0u; i < DODO_FRAME_LEDGER_CAPACITY; ++i) {
        if (f.frames.ledger.records[i].physical_address == root) {
            f.frames.ledger.records[i].kind = original_kind;
            break;
        }
    }
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_runtime_validator_checks_table_frame_kind(void) {
    struct fixture f;
    fixture_init(&f, 116u);
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x401000),
        DODO_PAGE_USER_ACCESSIBLE) == DODO_ADDRESS_SPACE_RUNTIME_OK);
    const uint64_t table_frame = f.runtime.table_frames[0];
    enum dodo_frame_kind original_kind = DODO_FRAME_KIND_UNUSED;
    assert(dodo_owned_frames_get_kind(&f.frames, 116u, table_frame,
        &original_kind) == DODO_OWNED_FRAMES_OK);
    for (uint32_t i = 0u; i < DODO_FRAME_LEDGER_CAPACITY; ++i) {
        if (f.frames.ledger.records[i].physical_address == table_frame) {
            f.frames.ledger.records[i].kind = DODO_FRAME_KIND_USER_DATA;
            break;
        }
    }
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR);
    for (uint32_t i = 0u; i < DODO_FRAME_LEDGER_CAPACITY; ++i) {
        if (f.frames.ledger.records[i].physical_address == table_frame) {
            f.frames.ledger.records[i].kind = original_kind;
            break;
        }
    }
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_runtime_validator_rejects_stale_unused_table_slot(void) {
    struct fixture f;
    fixture_init(&f, 115u);
    f.runtime.table_frames[0] = UINT64_C(0x123000);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE);
    f.runtime.table_frames[0] = 0u;
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_runtime_rejects_borrowed_mapping_metadata(void) {
    struct fixture f;
    fixture_init(&f, 108u);
    assert(dodo_address_space_record_mapped_page(&f.runtime.metadata,
        UINT64_C(0x401000), UINT64_C(0x2000),
        DODO_PAGE_USER_ACCESSIBLE,
        DODO_ADDRESS_SPACE_BORROWED_FRAME) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE);
    assert(dodo_address_space_forget_unmapped_page(&f.runtime.metadata,
        UINT64_C(0x401000)) == DODO_ADDRESS_SPACE_OK);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_invalid_virtual_range_does_not_leak(void) {
    struct fixture f;
    fixture_init(&f, 109u);
    uint32_t before = 0u;
    uint32_t after = UINT32_MAX;
    assert(dodo_owned_frames_count_owner(&f.frames, 109u, &before) ==
           DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x1000),
        DODO_PAGE_USER_ACCESSIBLE) == DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT);
    assert(dodo_owned_frames_count_owner(&f.frames, 109u, &after) ==
           DODO_OWNED_FRAMES_OK);
    assert(after == before);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_destroy_refuses_active_root(void) {
    struct fixture f;
    fixture_init(&f, 104u);
    assert(dodo_x86_64_paging_switch_root(f.runtime.root_physical) == DODO_PAGING_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE);
    assert(dodo_x86_64_paging_switch_root(kernel_root) == DODO_PAGING_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

int main(void) {
    test_create_map_unmap_destroy_reclaims_all_owned_frames();
    test_invalid_owner_id_allocates_nothing();
    test_invalid_runtime_range_allocates_nothing();
    test_owner_id_cannot_be_reused_for_live_runtime();
    test_user_root_creation_failure_releases_root_frame();
    test_failed_root_switch_rolls_back_mapping_allocation();
    test_restore_switch_failure_keeps_mapping_owned();
    test_unmap_restore_failure_keeps_metadata_consistent();
    test_duplicate_and_invalid_mapping_do_not_leak();
    test_low_level_mapping_failure_rolls_back_owned_frames();
    test_hardware_metadata_mismatch_fails_closed();
    test_mapping_capacity_is_bounded_without_leaks();
    test_runtime_validator_checks_root_frame_kind();
    test_runtime_validator_checks_table_frame_kind();
    test_runtime_validator_rejects_stale_unused_table_slot();
    test_runtime_rejects_borrowed_mapping_metadata();
    test_invalid_virtual_range_does_not_leak();
    test_destroy_refuses_active_root();
    puts("address-space runtime tests: ok");
    return 0;
}
