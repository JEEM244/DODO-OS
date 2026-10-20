#include <stdint.h>

#include "dodo/arch/x86_64/timer.h"

#define DODO_PIC_MASTER_COMMAND 0x20u
#define DODO_PIC_MASTER_DATA    0x21u
#define DODO_PIC_SLAVE_COMMAND  0xa0u
#define DODO_PIC_SLAVE_DATA     0xa1u
#define DODO_PIT_CHANNEL_0      0x40u
#define DODO_PIT_COMMAND        0x43u

static volatile uint64_t ticks = 0;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

void dodo_x86_64_timer_init(void) {
    /* Do not let a partially reprogrammed PIC deliver an interrupt. */
    __asm__ volatile ("cli" : : : "memory");
    outb(DODO_PIC_MASTER_DATA, 0xffu);
    outb(DODO_PIC_SLAVE_DATA, 0xffu);

    /* ICW1: initialize both 8259 controllers and expect ICW4. */
    outb(DODO_PIC_MASTER_COMMAND, 0x11u);
    outb(0x80u, 0u);
    outb(DODO_PIC_SLAVE_COMMAND, 0x11u);
    outb(0x80u, 0u);

    /* ICW2: route hardware IRQs to vectors 32-47, clear of CPU exceptions. */
    outb(DODO_PIC_MASTER_DATA, 0x20u);
    outb(DODO_PIC_SLAVE_DATA, 0x28u);
    /* ICW3: slave PIC is connected to master IRQ2. */
    outb(DODO_PIC_MASTER_DATA, 0x04u);
    outb(DODO_PIC_SLAVE_DATA, 0x02u);
    /* ICW4: 8086/88 mode. */
    outb(DODO_PIC_MASTER_DATA, 0x01u);
    outb(DODO_PIC_SLAVE_DATA, 0x01u);

    /* Unmask only IRQ0; keep keyboard and all other IRQs disabled for now. */
    outb(DODO_PIC_MASTER_DATA, 0xfeu);
    outb(DODO_PIC_SLAVE_DATA, 0xffu);

    const uint16_t divisor =
        (uint16_t)(1193182u / DODO_X86_64_TIMER_HZ);
    outb(DODO_PIT_COMMAND, 0x36u);
    outb(DODO_PIT_CHANNEL_0, (uint8_t)(divisor & 0xffu));
    outb(DODO_PIT_CHANNEL_0, (uint8_t)(divisor >> 8));

    ticks = 0;
    __asm__ volatile ("sti" : : : "memory");
}

void dodo_x86_64_timer_handle_irq(void) {
    ticks++;
    /* IRQ0 originates on the master PIC, so only it needs an EOI. */
    outb(DODO_PIC_MASTER_COMMAND, 0x20u);
}

uint64_t dodo_x86_64_timer_ticks(void) {
    return ticks;
}
