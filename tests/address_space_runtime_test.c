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
        (root & (DODO_PAGE_SIZE - 1u)) != 0u) {
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
    test_failed_root_switch_rolls_back_mapping_allocation();
    test_duplicate_and_invalid_mapping_do_not_leak();
    test_destroy_refuses_active_root();
    puts("address-space runtime tests: ok");
    return 0;
}
