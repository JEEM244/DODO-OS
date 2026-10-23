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

static void test_create_allocates_exactly_one_root_frame(void) {
    struct fixture f;
    fixture_init(&f, 125u);
    uint32_t count = UINT32_MAX;
    enum dodo_frame_kind kind = DODO_FRAME_KIND_UNUSED;
    assert(dodo_owned_frames_count_owner(&f.frames, 125u, &count) ==
           DODO_OWNED_FRAMES_OK);
    assert(count == 1u);
    assert(dodo_owned_frames_get_kind(&f.frames, 125u,
        f.runtime.root_physical, &kind) == DODO_OWNED_FRAMES_OK);
    assert(kind == DODO_FRAME_KIND_ROOT);
    assert(f.runtime.table_frame_count == 0u);
    assert(f.runtime.metadata.mapping_count == 0u);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
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
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x401000),
        DODO_PAGE_USER_ACCESSIBLE) == DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE);

    /* Reusing the same object is safe after a complete destroy. */
    assert(dodo_address_space_runtime_create(&f.runtime, &f.frames, 101u,
        UINT64_C(0x400000), UINT64_C(0x800000)) ==
        DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(f.allocator.free_pages == total_pages);
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
        UINT64_C(0x0), UINT64_C(0x800000)) ==
        DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT);
    assert(f.allocator.free_pages == free_before);
    assert(f.allocator.allocated_pages == 0u);
}

static void test_distinct_owners_can_have_independent_roots(void) {
    struct fixture f;
    fixture_init(&f, 126u);
    struct dodo_address_space_runtime second;
    assert(dodo_address_space_runtime_create(&second, &f.frames, 127u,
        UINT64_C(0x900000), UINT64_C(0xA00000)) ==
        DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(second.root_physical != f.runtime.root_physical);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_validate(&second) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&second) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(f.allocator.allocated_pages == 0u);
}

static void test_owner_id_cannot_be_reused_for_live_runtime(void) {
    struct fixture f;
    fixture_init(&f, 113u);
    struct dodo_address_space_runtime second_runtime;
    uint32_t before = UINT32_MAX;
    uint32_t after = UINT32_MAX;
    assert(dodo_owned_frames_count_owner(&f.frames, 113u, &before) ==
           DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_runtime_create(&second_runtime, &f.frames, 113u,
        UINT64_C(0x400000), UINT64_C(0x800000)) ==
        DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR);
    assert(dodo_owned_frames_count_owner(&f.frames, 113u, &after) ==
           DODO_OWNED_FRAMES_OK);
    assert(after == before);
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

static void test_partial_table_frame_allocation_is_rolled_back(void) {
    struct fixture f;
    f.usable.base = UINT64_C(0x1000);
    f.usable.length = UINT64_C(3) * DODO_PHYS_PAGE_SIZE;
    f.usable.type = DODO_LIMINE_MEMMAP_USABLE;
    f.entries[0] = &f.usable;
    f.map.revision = 0u;
    f.map.entry_count = 1u;
    f.map.entries = f.entries;
    assert(dodo_phys_init(&f.allocator, &f.map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&f.frames, &f.allocator);
    assert(dodo_address_space_runtime_create(&f.runtime, &f.frames, 132u,
        UINT64_C(0x400000), UINT64_C(0x800000)) ==
        DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x401000),
        DODO_PAGE_USER_ACCESSIBLE) ==
        DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR);
    assert(f.runtime.metadata.mapping_count == 0u);
    assert(f.runtime.table_frame_count == 0u);
    uint32_t count = UINT32_MAX;
    assert(dodo_owned_frames_count_owner(&f.frames, 132u, &count) ==
           DODO_OWNED_FRAMES_OK);
    assert(count == 1u);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(f.allocator.allocated_pages == 0u);
}

static void test_table_frame_exhaustion_rolls_back_user_data(void) {
    struct fixture f;
    f.usable.base = UINT64_C(0x1000);
    f.usable.length = UINT64_C(2) * DODO_PHYS_PAGE_SIZE;
    f.usable.type = DODO_LIMINE_MEMMAP_USABLE;
    f.entries[0] = &f.usable;
    f.map.revision = 0u;
    f.map.entry_count = 1u;
    f.map.entries = f.entries;
    assert(dodo_phys_init(&f.allocator, &f.map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&f.frames, &f.allocator);
    assert(dodo_address_space_runtime_create(&f.runtime, &f.frames, 131u,
        UINT64_C(0x400000), UINT64_C(0x800000)) ==
        DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x401000),
        DODO_PAGE_USER_ACCESSIBLE) ==
        DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR);
    assert(f.runtime.metadata.mapping_count == 0u);
    assert(f.runtime.table_frame_count == 0u);
    uint32_t count = UINT32_MAX;
    assert(dodo_owned_frames_count_owner(&f.frames, 131u, &count) ==
           DODO_OWNED_FRAMES_OK);
    assert(count == 1u);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(f.allocator.allocated_pages == 0u);
}

static void test_mapping_allocation_failure_leaves_runtime_unchanged(void) {
    struct fixture f;
    f.usable.base = UINT64_C(0x1000);
    f.usable.length = DODO_PHYS_PAGE_SIZE;
    f.usable.type = DODO_LIMINE_MEMMAP_USABLE;
    f.entries[0] = &f.usable;
    f.map.revision = 0u;
    f.map.entry_count = 1u;
    f.map.entries = f.entries;
    assert(dodo_phys_init(&f.allocator, &f.map) == DODO_PHYS_OK);
    dodo_owned_frames_init(&f.frames, &f.allocator);
    assert(dodo_address_space_runtime_create(&f.runtime, &f.frames, 130u,
        UINT64_C(0x400000), UINT64_C(0x800000)) ==
        DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(f.allocator.allocated_pages == 1u);
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x401000),
        DODO_PAGE_USER_ACCESSIBLE) ==
        DODO_ADDRESS_SPACE_RUNTIME_OWNERSHIP_ERROR);
    assert(f.runtime.metadata.mapping_count == 0u);
    assert(f.runtime.table_frame_count == 0u);
    uint32_t count = UINT32_MAX;
    assert(dodo_owned_frames_count_owner(&f.frames, 130u, &count) ==
           DODO_OWNED_FRAMES_OK);
    assert(count == 1u);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(f.allocator.allocated_pages == 0u);
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

static void test_read_only_executable_mapping_is_accepted(void) {
    struct fixture f;
    fixture_init(&f, 121u);
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x401000),
        DODO_PAGE_USER_ACCESSIBLE) == DODO_ADDRESS_SPACE_RUNTIME_OK);
    struct dodo_address_space_mapping mapping;
    assert(dodo_address_space_lookup(&f.runtime.metadata, UINT64_C(0x401000),
        &mapping) == DODO_ADDRESS_SPACE_OK);
    assert(mapping.flags == DODO_PAGE_USER_ACCESSIBLE);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
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
           DODO_ADDRESS_SPACE_RUNTIME_ALREADY_MAPPED);
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

static void test_destroy_refuses_corrupted_hardware_mapping(void) {
    struct fixture f;
    fixture_init(&f, 136u);
    const uint64_t address = UINT64_C(0x401000);
    assert(dodo_address_space_runtime_map(&f.runtime, address,
        DODO_PAGE_USER_ACCESSIBLE) == DODO_ADDRESS_SPACE_RUNTIME_OK);
    struct test_mapping *mapping = find_mapping(f.runtime.root_physical, address);
    assert(mapping != NULL);
    const uint64_t original = mapping->physical_address;
    mapping->physical_address += DODO_PAGE_SIZE;
    uint32_t owner_count = 0u;
    assert(dodo_owned_frames_count_owner(&f.frames, 136u, &owner_count) ==
           DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_PAGING_ERROR);
    assert(f.runtime.initialized == 1u);
    uint32_t after_count = 0u;
    assert(dodo_owned_frames_count_owner(&f.frames, 136u, &after_count) ==
           DODO_OWNED_FRAMES_OK);
    assert(after_count == owner_count);
    mapping->physical_address = original;
    assert(dodo_x86_64_paging_switch_root(kernel_root) == DODO_PAGING_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_unmap_fails_closed_when_hardware_leaf_is_missing(void) {
    struct fixture f;
    fixture_init(&f, 135u);
    const uint64_t address = UINT64_C(0x401000);
    assert(dodo_address_space_runtime_map(&f.runtime, address,
        DODO_PAGE_USER_ACCESSIBLE) == DODO_ADDRESS_SPACE_RUNTIME_OK);
    struct test_mapping *mapping = find_mapping(f.runtime.root_physical, address);
    assert(mapping != NULL && mapping->used != 0u);
    mapping->used = 0u;
    assert(dodo_address_space_runtime_unmap(&f.runtime, address) ==
           DODO_ADDRESS_SPACE_RUNTIME_PAGING_ERROR);
    assert(f.runtime.metadata.mapping_count == 1u);
    mapping->used = 1u;
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
    const uint64_t expected_flags = mapping->flags;
    mapping->flags ^= DODO_PAGE_NO_EXECUTE;
    assert(dodo_address_space_runtime_unmap(&f.runtime, address) ==
           DODO_ADDRESS_SPACE_RUNTIME_PAGING_ERROR);
    mapping->flags = expected_flags;
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

static void test_null_page_mapping_is_rejected_without_allocation(void) {
    struct fixture f;
    fixture_init(&f, 122u);
    uint32_t before = 0u;
    uint32_t after = UINT32_MAX;
    assert(dodo_owned_frames_count_owner(&f.frames, 122u, &before) ==
           DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_runtime_map(&f.runtime, 0u,
        DODO_PAGE_USER_ACCESSIBLE) ==
        DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT);
    assert(dodo_address_space_runtime_map(&f.runtime, DODO_PAGE_SIZE,
        DODO_PAGE_USER_ACCESSIBLE) ==
        DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT);
    assert(dodo_owned_frames_count_owner(&f.frames, 122u, &after) ==
           DODO_OWNED_FRAMES_OK);
    assert(after == before);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_noncanonical_mapping_address_is_rejected(void) {
    struct fixture f;
    fixture_init(&f, 123u);
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_MAX,
        DODO_PAGE_USER_ACCESSIBLE) ==
        DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_user_half_upper_boundary_is_exclusive(void) {
    struct fixture f;
    fixture_init(&f, 124u);
    assert(dodo_address_space_runtime_map(&f.runtime,
        DODO_X86_64_USER_ADDRESS_LIMIT,
        DODO_PAGE_USER_ACCESSIBLE) ==
        DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT);
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_unaligned_mapping_address_allocates_nothing(void) {
    struct fixture f;
    fixture_init(&f, 119u);
    uint32_t before = 0u;
    uint32_t after = UINT32_MAX;
    assert(dodo_owned_frames_count_owner(&f.frames, 119u, &before) ==
           DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x401001),
        DODO_PAGE_USER_ACCESSIBLE) ==
        DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT);
    assert(dodo_owned_frames_count_owner(&f.frames, 119u, &after) ==
           DODO_OWNED_FRAMES_OK);
    assert(after == before);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_out_of_range_mapping_address_allocates_nothing(void) {
    struct fixture f;
    fixture_init(&f, 120u);
    uint32_t before = 0u;
    uint32_t after = UINT32_MAX;
    assert(dodo_owned_frames_count_owner(&f.frames, 120u, &before) ==
           DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x800000),
        DODO_PAGE_USER_ACCESSIBLE) ==
        DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT);
    assert(dodo_owned_frames_count_owner(&f.frames, 120u, &after) ==
           DODO_OWNED_FRAMES_OK);
    assert(after == before);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_invalid_mapping_flags_allocate_nothing(void) {
    struct fixture f;
    fixture_init(&f, 118u);
    uint32_t before = 0u;
    uint32_t after = UINT32_MAX;
    assert(dodo_owned_frames_count_owner(&f.frames, 118u, &before) ==
           DODO_OWNED_FRAMES_OK);
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x401000),
        DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_WRITABLE) ==
        DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT);
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x401000),
        DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_GLOBAL) ==
        DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT);
    assert(dodo_address_space_runtime_map(&f.runtime, UINT64_C(0x401000),
        DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_CACHE_DISABLE) ==
        DODO_ADDRESS_SPACE_RUNTIME_INVALID_ARGUMENT);
    assert(dodo_owned_frames_count_owner(&f.frames, 118u, &after) ==
           DODO_OWNED_FRAMES_OK);
    assert(after == before);
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

static void test_runtime_activation_validates_and_switches_root(void) {
    struct fixture f;
    fixture_init(&f, 128u);
    assert(active_root == kernel_root);
    assert(dodo_address_space_runtime_activate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(active_root == f.runtime.root_physical);
    assert(dodo_address_space_runtime_deactivate(&f.runtime,
        f.runtime.root_physical) == DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE);
    assert(dodo_address_space_runtime_deactivate(&f.runtime,
        UINT64_C(0x9000)) == DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE);
    assert(active_root == f.runtime.root_physical);
    f.runtime.table_frame_count = DODO_ADDRESS_SPACE_RUNTIME_MAX_TABLE_FRAMES + 1u;
    assert(dodo_address_space_runtime_deactivate(&f.runtime, kernel_root) ==
           DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE);
    assert(active_root == f.runtime.root_physical);
    f.runtime.table_frame_count = 0u;
    assert(dodo_address_space_runtime_deactivate(&f.runtime, kernel_root) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(active_root == kernel_root);
    assert(dodo_address_space_runtime_deactivate(&f.runtime, kernel_root) ==
           DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);

    fixture_init(&f, 129u);
    assert(dodo_address_space_runtime_activate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE);
    assert(dodo_x86_64_paging_switch_root(kernel_root) == DODO_PAGING_OK);
    f.runtime.table_frame_count = DODO_ADDRESS_SPACE_RUNTIME_MAX_TABLE_FRAMES + 1u;
    assert(dodo_address_space_runtime_activate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_INVALID_STATE);
    assert(active_root == kernel_root);
    f.runtime.table_frame_count = 0u;
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_deactivation_switch_failure_preserves_user_root(void) {
    struct fixture f;
    fixture_init(&f, 134u);
    assert(dodo_address_space_runtime_activate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    fail_switch_to = kernel_root;
    assert(dodo_address_space_runtime_deactivate(&f.runtime, kernel_root) ==
           DODO_ADDRESS_SPACE_RUNTIME_ROOT_SWITCH_ERROR);
    assert(active_root == f.runtime.root_physical);
    fail_switch_to = 0u;
    assert(dodo_address_space_runtime_deactivate(&f.runtime, kernel_root) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
    assert(active_root == kernel_root);
    assert(dodo_address_space_runtime_destroy(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
}

static void test_activation_switch_failure_preserves_current_root(void) {
    struct fixture f;
    fixture_init(&f, 133u);
    fail_switch_to = f.runtime.root_physical;
    assert(dodo_address_space_runtime_activate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_ROOT_SWITCH_ERROR);
    assert(active_root == kernel_root);
    fail_switch_to = 0u;
    assert(dodo_address_space_runtime_validate(&f.runtime) ==
           DODO_ADDRESS_SPACE_RUNTIME_OK);
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
    test_create_allocates_exactly_one_root_frame();
    test_create_map_unmap_destroy_reclaims_all_owned_frames();
    test_invalid_owner_id_allocates_nothing();
    test_invalid_runtime_range_allocates_nothing();
    test_distinct_owners_can_have_independent_roots();
    test_owner_id_cannot_be_reused_for_live_runtime();
    test_user_root_creation_failure_releases_root_frame();
    test_partial_table_frame_allocation_is_rolled_back();
    test_table_frame_exhaustion_rolls_back_user_data();
    test_mapping_allocation_failure_leaves_runtime_unchanged();
    test_failed_root_switch_rolls_back_mapping_allocation();
    test_restore_switch_failure_keeps_mapping_owned();
    test_unmap_restore_failure_keeps_metadata_consistent();
    test_read_only_executable_mapping_is_accepted();
    test_duplicate_and_invalid_mapping_do_not_leak();
    test_low_level_mapping_failure_rolls_back_owned_frames();
    test_destroy_refuses_corrupted_hardware_mapping();
    test_unmap_fails_closed_when_hardware_leaf_is_missing();
    test_hardware_metadata_mismatch_fails_closed();
    test_mapping_capacity_is_bounded_without_leaks();
    test_runtime_validator_checks_root_frame_kind();
    test_runtime_validator_checks_table_frame_kind();
    test_runtime_validator_rejects_stale_unused_table_slot();
    test_runtime_rejects_borrowed_mapping_metadata();
    test_null_page_mapping_is_rejected_without_allocation();
    test_noncanonical_mapping_address_is_rejected();
    test_user_half_upper_boundary_is_exclusive();
    test_unaligned_mapping_address_allocates_nothing();
    test_out_of_range_mapping_address_allocates_nothing();
    test_invalid_mapping_flags_allocate_nothing();
    test_invalid_virtual_range_does_not_leak();
    test_runtime_activation_validates_and_switches_root();
    test_deactivation_switch_failure_preserves_user_root();
    test_activation_switch_failure_preserves_current_root();
    test_destroy_refuses_active_root();
    puts("address-space runtime tests: ok");
    return 0;
}
