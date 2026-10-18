#include <stddef.h>
#include <stdint.h>

#include "dodo/arch/x86_64/gdt.h"

struct dodo_x86_64_gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct dodo_x86_64_tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} __attribute__((packed));

_Static_assert(sizeof(struct dodo_x86_64_gdtr) == 10,
               "x86-64 GDTR operand must be 10 bytes");
_Static_assert(sizeof(struct dodo_x86_64_tss) == 104,
               "x86-64 TSS must be 104 bytes");

#define DODO_TSS_ACCESS_AVAILABLE UINT64_C(0x89)
#define DODO_GDT_ENTRY_COUNT 11u
#define DODO_RING0_INTERRUPT_STACK_SIZE 16384u

/*
 * Limine enters with CS=0x28 and DS/ES/SS=0x30. DODO deliberately preserves
 * the legacy descriptor slots and installs its own kernel/user descriptors
 * at the same selector offsets. The TSS descriptor occupies entries 9 and 10.
 */
static uint64_t gdt[DODO_GDT_ENTRY_COUNT] __attribute__((aligned(16))) = {
    UINT64_C(0x0000000000000000), /* 0x00: null */
    UINT64_C(0x00009A000000FFFF), /* 0x08: 16-bit code (Limine layout) */
    UINT64_C(0x000092000000FFFF), /* 0x10: 16-bit data (Limine layout) */
    UINT64_C(0x00CF9A000000FFFF), /* 0x18: 32-bit code (Limine layout) */
    UINT64_C(0x00CF92000000FFFF), /* 0x20: 32-bit data (Limine layout) */
    UINT64_C(0x00AF9A000000FFFF), /* 0x28: ring-0 64-bit code */
    UINT64_C(0x00CF92000000FFFF), /* 0x30: ring-0 data */
    UINT64_C(0x00CFF2000000FFFF), /* 0x38: ring-3 data, selector 0x3b */
    UINT64_C(0x00AFFA000000FFFF), /* 0x40: ring-3 64-bit code, selector 0x43 */
    UINT64_C(0),                 /* 0x48: low half of 64-bit TSS descriptor */
    UINT64_C(0)                  /* 0x50: high half of 64-bit TSS descriptor */
};

static struct dodo_x86_64_tss tss __attribute__((aligned(16)));
static uint8_t ring0_interrupt_stack[DODO_RING0_INTERRUPT_STACK_SIZE]
    __attribute__((aligned(16)));

static void set_tss_descriptor(void) {
    const uint64_t base = (uint64_t)(uintptr_t)&tss;
    const uint64_t limit = (uint64_t)(sizeof(tss) - 1u);

    gdt[9] = (limit & UINT64_C(0xffff)) |
             ((base & UINT64_C(0xffffff)) << 16) |
             (DODO_TSS_ACCESS_AVAILABLE << 40) |
             ((limit & UINT64_C(0xf0000)) << 32) |
             ((base & UINT64_C(0xff000000)) << 32);
    gdt[10] = base >> 32;
}

void dodo_x86_64_gdt_init(void) {
    tss = (struct dodo_x86_64_tss){0};
    tss.rsp0 = ((uint64_t)(uintptr_t)
        (ring0_interrupt_stack + sizeof(ring0_interrupt_stack))) &
        ~UINT64_C(0xf);
    /* Place the I/O bitmap beyond the TSS limit: CPL3 I/O instructions fault. */
    tss.io_map_base = (uint16_t)sizeof(tss);
    set_tss_descriptor();

    const struct dodo_x86_64_gdtr gdtr = {
        .limit = (uint16_t)(sizeof(gdt) - 1u),
        .base = (uint64_t)(uintptr_t)gdt
    };

    /*
     * The current CS/DS/ES/SS hidden caches remain valid: their selectors have
     * the same descriptor meaning in DODO's GDT. We don't perform an untested
     * far return here. LTR loads the privilege-transition stack and marks the
     * TSS descriptor busy.
     */
    const uint16_t tss_selector = DODO_X86_64_TSS_SELECTOR;
    __asm__ volatile (
        "lgdt %0\n\t"
        "ltr %1"
        :
        : "m"(gdtr), "rm"(tss_selector)
        : "memory"
    );
}

int dodo_x86_64_gdt_verify(void) {
    struct dodo_x86_64_gdtr active_gdtr;
    uint16_t cs;
    uint16_t ds;
    uint16_t es;
    uint16_t ss;
    uint16_t tr;

    __asm__ volatile ("sgdt %0" : "=m"(active_gdtr));
    __asm__ volatile ("str %0" : "=r"(tr));
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    __asm__ volatile ("mov %%ds, %0" : "=r"(ds));
    __asm__ volatile ("mov %%es, %0" : "=r"(es));
    __asm__ volatile ("mov %%ss, %0" : "=r"(ss));

    const uint64_t expected_rsp0 = ((uint64_t)(uintptr_t)
        (ring0_interrupt_stack + sizeof(ring0_interrupt_stack))) &
        ~UINT64_C(0xf);
    const uint64_t tss_access = (gdt[9] >> 40) & UINT64_C(0x0f);

    return active_gdtr.base == (uint64_t)(uintptr_t)gdt &&
           active_gdtr.limit == (uint16_t)(sizeof(gdt) - 1u) &&
           cs == DODO_X86_64_KERNEL_CODE_SELECTOR &&
           ds == DODO_X86_64_KERNEL_DATA_SELECTOR &&
           es == DODO_X86_64_KERNEL_DATA_SELECTOR &&
           ss == DODO_X86_64_KERNEL_DATA_SELECTOR &&
           tr == DODO_X86_64_TSS_SELECTOR &&
           tss_access == UINT64_C(0x0b) &&
           tss.rsp0 == expected_rsp0 &&
           tss.io_map_base == sizeof(tss) &&
           gdt[7] == UINT64_C(0x00CFF2000000FFFF) &&
           gdt[8] == UINT64_C(0x00AFFA000000FFFF);
}
