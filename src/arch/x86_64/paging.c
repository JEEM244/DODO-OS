#include <stddef.h>
#include <stdint.h>

#include "dodo/arch/x86_64/paging.h"

#define DODO_PTE_PRESENT (UINT64_C(1) << 0)
#define DODO_PTE_WRITABLE (UINT64_C(1) << 1)
#define DODO_PTE_USER (UINT64_C(1) << 2)
#define DODO_PTE_PAGE_SIZE (UINT64_C(1) << 7)
#define DODO_PTE_NO_EXECUTE (UINT64_C(1) << 63)
#define DODO_PTE_ADDRESS_MASK UINT64_C(0x000ffffffffff000)
#define DODO_PTE_ALLOWED_FLAGS (DODO_PAGE_WRITABLE | DODO_PAGE_USER_ACCESSIBLE | \
                                DODO_PAGE_WRITE_THROUGH | DODO_PAGE_CACHE_DISABLE | \
                                DODO_PAGE_GLOBAL | DODO_PAGE_NO_EXECUTE)
#define DODO_PML4_FIRST_KERNEL_SLOT UINT64_C(256)
#define DODO_PML4_LAST_KERNEL_SLOT UINT64_C(510)
#define DODO_PML4_SLOT_SHIFT 39u
#define DODO_PML4_SLOT_SPAN (UINT64_C(1) << DODO_PML4_SLOT_SHIFT)
#define DODO_HHDM_MINIMUM UINT64_C(0xffff800000000000)
#define DODO_USER_ADDRESS_LIMIT UINT64_C(0x0000800000000000)
#define DODO_EFER_MSR UINT32_C(0xc0000080)
#define DODO_EFER_NXE (UINT64_C(1) << 11)
#define DODO_CR4_LA57 (UINT64_C(1) << 12)
#define DODO_CR0_WP (UINT64_C(1) << 16)
#define DODO_MAX_TABLE_FRAMES UINT64_C(3)

static uint64_t hhdm_offset;
static uint64_t root_table_physical;
static int paging_initialized;
static int nx_enabled;

static int is_canonical(uint64_t address) {
    return address <= UINT64_C(0x00007fffffffffff) ||
           address >= UINT64_C(0xffff800000000000);
}

static int physical_to_hhdm(uint64_t physical_address,
                            volatile uint64_t **virtual_pointer) {
    if (virtual_pointer == 0 ||
        physical_address > DODO_PTE_ADDRESS_MASK ||
        physical_address > UINT64_MAX - hhdm_offset) {
        return 0;
    }

    const uint64_t virtual_address = hhdm_offset + physical_address;
    if (!is_canonical(virtual_address) ||
        virtual_address < DODO_HHDM_MINIMUM) {
        return 0;
    }

    *virtual_pointer = (volatile uint64_t *)(uintptr_t)virtual_address;
    return 1;
}

static void zero_physical_page(volatile uint64_t *page) {
    for (uint64_t i = 0; i < DODO_PAGE_SIZE / sizeof(uint64_t); ++i) {
        page[i] = 0;
    }
}

static uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static uint64_t read_cr4(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(value));
    return value;
}

static int enable_supervisor_write_protect(void) {
    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= DODO_CR0_WP;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & DODO_CR0_WP) != 0u;
}

static void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *a,
                  uint32_t *b, uint32_t *c, uint32_t *d) {
    uint32_t eax = leaf;
    uint32_t ebx;
    uint32_t ecx = subleaf;
    uint32_t edx;

    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    *a = eax;
    *b = ebx;
    *c = ecx;
    *d = edx;
}

static int probe_and_enable_nx(void) {
    uint32_t a, b, c, d;
    cpuid(UINT32_C(1), 0, &a, &b, &c, &d);

    /* EFER is an MSR; do not touch it if CPUID says MSRs are unavailable. */
    if ((d & (UINT32_C(1) << 5)) == 0) {
        return 0;
    }

    cpuid(UINT32_C(0x80000000), 0, &a, &b, &c, &d);
    if (a < UINT32_C(0x80000001)) {
        return 0;
    }

    cpuid(UINT32_C(0x80000001), 0, &a, &b, &c, &d);
    if ((d & (UINT32_C(1) << 20)) == 0) {
        return 0;
    }

    uint32_t low, high;
    __asm__ volatile ("rdmsr"
                      : "=a"(low), "=d"(high)
                      : "c"(DODO_EFER_MSR));
    uint64_t efer = ((uint64_t)high << 32) | low;
    efer |= DODO_EFER_NXE;
    low = (uint32_t)efer;
    high = (uint32_t)(efer >> 32);
    __asm__ volatile ("wrmsr"
                      :
                      : "c"(DODO_EFER_MSR), "a"(low), "d"(high)
                      : "memory");

    return 1;
}

enum dodo_paging_status dodo_x86_64_paging_init(uint64_t offset) {
    paging_initialized = 0;
    nx_enabled = 0;

    if ((offset & (DODO_PAGE_SIZE - 1u)) != 0u ||
        offset < DODO_HHDM_MINIMUM ||
        !is_canonical(offset)) {
        return DODO_PAGING_INVALID_HHDM;
    }

    /* This bootstrap mapper supports the 4-level page tables used by Limine. */
    if ((read_cr4() & DODO_CR4_LA57) != 0u) {
        return DODO_PAGING_UNSUPPORTED_MODE;
    }

    hhdm_offset = offset;
    root_table_physical = read_cr3() & DODO_PTE_ADDRESS_MASK;

    volatile uint64_t *root_table;
    if (!physical_to_hhdm(root_table_physical, &root_table)) {
        return DODO_PAGING_INVALID_HHDM;
    }

    /*
     * Without CR0.WP, supervisor-mode writes can bypass read-only PTEs.
     * Enable it before any protection self-test or security-sensitive mapping.
     */
    if (!enable_supervisor_write_protect()) {
        return DODO_PAGING_NOT_SUPPORTED;
    }

    nx_enabled = probe_and_enable_nx();
    paging_initialized = 1;
    return DODO_PAGING_OK;
}

#define DODO_MAX_CLONED_TABLES UINT64_C(8192)

struct dodo_page_table_clone_entry {
    uint64_t old_physical;
    uint64_t new_physical;
};

static struct dodo_page_table_clone_entry
    page_table_clone_cache[DODO_MAX_CLONED_TABLES];
static uint64_t page_table_clone_count;

/* Forward declaration: user-region discovery appears before the mapper helpers. */
static enum dodo_paging_status get_root_table(volatile uint64_t **root_table);

static enum dodo_paging_status clone_page_table(
    uint64_t old_physical,
    uint64_t level,
    dodo_paging_allocate_frame_fn allocate_frame,
    dodo_paging_release_frame_fn release_frame,
    void *context,
    uint64_t *new_physical) {
    if (new_physical == 0 || level < 1u || level > 4u ||
        (old_physical & (DODO_PAGE_SIZE - 1u)) != 0u ||
        old_physical > DODO_PTE_ADDRESS_MASK) {
        return DODO_PAGING_INVALID_ADDRESS;
    }

    /* Preserve shared table structure and terminate recursive self-mappings. */
    for (uint64_t i = 0; i < page_table_clone_count; ++i) {
        if (page_table_clone_cache[i].old_physical == old_physical) {
            *new_physical = page_table_clone_cache[i].new_physical;
            return DODO_PAGING_OK;
        }
    }

    if (page_table_clone_count >= DODO_MAX_CLONED_TABLES) {
        return DODO_PAGING_CLONE_CAPACITY_EXCEEDED;
    }

    uint64_t allocated_physical = 0;
    if (allocate_frame == 0 || !allocate_frame(context, &allocated_physical)) {
        return DODO_PAGING_NO_TABLE_FRAMES;
    }

    if ((allocated_physical & (DODO_PAGE_SIZE - 1u)) != 0u ||
        allocated_physical > DODO_PTE_ADDRESS_MASK ||
        allocated_physical == old_physical) {
        return DODO_PAGING_INVALID_ADDRESS;
    }

    for (uint64_t i = 0; i < page_table_clone_count; ++i) {
        if (page_table_clone_cache[i].new_physical == allocated_physical) {
            return DODO_PAGING_INVALID_ADDRESS;
        }
    }

    volatile uint64_t *old_table;
    volatile uint64_t *new_table;
    if (!physical_to_hhdm(old_physical, &old_table)) {
        (void)release_frame(context, allocated_physical);
        return DODO_PAGING_INVALID_HHDM;
    }
    if (!physical_to_hhdm(allocated_physical, &new_table)) {
        (void)release_frame(context, allocated_physical);
        return DODO_PAGING_INVALID_HHDM;
    }

    /*
     * Register before descending so shared references or recursive PML4
     * mappings resolve to this same new table instead of allocating forever.
     */
    page_table_clone_cache[page_table_clone_count].old_physical = old_physical;
    page_table_clone_cache[page_table_clone_count].new_physical =
        allocated_physical;
    page_table_clone_count++;

    for (uint64_t i = 0; i < DODO_PAGE_SIZE / sizeof(uint64_t); ++i) {
        const uint64_t entry = old_table[i];
        new_table[i] = entry;

        if ((entry & DODO_PTE_PRESENT) == 0u || level == 1u) {
            continue;
        }

        /*
         * Bit 7 means a huge-page leaf at PDPT/PD levels, not a child table.
         * A PML4 huge-page entry is not a supported x86-64 mapping.
         */
        if (level == 4u && (entry & DODO_PTE_PAGE_SIZE) != 0u) {
            return DODO_PAGING_NOT_SUPPORTED;
        }
        if ((level == 3u || level == 2u) &&
            (entry & DODO_PTE_PAGE_SIZE) != 0u) {
            continue;
        }

        const uint64_t old_child = entry & DODO_PTE_ADDRESS_MASK;
        uint64_t new_child = 0;
        const enum dodo_paging_status status = clone_page_table(
            old_child, level - 1u, allocate_frame, release_frame,
            context, &new_child);
        if (status != DODO_PAGING_OK) {
            return status;
        }

        new_table[i] = (entry & ~DODO_PTE_ADDRESS_MASK) |
                       (new_child & DODO_PTE_ADDRESS_MASK);
    }

    *new_physical = allocated_physical;
    return DODO_PAGING_OK;
}

enum dodo_paging_status dodo_x86_64_paging_clone_tree(
    dodo_paging_allocate_frame_fn allocate_frame,
    dodo_paging_release_frame_fn release_frame,
    void *context,
    uint64_t *new_root_physical,
    uint64_t *tables_cloned) {
    if (!paging_initialized || allocate_frame == 0 || release_frame == 0 ||
        new_root_physical == 0 || tables_cloned == 0) {
        return DODO_PAGING_INVALID_ARGUMENT;
    }

    page_table_clone_count = 0;
    const uint64_t old_root_physical = root_table_physical;
    uint64_t cloned_root_physical = 0;

    enum dodo_paging_status status = clone_page_table(
        old_root_physical, 4u, allocate_frame, release_frame, context,
        &cloned_root_physical);
    if (status != DODO_PAGING_OK) {
        /*
         * Nothing from a failed clone was installed in CR3. Roll back every
         * frame recorded so far, in reverse order, to avoid a partial-tree leak.
         */
        int rollback_ok = 1;
        for (uint64_t i = page_table_clone_count; i > 0; --i) {
            const uint64_t frame =
                page_table_clone_cache[i - 1u].new_physical;
            if (!release_frame(context, frame)) {
                rollback_ok = 0;
            }
            page_table_clone_cache[i - 1u].old_physical = 0;
            page_table_clone_cache[i - 1u].new_physical = 0;
        }
        page_table_clone_count = 0;
        return rollback_ok ? status : DODO_PAGING_ROLLBACK_FAILED;
    }

    __asm__ volatile ("mov %0, %%cr3"
                      :
                      : "r"(cloned_root_physical)
                      : "memory");

    if ((read_cr3() & DODO_PTE_ADDRESS_MASK) != cloned_root_physical) {
        return DODO_PAGING_READBACK_FAILED;
    }

    root_table_physical = cloned_root_physical;
    *new_root_physical = cloned_root_physical;
    *tables_cloned = page_table_clone_count;

    /* A separate read keeps this observable after the root variable updates. */
    (void)old_root_physical;
    return DODO_PAGING_OK;
}

int dodo_x86_64_nx_enabled(void) {
    return nx_enabled;
}

volatile void *dodo_x86_64_hhdm_pointer(uint64_t physical_address) {
    if (!paging_initialized) {
        return 0;
    }

    volatile uint64_t *pointer;
    if (!physical_to_hhdm(physical_address, &pointer)) {
        return 0;
    }
    return (volatile void *)pointer;
}

enum dodo_paging_status dodo_x86_64_find_unused_user_region(
    uint64_t *virtual_address) {
    if (!paging_initialized || virtual_address == 0) {
        return DODO_PAGING_INVALID_ARGUMENT;
    }

    volatile uint64_t *root_table;
    const enum dodo_paging_status status = get_root_table(&root_table);
    if (status != DODO_PAGING_OK) {
        return status;
    }

    /* Skip PML4[0], which commonly contains boot-time identity mappings. */
    for (uint64_t index = 1; index < UINT64_C(256); ++index) {
        if (root_table[index] == 0u) {
            *virtual_address = index * DODO_PML4_SLOT_SPAN;
            if (!is_canonical(*virtual_address)) {
                return DODO_PAGING_INVALID_ADDRESS;
            }
            return DODO_PAGING_OK;
        }
    }

    return DODO_PAGING_NO_FREE_USER_SLOT;
}

static enum dodo_paging_status get_root_table(volatile uint64_t **root_table) {
    if (!paging_initialized) {
        return DODO_PAGING_INVALID_ARGUMENT;
    }
    if (!physical_to_hhdm(root_table_physical, root_table)) {
        return DODO_PAGING_INVALID_HHDM;
    }
    return DODO_PAGING_OK;
}

static enum dodo_paging_status get_or_create_child(
    volatile uint64_t *parent_entry,
    uint64_t level,
    uint64_t mapping_flags,
    const uint64_t *frame_pool,
    uint64_t frame_count,
    uint64_t *frames_used,
    volatile uint64_t **child_table) {
    uint64_t entry = *parent_entry;

    if ((entry & DODO_PTE_PRESENT) != 0u) {
        if (level != 0u && (entry & DODO_PTE_PAGE_SIZE) != 0u) {
            return DODO_PAGING_HUGE_PAGE_CONFLICT;
        }
        if ((mapping_flags & DODO_PAGE_WRITABLE) != 0u &&
            (entry & DODO_PTE_WRITABLE) == 0u) {
            return DODO_PAGING_PERMISSION_CONFLICT;
        }
        if ((mapping_flags & DODO_PAGE_USER_ACCESSIBLE) != 0u &&
            (entry & DODO_PTE_USER) == 0u) {
            /* Never widen an existing supervisor-only ancestor implicitly. */
            return DODO_PAGING_PERMISSION_CONFLICT;
        }
        if ((mapping_flags & DODO_PAGE_NO_EXECUTE) == 0u &&
            (entry & DODO_PTE_NO_EXECUTE) != 0u) {
            return DODO_PAGING_PERMISSION_CONFLICT;
        }

        const uint64_t child_physical = entry & DODO_PTE_ADDRESS_MASK;
        if (!physical_to_hhdm(child_physical, child_table)) {
            return DODO_PAGING_INVALID_HHDM;
        }
        return DODO_PAGING_OK;
    }

    if (*frames_used >= frame_count) {
        return DODO_PAGING_NO_TABLE_FRAMES;
    }

    const uint64_t frame = frame_pool[*frames_used];
    if ((frame & (DODO_PAGE_SIZE - 1u)) != 0u ||
        frame > DODO_PTE_ADDRESS_MASK) {
        return DODO_PAGING_INVALID_ADDRESS;
    }

    volatile uint64_t *new_table;
    if (!physical_to_hhdm(frame, &new_table)) {
        return DODO_PAGING_INVALID_HHDM;
    }
    zero_physical_page(new_table);

    /*
     * Intermediate permissions must allow every requested leaf permission;
     * supervisor mappings never acquire U/S bits, even when a parent exists.
     */
    *parent_entry = frame | DODO_PTE_PRESENT | DODO_PTE_WRITABLE |
        ((mapping_flags & DODO_PAGE_USER_ACCESSIBLE) != 0u
            ? DODO_PTE_USER : 0u);
    *frames_used += 1u;
    *child_table = new_table;
    return DODO_PAGING_OK;
}

enum dodo_paging_status dodo_x86_64_map_page(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags,
    const uint64_t *page_table_frames,
    uint64_t page_table_frame_count,
    uint64_t *frames_used) {
    if (!paging_initialized || frames_used == 0 ||
        (page_table_frame_count != 0u && page_table_frames == 0)) {
        return DODO_PAGING_INVALID_ARGUMENT;
    }

    *frames_used = 0;

    if (!is_canonical(virtual_address) ||
        (virtual_address & (DODO_PAGE_SIZE - 1u)) != 0u ||
        (physical_address & (DODO_PAGE_SIZE - 1u)) != 0u ||
        physical_address > DODO_PTE_ADDRESS_MASK ||
        (flags & ~DODO_PTE_ALLOWED_FLAGS) != 0u ||
        ((flags & DODO_PAGE_USER_ACCESSIBLE) != 0u &&
         virtual_address >= DODO_USER_ADDRESS_LIMIT)) {
        return DODO_PAGING_INVALID_ADDRESS;
    }

    if (page_table_frame_count > DODO_MAX_TABLE_FRAMES) {
        return DODO_PAGING_INVALID_ARGUMENT;
    }
    if ((flags & DODO_PAGE_NO_EXECUTE) != 0u && !nx_enabled) {
        return DODO_PAGING_NOT_SUPPORTED;
    }

    for (uint64_t i = 0; i < page_table_frame_count; ++i) {
        const uint64_t frame = page_table_frames[i];
        if ((frame & (DODO_PAGE_SIZE - 1u)) != 0u ||
            frame > DODO_PTE_ADDRESS_MASK ||
            frame == physical_address) {
            return DODO_PAGING_INVALID_ADDRESS;
        }
        for (uint64_t j = 0; j < i; ++j) {
            if (frame == page_table_frames[j]) {
                return DODO_PAGING_INVALID_ADDRESS;
            }
        }
    }

    volatile uint64_t *table;
    enum dodo_paging_status status = get_root_table(&table);
    if (status != DODO_PAGING_OK) {
        return status;
    }

    const uint64_t indexes[3] = {
        (virtual_address >> 39) & UINT64_C(0x1ff),
        (virtual_address >> 30) & UINT64_C(0x1ff),
        (virtual_address >> 21) & UINT64_C(0x1ff)
    };

    for (uint64_t level = 0; level < 3u; ++level) {
        volatile uint64_t *child = 0;
        status = get_or_create_child(&table[indexes[level]], level,
                                     flags, page_table_frames,
                                     page_table_frame_count, frames_used,
                                     &child);
        if (status != DODO_PAGING_OK) {
            return status;
        }
        table = child;
    }

    const uint64_t leaf_index = (virtual_address >> 12) & UINT64_C(0x1ff);
    if ((table[leaf_index] & DODO_PTE_PRESENT) != 0u) {
        return DODO_PAGING_ALREADY_MAPPED;
    }

    table[leaf_index] = physical_address | DODO_PTE_PRESENT | flags;
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return DODO_PAGING_OK;
}

static enum dodo_paging_status find_unused_kernel_slot(uint64_t *virtual_address) {
    if (virtual_address == 0) {
        return DODO_PAGING_INVALID_ARGUMENT;
    }

    volatile uint64_t *root_table;
    enum dodo_paging_status status = get_root_table(&root_table);
    if (status != DODO_PAGING_OK) {
        return status;
    }

    for (uint64_t index = DODO_PML4_FIRST_KERNEL_SLOT;
         index <= DODO_PML4_LAST_KERNEL_SLOT; ++index) {
        if (root_table[index] == 0u) {
            *virtual_address =
                DODO_HHDM_MINIMUM +
                ((index - DODO_PML4_FIRST_KERNEL_SLOT) * DODO_PML4_SLOT_SPAN);
            return DODO_PAGING_OK;
        }
    }

    return DODO_PAGING_NO_FREE_KERNEL_SLOT;
}

enum dodo_paging_status dodo_x86_64_paging_self_test(
    const uint64_t frames[4],
    uint64_t *mapped_virtual_address) {
    if (frames == 0 || mapped_virtual_address == 0 ||
        !paging_initialized) {
        return DODO_PAGING_INVALID_ARGUMENT;
    }

    for (uint64_t i = 0; i < 4u; ++i) {
        if ((frames[i] & (DODO_PAGE_SIZE - 1u)) != 0u ||
            frames[i] > DODO_PTE_ADDRESS_MASK) {
            return DODO_PAGING_INVALID_ADDRESS;
        }
        for (uint64_t j = 0; j < i; ++j) {
            if (frames[i] == frames[j]) {
                return DODO_PAGING_INVALID_ADDRESS;
            }
        }
    }

    uint64_t test_virtual_address = 0;
    enum dodo_paging_status status =
        find_unused_kernel_slot(&test_virtual_address);
    if (status != DODO_PAGING_OK) {
        return status;
    }

    uint64_t frames_used = 0;
    const uint64_t flags = DODO_PAGE_WRITABLE |
        (nx_enabled ? DODO_PAGE_NO_EXECUTE : 0u);
    status = dodo_x86_64_map_page(test_virtual_address, frames[3],
                                  flags, frames, 3u, &frames_used);
    if (status != DODO_PAGING_OK) {
        return status;
    }
    if (frames_used != 3u) {
        return DODO_PAGING_NO_TABLE_FRAMES;
    }

    volatile uint64_t *direct_alias;
    if (!physical_to_hhdm(frames[3], &direct_alias)) {
        return DODO_PAGING_INVALID_HHDM;
    }

    volatile uint64_t *mapped_alias =
        (volatile uint64_t *)(uintptr_t)test_virtual_address;
    const uint64_t first_pattern = UINT64_C(0xd0d0c0ffee123456);
    const uint64_t second_pattern = UINT64_C(0x5a5a96a5d00df00d);

    *direct_alias = first_pattern;
    if (*mapped_alias != first_pattern) {
        return DODO_PAGING_READBACK_FAILED;
    }

    *mapped_alias = second_pattern;
    if (*direct_alias != second_pattern) {
        return DODO_PAGING_READBACK_FAILED;
    }

    *mapped_virtual_address = test_virtual_address;
    return DODO_PAGING_OK;
}
