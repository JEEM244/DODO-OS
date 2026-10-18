#include <stddef.h>
#include <stdint.h>

#include "dodo/boot/limine_protocol.h"

#define COM1_PORT 0x3F8u
#define DODO_MAX_MEMMAP_ENTRIES UINT64_C(4096)

/*
 * Limine protocol markers and base revision 3.
 * The loader updates the requested base-revision value when it accepts it.
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

/* Limine memory-map request identifier from the protocol specification. */
__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct dodo_limine_memmap_request limine_memmap_request = {
    .id = {
        0xc7b1dd30df4c8b88,
        0x0a82e883a194f07b,
        0x67cf3d9d378a806f,
        0xe304acdfc50c3c62
    },
    .revision = 0,
    .response = NULL
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

static void serial_write_u64(uint64_t value) {
    char digits[20];
    size_t count = 0;

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));

    while (count > 0u) {
        serial_putc(digits[--count]);
    }
}

__attribute__((noreturn))
static void kernel_halt(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

__attribute__((noreturn))
static void report_boot_error(const char *message) {
    serial_write("DODO: ERROR: ");
    serial_write(message);
    serial_write("\n");
    kernel_halt();
}

static void inspect_memory_map(void) {
    struct dodo_limine_memmap_response *response = limine_memmap_request.response;

    if (response == NULL) {
        report_boot_error("Limine did not provide a memory map");
    }

    if (response->entry_count > DODO_MAX_MEMMAP_ENTRIES) {
        report_boot_error("memory-map entry count exceeds the bootstrap limit");
    }

    if (response->entry_count != 0u && response->entries == NULL) {
        report_boot_error("memory-map entry array is null");
    }

    uint64_t usable_bytes = 0u;
    for (uint64_t i = 0; i < response->entry_count; ++i) {
        struct dodo_limine_memmap_entry *entry = response->entries[i];

        if (entry == NULL) {
            report_boot_error("memory map contains a null entry");
        }

        if (entry->type == DODO_LIMINE_MEMMAP_USABLE) {
            if (UINT64_MAX - usable_bytes < entry->length) {
                report_boot_error("usable-memory total overflowed");
            }
            usable_bytes += entry->length;
        }
    }

    serial_write("DODO: memory map accepted; entries = ");
    serial_write_u64(response->entry_count);
    serial_write("\nDODO: usable memory bytes = ");
    serial_write_u64(usable_bytes);
    serial_write("\n");
}

/*
 * Limine transfers control in x86-64 long mode at the ELF entry point.
 * This milestone validates the handoff and reads the firmware memory map;
 * general memory management, interrupts, and scheduling are still pending.
 */
__attribute__((noreturn))
void kernel_main(void) {
    serial_init();
    serial_write("\nDODO OS: kernel reached entry point\n");

    if (limine_base_revision[2] != 0u) {
        report_boot_error("Limine base revision 3 was not accepted");
    }

    serial_write("DODO: Limine base revision accepted\n");
    serial_write("DODO: COM1 serial diagnostics ready\n");
    inspect_memory_map();
    serial_write("DODO: milestone K1 reached; own memory manager pending\n");
    kernel_halt();
}
