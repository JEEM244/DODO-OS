#include <stddef.h>
#include <stdint.h>

#include "dodo/arch/x86_64/gdt.h"

struct dodo_x86_64_gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

_Static_assert(sizeof(struct dodo_x86_64_gdtr) == 10,
               "x86-64 GDTR operand must be 10 bytes");

/*
 * Flat segmentation: privilege separation will be enforced with paging and
 * CPL transitions. Ring-3 selectors are reserved for the later user-mode work.
 */
static const uint64_t gdt[] __attribute__((aligned(8))) = {
    UINT64_C(0x0000000000000000), /* 0x00: null */
    UINT64_C(0x00AF9A000000FFFF), /* 0x08: ring-0 64-bit code */
    UINT64_C(0x00CF92000000FFFF), /* 0x10: ring-0 data */
    UINT64_C(0x00CFF2000000FFFF), /* 0x18: ring-3 data, selector 0x1b */
    UINT64_C(0x00AFFA000000FFFF)  /* 0x20: ring-3 64-bit code, selector 0x23 */
};

void dodo_x86_64_gdt_init(void) {
    const struct dodo_x86_64_gdtr gdtr = {
        .limit = (uint16_t)(sizeof(gdt) - 1u),
        .base = (uint64_t)(uintptr_t)gdt
    };

    /*
     * The ring-0 code descriptor is intentionally identical to the selector
     * Limine provided at entry. Load the new table and replace data selectors
     * first; an audited far-transfer path will be added before CPL transitions.
     */
    __asm__ volatile (
        "lgdt %0\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "xorw %%ax, %%ax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        :
        : "m"(gdtr)
        : "rax", "memory"
    );
}

int dodo_x86_64_gdt_verify(void) {
    struct dodo_x86_64_gdtr active_gdtr;
    uint16_t cs;
    uint16_t ds;
    uint16_t es;
    uint16_t ss;

    __asm__ volatile ("sgdt %0" : "=m"(active_gdtr));
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    __asm__ volatile ("mov %%ds, %0" : "=r"(ds));
    __asm__ volatile ("mov %%es, %0" : "=r"(es));
    __asm__ volatile ("mov %%ss, %0" : "=r"(ss));

    return active_gdtr.base == (uint64_t)(uintptr_t)gdt &&
           active_gdtr.limit == (uint16_t)(sizeof(gdt) - 1u) &&
           cs == DODO_X86_64_KERNEL_CODE_SELECTOR &&
           ds == DODO_X86_64_KERNEL_DATA_SELECTOR &&
           es == DODO_X86_64_KERNEL_DATA_SELECTOR &&
           ss == DODO_X86_64_KERNEL_DATA_SELECTOR &&
           gdt[3] == UINT64_C(0x00CFF2000000FFFF) &&
           gdt[4] == UINT64_C(0x00AFFA000000FFFF);
}
