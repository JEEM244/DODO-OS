#ifndef DODO_ARCH_X86_64_PAGING_H
#define DODO_ARCH_X86_64_PAGING_H

#include <stdint.h>

#define DODO_PAGE_SIZE UINT64_C(4096)
#define DODO_PAGE_WRITABLE (UINT64_C(1) << 1)
#define DODO_PAGE_USER_ACCESSIBLE (UINT64_C(1) << 2)
#define DODO_X86_64_USER_ADDRESS_LIMIT UINT64_C(0x0000800000000000)
#define DODO_PAGE_WRITE_THROUGH (UINT64_C(1) << 3)
#define DODO_PAGE_CACHE_DISABLE (UINT64_C(1) << 4)
#define DODO_PAGE_GLOBAL (UINT64_C(1) << 8)
#define DODO_PAGE_NO_EXECUTE (UINT64_C(1) << 63)

enum dodo_paging_status {
    DODO_PAGING_OK = 0,
    DODO_PAGING_INVALID_ARGUMENT = 1,
    DODO_PAGING_INVALID_ADDRESS = 2,
    DODO_PAGING_UNSUPPORTED_MODE = 3,
    DODO_PAGING_INVALID_HHDM = 4,
    DODO_PAGING_NO_FREE_KERNEL_SLOT = 5,
    DODO_PAGING_NO_TABLE_FRAMES = 6,
    DODO_PAGING_ALREADY_MAPPED = 7,
    DODO_PAGING_HUGE_PAGE_CONFLICT = 8,
    DODO_PAGING_PERMISSION_CONFLICT = 9,
    DODO_PAGING_NOT_SUPPORTED = 10,
    DODO_PAGING_READBACK_FAILED = 11,
    DODO_PAGING_CLONE_CAPACITY_EXCEEDED = 12,
    DODO_PAGING_ROLLBACK_FAILED = 13,
    DODO_PAGING_NO_FREE_USER_SLOT = 14
};

/*
 * Initialize DODO's x86-64 paging helpers against the current Limine page-table
 * root and its higher-half direct map. This does not replace the bootloader's
 * root; it makes a limited, checked extension to it for bootstrap testing.
 */
enum dodo_paging_status dodo_x86_64_paging_init(uint64_t hhdm_offset);

/*
 * Clone the active four-level page-table tree into newly allocated DODO-owned
 * table frames. Leaf mappings keep their physical targets and permissions;
 * intermediate tables are private to DODO. The allocator callback returns
 * nonzero on success. The old hierarchy is not reclaimed during bootstrap.
 */
typedef int (*dodo_paging_allocate_frame_fn)(
    void *context, uint64_t *physical_address);
typedef int (*dodo_paging_release_frame_fn)(
    void *context, uint64_t physical_address);

enum dodo_paging_status dodo_x86_64_paging_clone_tree(
    dodo_paging_allocate_frame_fn allocate_frame,
    dodo_paging_release_frame_fn release_frame,
    void *context,
    uint64_t *new_root_physical,
    uint64_t *tables_cloned);

int dodo_x86_64_nx_enabled(void);

/* Physical address of the kernel root established during bootstrap. */
uint64_t dodo_x86_64_kernel_root_physical(void);

/*
 * Create a separate user root with all lower-half PML4 entries empty and only
 * the kernel-half entries copied from DODO's active kernel root. This provides
 * a fresh user mapping tree while sharing supervisor-only higher-half mappings.
 */
enum dodo_paging_status dodo_x86_64_paging_create_user_root(
    uint64_t user_root_physical);

/* Switch CR3 to a previously prepared root and update the mapper's active root. */
enum dodo_paging_status dodo_x86_64_paging_switch_root(
    uint64_t root_physical);

/* Return the HHDM alias for a physical address, or NULL before init/on error. */
volatile void *dodo_x86_64_hhdm_pointer(uint64_t physical_address);

/* Find an empty canonical low-half PML4 slot for a user address space. */
enum dodo_paging_status dodo_x86_64_find_unused_user_region(
    uint64_t *virtual_address);

/*
 * Safely copy bytes from the address space currently loaded in hardware CR3.
 * Every paging level must be present and user-accessible; huge-page mappings
 * are deliberately rejected by this bootstrap API. Length is bounded by the
 * caller and the user-half canonical address limit.
 */
enum dodo_paging_status dodo_x86_64_copy_from_active_user(
    void *kernel_destination,
    uint64_t user_source,
    uint64_t length);

/* Assembly helpers used only by the controlled read-only/NX fault self-tests. */
int dodo_x86_64_test_write_readonly(volatile uint64_t *target);
int dodo_x86_64_test_execute_nx(void *target);
void dodo_x86_64_readonly_write_fault_resume(void);
void dodo_x86_64_nx_execute_fault_resume(void);

/*
 * Map one 4 KiB physical page at one canonical virtual address in the current
 * page-table root. page_table_frames is a pool of usable physical frames for
 * any missing intermediate tables; frames_used reports how many were consumed.
 * Existing mappings are never overwritten. This prototype supports 4-level
 * paging and supervisor mappings only.
 */
enum dodo_paging_status dodo_x86_64_map_page(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags,
    const uint64_t *page_table_frames,
    uint64_t page_table_frame_count,
    uint64_t *frames_used);

/* Map one page in an unused kernel PML4 slot, test both aliases, keep mapping. */
enum dodo_paging_status dodo_x86_64_paging_self_test(
    const uint64_t frames[4],
    uint64_t *mapped_virtual_address);

#endif
