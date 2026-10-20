#include <stddef.h>
#include <stdint.h>

#include "dodo/arch/x86_64/interrupts.h"

#define DODO_IDT_ENTRIES 256u
#define DODO_IDT_EXCEPTION_COUNT 32u
#define DODO_IDT_INTERRUPT_GATE UINT8_C(0x8e)
#define DODO_IDT_USER_TEST_GATE UINT8_C(0xee)
#define DODO_IDT_USER_TEST_VECTOR 128u

struct dodo_idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct dodo_idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

_Static_assert(sizeof(struct dodo_idt_gate) == 16, "IDT gate must be 16 bytes");
_Static_assert(sizeof(struct dodo_idtr) == 10, "IDTR must be 10 bytes");

static struct dodo_idt_gate idt[DODO_IDT_ENTRIES] __attribute__((aligned(16)));
extern void (*dodo_isr_table[DODO_IDT_EXCEPTION_COUNT])(void);
extern void dodo_isr_128(void);
extern void dodo_isr_32(void);

static void set_gate(uint8_t vector, uintptr_t handler,
                     uint16_t code_selector, uint8_t attributes) {
    struct dodo_idt_gate *gate = &idt[vector];
    gate->offset_low = (uint16_t)(handler & UINT64_C(0xffff));
    gate->selector = code_selector;
    gate->ist = 0;
    gate->attributes = attributes;
    gate->offset_middle = (uint16_t)((handler >> 16) & UINT64_C(0xffff));
    gate->offset_high = (uint32_t)((handler >> 32) & UINT64_C(0xffffffff));
    gate->reserved = 0;
}

void dodo_x86_64_idt_init(void) {
    uint16_t code_selector;
    __asm__ volatile ("mov %%cs, %0" : "=r"(code_selector));

    for (uint64_t i = 0; i < DODO_IDT_ENTRIES; ++i) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].attributes = 0;
        idt[i].offset_middle = 0;
        idt[i].offset_high = 0;
        idt[i].reserved = 0;
    }

    for (uint8_t vector = 0; vector < DODO_IDT_EXCEPTION_COUNT; ++vector) {
        set_gate(vector, (uintptr_t)dodo_isr_table[vector], code_selector,
                 DODO_IDT_INTERRUPT_GATE);
    }

    /* IRQ0 is routed to vector 32 after the legacy PIC is remapped. */
    set_gate(32u, (uintptr_t)dodo_isr_32, code_selector,
             DODO_IDT_INTERRUPT_GATE);

    /* Only this intentional test interrupt is callable from CPL3. */
    set_gate(DODO_IDT_USER_TEST_VECTOR, (uintptr_t)dodo_isr_128,
             code_selector, DODO_IDT_USER_TEST_GATE);

    const struct dodo_idtr idtr = {
        .limit = (uint16_t)(sizeof(idt) - 1u),
        .base = (uint64_t)(uintptr_t)idt
    };

    __asm__ volatile ("cli; lidt %0" : : "m"(idtr) : "memory");
}
