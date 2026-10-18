#include <stddef.h>
#include <stdint.h>

#define COM1_PORT 0x3F8u

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void) {
    outb((uint16_t)(COM1_PORT + 1u), 0x00u);
    outb((uint16_t)(COM1_PORT + 3u), 0x80u);
    outb((uint16_t)(COM1_PORT + 0u), 0x01u);
    outb((uint16_t)(COM1_PORT + 1u), 0x00u);
    outb((uint16_t)(COM1_PORT + 3u), 0x03u);
    outb((uint16_t)(COM1_PORT + 2u), 0xC7u);
    outb((uint16_t)(COM1_PORT + 4u), 0x0Bu);
}

static void serial_putc(char ch) {
    /* Bound the wait so a missing serial device cannot hang diagnostics forever. */
    for (uint32_t tries = 0; tries < 1000000u; ++tries) {
        if ((inb((uint16_t)(COM1_PORT + 5u)) & 0x20u) != 0u) {
            outb((uint16_t)(COM1_PORT + 0u), (uint8_t)ch);
            return;
        }
        __asm__ volatile ("pause");
    }
}

static void serial_write(const char *text) {
    if (text == NULL) {
        return;
    }

    while (*text != '\0') {
        if (*text == '\n') {
            serial_putc('\r');
        }
        serial_putc(*text++);
    }
}

__attribute__((noreturn))
static void kernel_halt(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

/*
 * This is the first freestanding kernel entry routine.
 * A boot protocol and a bootable image are intentionally not claimed yet.
 */
__attribute__((noreturn))
void kernel_main(void) {
    serial_init();
    serial_write("\nDODO OS kernel scaffold\n");
    serial_write("Milestone: M0 / serial diagnostics\n");
    serial_write("Status: experimental; no security guarantees\n");
    kernel_halt();
}
