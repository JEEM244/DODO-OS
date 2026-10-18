#include <stddef.h>
#include <stdint.h>

#define COM1_PORT 0x3F8u

/*
 * Limine protocol markers and base revision 3.
 * These live in a dedicated writable load segment because the bootloader
 * updates the requested base-revision value when it accepts the protocol.
 */
__attribute__((used, section(".limine_requests_start"), aligned(8)))
static volatile uint64_t limine_requests_start_marker[4] = {
    0xf6b8f4b39de7d1ae,
    0xfab91a6940fcb9cf,
    0x785c6ed015d3e316,
    0x181e920a7852b9d9
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile uint64_t limine_base_revision[3] = {
    0xf9562b2d5c95a6c8,
    0x6a7b384944536bdc,
    3
};

__attribute__((used, section(".limine_requests_end"), aligned(8)))
static volatile uint64_t limine_requests_end_marker[2] = {
    0xadc0e0531bb10d03,
    0x9572709f31764c62
};

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
    /* A bounded wait avoids hanging diagnostics if the UART is not present. */
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
 * Limine transfers control in x86-64 long mode at the ELF entry point.
 * Kernel functionality is intentionally minimal until the boot test is stable.
 */
__attribute__((noreturn))
void kernel_main(void) {
    serial_init();
    serial_write("\nDODO OS: kernel reached entry point\n");

    if (limine_base_revision[2] != 0u) {
        serial_write("DODO: ERROR: Limine base revision 3 was not accepted\n");
        kernel_halt();
    }

    serial_write("DODO: Limine base revision accepted\n");
    serial_write("DODO: COM1 serial diagnostics ready\n");
    serial_write("DODO: milestone K0 reached; CPU and memory setup pending\n");
    kernel_halt();
}
