#include <stddef.h>
#include <stdint.h>

#include "dodo/arch/x86_64/interrupts.h"
#include "dodo/boot/limine_protocol.h"
#include "dodo/mm/phys.h"

#define COM1_PORT 0x3F8u
#define DODO_BOOT_TEST_FRAME_COUNT 4u

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

static struct dodo_phys_allocator physical_allocator;
/*
 * These four frames are deliberately retained after the bootstrap self-test.
 * Later page-table bootstrap code can use them; they are not returned to pool.
 */
static uint64_t bootstrap_reserved_frames[DODO_BOOT_TEST_FRAME_COUNT];
static volatile uint64_t breakpoint_selftest_armed = 0;

static const char *const exception_names[32] = {
    "Divide Error",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved Exception",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved Exception",
    "Reserved Exception",
    "Reserved Exception",
    "Reserved Exception",
    "Reserved Exception",
    "Reserved Exception",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved Exception"
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

static void serial_write_hex64(uint64_t value) {
    static const char hex_digits[] = "0123456789ABCDEF";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        serial_putc(hex_digits[(value >> (uint32_t)shift) & UINT64_C(0x0f)]);
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

int dodo_x86_64_exception_handler(
    const struct dodo_x86_64_exception_frame *frame) {
    if (frame == NULL || frame->vector >= 32u) {
        serial_write("DODO: FATAL: invalid exception frame\n");
        return 0;
    }

    if (frame->vector == 3u && breakpoint_selftest_armed != 0u) {
        breakpoint_selftest_armed = 0;
        serial_write("DODO: IDT breakpoint self-check passed; RIP=");
        serial_write_hex64(frame->rip);
        serial_write("\n");
        return 1;
    }

    serial_write("DODO: FATAL exception: ");
    serial_write(exception_names[frame->vector]);
    serial_write(" (vector ");
    serial_write_u64(frame->vector);
    serial_write(")\nDODO: error code = ");
    serial_write_hex64(frame->error_code);
    serial_write("\nDODO: instruction pointer = ");
    serial_write_hex64(frame->rip);
    serial_write("\n");

    if (frame->vector == 14u) {
        uint64_t fault_address;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
        serial_write("DODO: page-fault address = ");
        serial_write_hex64(fault_address);
        serial_write("\n");
    }

    return 0;
}

static void inspect_memory_map(void) {
    struct dodo_limine_memmap_response *response = limine_memmap_request.response;

    if (response == NULL) {
        report_boot_error("Limine did not provide a memory map");
    }

    serial_write("DODO: memory map received; entries = ");
    serial_write_u64(response->entry_count);
    serial_write("\n");

    const enum dodo_phys_status status =
        dodo_phys_init(&physical_allocator, response);
    if (status != DODO_PHYS_OK) {
        serial_write("DODO: physical allocator rejected map; code = ");
        serial_write_u64((uint64_t)status);
        serial_write("\n");
        report_boot_error("physical memory map validation failed");
    }

    const uint64_t initial_pages = physical_allocator.free_pages;
    if (initial_pages < DODO_BOOT_TEST_FRAME_COUNT) {
        report_boot_error("not enough usable frames for the bootstrap self-test");
    }

    serial_write("DODO: usable whole pages = ");
    serial_write_u64(initial_pages);
    serial_write("\n");

    for (uint64_t i = 0; i < DODO_BOOT_TEST_FRAME_COUNT; ++i) {
        uint64_t physical_address = 0;
        if (dodo_phys_alloc_page(&physical_allocator, &physical_address) !=
            DODO_PHYS_OK) {
            report_boot_error("physical page allocation failed");
        }

        if ((physical_address % DODO_PHYS_PAGE_SIZE) != 0) {
            report_boot_error("allocator returned an unaligned physical page");
        }

        for (uint64_t previous = 0; previous < i; ++previous) {
            if (bootstrap_reserved_frames[previous] == physical_address) {
                report_boot_error("allocator returned a duplicate physical page");
            }
        }

        bootstrap_reserved_frames[i] = physical_address;
    }

    serial_write("DODO: physical allocator self-check passed; reserved pages = ");
    serial_write_u64(DODO_BOOT_TEST_FRAME_COUNT);
    serial_write("\nDODO: remaining whole pages = ");
    serial_write_u64(physical_allocator.free_pages);
    serial_write("\n");
}

static void test_exception_handler(void) {
    breakpoint_selftest_armed = 1;
    __asm__ volatile ("int3" : : : "memory");

    if (breakpoint_selftest_armed != 0u) {
        report_boot_error("breakpoint exception self-test did not resume");
    }

    serial_write("DODO: exception handler resumed after INT3\n");
}

/*
 * Limine transfers control in x86-64 long mode at the ELF entry point.
 * This milestone validates the handoff, installs the early exception IDT,
 * exercises the physical page-frame allocator, and tests INT3 recovery.
 */
__attribute__((noreturn))
void kernel_main(void) {
    serial_init();
    dodo_x86_64_idt_init();

    serial_write("\nDODO OS: kernel reached entry point\n");

    if (limine_base_revision[2] != 0u) {
        report_boot_error("Limine base revision 3 was not accepted");
    }

    serial_write("DODO: Limine base revision accepted\n");
    serial_write("DODO: COM1 serial diagnostics ready\n");
    serial_write("DODO: exception IDT loaded\n");
    inspect_memory_map();
    test_exception_handler();
    serial_write("DODO: all kernel bootstrap self-checks passed\n");
    kernel_halt();
}
