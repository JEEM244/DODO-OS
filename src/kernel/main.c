#include <stddef.h>
#include <stdint.h>

#include "dodo/arch/x86_64/interrupts.h"
#include "dodo/arch/x86_64/paging.h"
#include "dodo/boot/limine_protocol.h"
#include "dodo/mm/phys.h"

#define COM1_PORT 0x3F8u
#define DODO_BOOT_TEST_FRAME_COUNT 5u

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

/* Limine higher-half direct-map request. */
__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct dodo_limine_hhdm_request limine_hhdm_request = {
    .id = {
        0xc7b1dd30df4c8b88,
        0x0a82e883a194f07b,
        0x48dcf1cb8ad2b852,
        0x63984e959a98244b
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
 * The first four frames are retained by the paging self-test: three for
 * intermediate tables and one for the data page. The fifth is a read-only/NX
 * page used by controlled protection-fault tests.
 */
static uint64_t bootstrap_reserved_frames[DODO_BOOT_TEST_FRAME_COUNT];
static volatile uint64_t breakpoint_selftest_armed = 0;
static volatile uint64_t page_fault_selftest_armed = 0;
static volatile uint64_t expected_page_fault_address = 0;
static volatile uint64_t expected_page_fault_kind = 0;

#define DODO_EXPECT_WRITE_FAULT UINT64_C(1)
#define DODO_EXPECT_EXECUTE_FAULT UINT64_C(2)

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
    struct dodo_x86_64_exception_frame *frame) {
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

    /*
     * Only resume a page fault when the explicitly armed probe faulted at the
     * exact expected address and the CPU error code matches the expected case.
     * All other page faults remain fatal.
     */
    if (frame->vector == 14u && page_fault_selftest_armed != 0u) {
        uint64_t fault_address;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));

        if (fault_address == expected_page_fault_address) {
            const uint64_t error = frame->error_code;
            if (expected_page_fault_kind == DODO_EXPECT_WRITE_FAULT &&
                (error & UINT64_C(0x3)) == UINT64_C(0x3)) {
                page_fault_selftest_armed = 0;
                frame->rip = (uint64_t)(uintptr_t)
                    dodo_x86_64_readonly_write_fault_resume;
                serial_write("DODO: read-only page write was blocked\n");
                return 1;
            }

            if (expected_page_fault_kind == DODO_EXPECT_EXECUTE_FAULT &&
                (error & UINT64_C(0x11)) == UINT64_C(0x11) &&
                (error & UINT64_C(0x2)) == 0u) {
                page_fault_selftest_armed = 0;
                frame->rip = (uint64_t)(uintptr_t)
                    dodo_x86_64_nx_execute_fault_resume;
                serial_write("DODO: NX instruction fetch was blocked\n");
                return 1;
            }
        }
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

static void test_virtual_mapping(void) {
    struct dodo_limine_hhdm_response *response = limine_hhdm_request.response;
    if (response == NULL) {
        report_boot_error("Limine did not provide the higher-half direct map");
    }

    serial_write("DODO: HHDM offset = ");
    serial_write_hex64(response->offset);
    serial_write("\n");

    enum dodo_paging_status status =
        dodo_x86_64_paging_init(response->offset);
    if (status != DODO_PAGING_OK) {
        serial_write("DODO: paging initialization error code = ");
        serial_write_u64((uint64_t)status);
        serial_write("\n");
        report_boot_error("paging helper initialization failed");
    }

    serial_write("DODO: NX page protection ");
    serial_write(dodo_x86_64_nx_enabled() ? "enabled\n" : "not available\n");

    uint64_t mapped_virtual_address = 0;
    status = dodo_x86_64_paging_self_test(
        bootstrap_reserved_frames, &mapped_virtual_address);
    if (status != DODO_PAGING_OK) {
        serial_write("DODO: virtual mapping test error code = ");
        serial_write_u64((uint64_t)status);
        serial_write("\n");
        report_boot_error("virtual page mapping self-test failed");
    }

    serial_write("DODO: virtual page mapping self-check passed; VA = ");
    serial_write_hex64(mapped_virtual_address);
    serial_write("\n");

    /*
     * Security regressions: a present leaf must never be silently replaced,
     * and non-canonical addresses must be rejected before any table mutation.
     */
    uint64_t frames_used = 0;
    const uint64_t existing_flags = DODO_PAGE_WRITABLE |
        (dodo_x86_64_nx_enabled() ? DODO_PAGE_NO_EXECUTE : 0u);
    status = dodo_x86_64_map_page(
        mapped_virtual_address, bootstrap_reserved_frames[3],
        existing_flags, NULL, 0, &frames_used);
    if (status != DODO_PAGING_ALREADY_MAPPED || frames_used != 0u) {
        report_boot_error("mapper did not protect an existing leaf mapping");
    }
    serial_write("DODO: existing mapping protection self-check passed\n");

    status = dodo_x86_64_map_page(
        UINT64_C(0x0000800000000000), bootstrap_reserved_frames[4],
        0, NULL, 0, &frames_used);
    if (status != DODO_PAGING_INVALID_ADDRESS || frames_used != 0u) {
        report_boot_error("mapper accepted a non-canonical virtual address");
    }
    serial_write("DODO: noncanonical address rejection self-check passed\n");

    /*
     * Map an additional page read-only. The direct-map alias remains writable,
     * so the contents can be initialized without violating the test mapping.
     */
    const uint64_t read_only_virtual_address =
        mapped_virtual_address + DODO_PAGE_SIZE;
    const uint64_t read_only_flags =
        dodo_x86_64_nx_enabled() ? DODO_PAGE_NO_EXECUTE : 0u;
    status = dodo_x86_64_map_page(
        read_only_virtual_address, bootstrap_reserved_frames[4],
        read_only_flags, NULL, 0, &frames_used);
    if (status != DODO_PAGING_OK || frames_used != 0u) {
        serial_write("DODO: read-only mapping error code = ");
        serial_write_u64((uint64_t)status);
        serial_write("\n");
        report_boot_error("read-only page mapping failed");
    }

    volatile uint8_t *direct_alias = (volatile uint8_t *)
        dodo_x86_64_hhdm_pointer(bootstrap_reserved_frames[4]);
    if (direct_alias == NULL) {
        report_boot_error("cannot access test frame through HHDM");
    }

    volatile uint64_t *direct_word =
        (volatile uint64_t *)(direct_alias + sizeof(uint64_t));
    volatile uint64_t *read_only_word =
        (volatile uint64_t *)(uintptr_t)
            (read_only_virtual_address + sizeof(uint64_t));
    const uint64_t expected_value = UINT64_C(0x61c0ffee5a17b00b);
    *direct_word = expected_value;

    /* RET opcode: an NX page may be read but must not be instruction-fetched. */
    direct_alias[0] = UINT8_C(0xc3);

    if (*read_only_word != expected_value) {
        report_boot_error("read-only alias did not read the mapped physical page");
    }

    expected_page_fault_address = (uint64_t)(uintptr_t)read_only_word;
    expected_page_fault_kind = DODO_EXPECT_WRITE_FAULT;
    page_fault_selftest_armed = 1;
    const int write_result = dodo_x86_64_test_write_readonly(read_only_word);
    if (page_fault_selftest_armed != 0u || write_result != 0) {
        report_boot_error("read-only page write protection was not enforced");
    }
    serial_write("DODO: read-only page protection self-check passed\n");

    if (dodo_x86_64_nx_enabled()) {
        expected_page_fault_address = read_only_virtual_address;
        expected_page_fault_kind = DODO_EXPECT_EXECUTE_FAULT;
        page_fault_selftest_armed = 1;
        const int execute_result = dodo_x86_64_test_execute_nx(
            (void *)(uintptr_t)read_only_virtual_address);
        if (page_fault_selftest_armed != 0u || execute_result != 0) {
            report_boot_error("NX execution protection was not enforced");
        }
        serial_write("DODO: NX execution protection self-check passed\n");
    } else {
        serial_write("DODO: NX protection unavailable; execution test skipped\n");
    }
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
    test_virtual_mapping();
    test_exception_handler();
    serial_write("DODO: all kernel bootstrap self-checks passed\n");
    kernel_halt();
}
