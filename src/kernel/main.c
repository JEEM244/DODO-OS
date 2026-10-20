#include <stddef.h>
#include <stdint.h>

#include "dodo/arch/x86_64/gdt.h"
#include "dodo/arch/x86_64/interrupts.h"
#include "dodo/arch/x86_64/timer.h"
#include "dodo/arch/x86_64/paging.h"
#include "dodo/arch/x86_64/user_mode.h"
#include "dodo/boot/limine_protocol.h"
#include "dodo/mm/phys.h"

#define COM1_PORT 0x3F8u
#define DODO_BOOT_TEST_FRAME_COUNT 5u

static void serial_write_hex64(uint64_t value);
__attribute__((noreturn)) static void report_boot_error(const char *message);

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
static uint64_t shared_kernel_test_virtual_address = 0;
static volatile uint64_t breakpoint_selftest_armed = 0;
static volatile uint64_t page_fault_selftest_armed = 0;
static volatile uint64_t expected_page_fault_address = 0;
static volatile uint64_t expected_page_fault_kind = 0;
static volatile uint64_t user_mode_test_armed = 0;
static volatile uint64_t user_mode_return_marker = 0;
static volatile uint64_t user_supervisor_read_test_passed = 0;
static volatile uint64_t user_cross_root_isolation_passed = 0;
static volatile uint64_t user_write_syscall_passed = 0;
static volatile uint64_t user_invalid_pointer_denied = 0;
static volatile uint64_t user_invalid_length_denied = 0;
static volatile uint64_t user_zero_length_denied = 0;
static volatile uint64_t user_address_limit_denied = 0;
static volatile uint64_t user_unmapped_pointer_denied = 0;
static volatile uint64_t user_unknown_syscall_denied = 0;
static volatile uint64_t user_cross_page_write_passed = 0;

#define DODO_EXPECT_WRITE_FAULT UINT64_C(1)
#define DODO_EXPECT_EXECUTE_FAULT UINT64_C(2)
#define DODO_EXPECT_USER_SUPERVISOR_FAULT UINT64_C(3)
#define DODO_EXPECT_USER_UNMAPPED_FAULT UINT64_C(4)

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

struct dodo_paging_frame_context {
    struct dodo_phys_allocator *allocator;
    uint64_t allocation_limit;
    uint64_t successful_allocations;
};

static int allocate_paging_frame(void *context, uint64_t *physical_address) {
    if (context == NULL || physical_address == NULL) {
        return 0;
    }

    struct dodo_paging_frame_context *frame_context =
        (struct dodo_paging_frame_context *)context;
    if (frame_context->allocator == NULL ||
        frame_context->successful_allocations >=
            frame_context->allocation_limit) {
        return 0;
    }

    if (dodo_phys_alloc_page(frame_context->allocator, physical_address) !=
        DODO_PHYS_OK) {
        return 0;
    }

    frame_context->successful_allocations++;
    return 1;
}

static int release_paging_frame(void *context, uint64_t physical_address) {
    if (context == NULL) {
        return 0;
    }

    struct dodo_paging_frame_context *frame_context =
        (struct dodo_paging_frame_context *)context;
    if (frame_context->allocator == NULL) {
        return 0;
    }

    return dodo_phys_free_page(frame_context->allocator, physical_address) ==
        DODO_PHYS_OK;
}

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



static void kernel_halt(void) __attribute__((noreturn));

static int serial_getc(void) {
    for (;;) {
        if ((inb((uint16_t)(COM1_PORT + 5u)) & 0x01u) != 0u) {
            return (int)inb((uint16_t)(COM1_PORT + 0u));
        }
        __asm__ volatile ("pause");
    }
}

static int string_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL) {
        return 0;
    }
    while (*left != '\0' && *right != '\0') {
        if (*left++ != *right++) {
            return 0;
        }
    }
    return *left == '\0' && *right == '\0';
}

static int parse_unsigned_decimal(const char *text, uint64_t *value) {
    uint64_t result = 0;
    int found_digit = 0;
    if (text == NULL || value == NULL) {
        return 0;
    }
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    while (*text >= '0' && *text <= '9') {
        const uint64_t digit = (uint64_t)(*text - '0');
        if (result > (UINT64_MAX - digit) / 10u) {
            return 0;
        }
        result = result * 10u + digit;
        found_digit = 1;
        ++text;
    }
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    if (!found_digit || *text != '\0') {
        return 0;
    }
    *value = result;
    return 1;
}

static void serial_readline(char *buffer, size_t capacity) {
    size_t length = 0;
    static int skip_lf_after_cr = 0;
    if (buffer == NULL || capacity == 0u) {
        return;
    }
    buffer[0] = '\0';
    for (;;) {
        const int input = serial_getc();
        const char ch = (char)input;
        if (skip_lf_after_cr != 0) {
            skip_lf_after_cr = 0;
            if (ch == '\n') {
                continue;
            }
        }
        if (ch == '\r' || ch == '\n') {
            if (ch == '\r') {
                skip_lf_after_cr = 1;
            }
            serial_write("\n");
            buffer[length] = '\0';
            return;
        }
        if (ch == '\b' || input == 0x7f) {
            if (length > 0u) {
                --length;
                buffer[length] = '\0';
                serial_write("\b \b");
            }
            continue;
        }
        if (input == 0x03) {
            length = 0;
            buffer[0] = '\0';
            serial_write("^C\n");
            serial_write("dodo> ");
            continue;
        }
        if (input >= 0x20 && input <= 0x7e && length + 1u < capacity) {
            buffer[length++] = ch;
            buffer[length] = '\0';
            serial_putc(ch);
        }
    }
}

static void run_serial_shell(void) __attribute__((noreturn));

static void run_serial_shell(void) {
    char line[128];
    serial_write("\nDODO OS interactive serial console\n");
    serial_write("Type 'help' to see available commands.\n\n");
    for (;;) {
        serial_write("dodo> ");
        serial_readline(line, sizeof(line));
        char *command = line;
        while (*command == ' ' || *command == '\t') {
            ++command;
        }
        if (*command == '\0') {
            continue;
        }
        if (string_equal(command, "help")) {
            serial_write("Commands:\n");
            serial_write("  help     Show this command list\n");
            serial_write("  about    Explain the current build\n");
            serial_write("  version  Show the kernel prototype version\n");
            serial_write("  mem      Show physical allocator counters\n");
            serial_write("  memcheck Exercise physical page allocate/free/reuse\n");
            serial_write("  uptime   Show ticks and approximate uptime\n");
            serial_write("  sleep N  Sleep for 1-60 seconds using the PIT timer\n");
            serial_write("  echo TXT Print TXT back to the console\n");
            serial_write("  clear    Clear a compatible terminal\n");
            serial_write("  halt     Stop the virtual CPU\n");
            serial_write("Input currently uses the QEMU serial terminal.\n");
        } else if (string_equal(command, "about")) {
            serial_write("DODO is an experimental x86-64 operating-system kernel.\n");
            serial_write("This build has tested memory mapping and ring-3 isolation probes.\n");
            serial_write("It is not yet a daily-use OS and has no disk filesystem or GUI.\n");
        } else if (string_equal(command, "version")) {
            serial_write("DODO OS kernel prototype 0.1.0\n");
            serial_write("Architecture: x86_64 | Boot: Limine BIOS/UEFI ISO\n");
        } else if (string_equal(command, "mem")) {
            serial_write("Physical memory allocator:\n  free pages:      ");
            serial_write_u64(physical_allocator.free_pages);
            serial_write("\n  allocated pages: ");
            serial_write_u64(physical_allocator.allocated_pages);
            serial_write("\n  known regions:   ");
            serial_write_u64(physical_allocator.region_count);
            serial_write("\n  page size:       4096 bytes\n");
        } else if (string_equal(command, "memcheck")) {
            const uint64_t free_before = physical_allocator.free_pages;
            const uint64_t allocated_before = physical_allocator.allocated_pages;
            uint64_t first_frame = 0;
            uint64_t reused_frame = 0;
            enum dodo_phys_status status =
                dodo_phys_alloc_page(&physical_allocator, &first_frame);
            if (status != DODO_PHYS_OK) {
                serial_write("memcheck failed: page allocation returned status ");
                serial_write_u64((uint64_t)status);
                serial_write("\n");
            } else {
                status = dodo_phys_free_page(&physical_allocator, first_frame);
                if (status != DODO_PHYS_OK) {
                    report_boot_error("memcheck could not free its test frame");
                }
                status = dodo_phys_alloc_page(&physical_allocator, &reused_frame);
                if (status != DODO_PHYS_OK) {
                    report_boot_error("memcheck could not reuse its released frame");
                }
                if (reused_frame != first_frame) {
                    report_boot_error("physical allocator did not reuse the most recently freed frame");
                }
                status = dodo_phys_free_page(&physical_allocator, reused_frame);
                if (status != DODO_PHYS_OK) {
                    report_boot_error("memcheck could not return its reused frame");
                }
                if (physical_allocator.free_pages != free_before ||
                    physical_allocator.allocated_pages != allocated_before) {
                    report_boot_error("memcheck changed physical allocator accounting");
                }
                serial_write("Physical allocator check passed.\n  frame allocated, freed, reused, and released: ");
                serial_write_hex64(first_frame);
                serial_write("\n  allocator counters unchanged\n");
            }
        } else if (string_equal(command, "uptime")) {
            const uint64_t ticks = dodo_x86_64_timer_ticks();
            serial_write("Timer frequency: 100 Hz\nTimer ticks: ");
            serial_write_u64(ticks);
            serial_write("\nUptime: ");
            serial_write_u64(ticks / DODO_X86_64_TIMER_HZ);
            serial_write(" seconds (approximate)\n");
        } else if (command[0] == 's' && command[1] == 'l' &&
                   command[2] == 'e' && command[3] == 'e' &&
                   command[4] == 'p' &&
                   (command[5] == ' ' || command[5] == '\t' ||
                    command[5] == '\0')) {
            uint64_t seconds = 0;
            if (!parse_unsigned_decimal(command + 5, &seconds) ||
                seconds < 1u || seconds > 60u) {
                serial_write("Usage: sleep 1-60\n");
            } else {
                const uint64_t start_ticks = dodo_x86_64_timer_ticks();
                const uint64_t wait_ticks = seconds * DODO_X86_64_TIMER_HZ;
                serial_write("Sleeping for ");
                serial_write_u64(seconds);
                serial_write(" seconds...\n");
                while ((dodo_x86_64_timer_ticks() - start_ticks) < wait_ticks) {
                    __asm__ volatile ("hlt" : : : "memory");
                }
                serial_write("Sleep complete.\n");
            }
        } else if (command[0] == 'e' && command[1] == 'c' &&
                   command[2] == 'h' && command[3] == 'o' &&
                   (command[4] == ' ' || command[4] == '\0')) {
            const char *message = command + 4;
            if (*message == ' ') {
                ++message;
            }
            serial_write(message);
            serial_write("\n");
        } else if (string_equal(command, "clear")) {
            serial_write("\x1b[2J\x1b[H");
        } else if (string_equal(command, "halt")) {
            serial_write("DODO: CPU halted by console command.\n");
            kernel_halt();
        } else {
            serial_write("Unknown command: ");
            serial_write(command);
            serial_write("\nType 'help' for available commands.\n");
        }
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

/*
 * One terminal failure path for boot-time errors and unexpected CPU faults.
 * It deliberately avoids allocation, formatting libraries, and locks so it
 * remains usable while the kernel is only partially initialized.
 */
__attribute__((noreturn))
static void kernel_panic(
    const char *reason,
    const struct dodo_x86_64_exception_frame *frame) {
    __asm__ volatile ("cli" : : : "memory");
    serial_write("\n\n=== DODO KERNEL PANIC ===\n");
    serial_write("Reason: ");
    serial_write(reason != NULL ? reason : "unspecified fatal error");
    serial_write("\n");

    if (frame != NULL) {
        serial_write("Vector: ");
        serial_write_u64(frame->vector);
        serial_write("\nError:  ");
        serial_write_hex64(frame->error_code);
        serial_write("\nRIP:    ");
        serial_write_hex64(frame->rip);
        serial_write("\nCS:     ");
        serial_write_hex64(frame->cs);
        serial_write("\nRFLAGS: ");
        serial_write_hex64(frame->rflags);
        serial_write("\n");

        if (frame->vector == 14u) {
            uint64_t fault_address;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
            serial_write("CR2:    ");
            serial_write_hex64(fault_address);
            serial_write("\n");
        }
    }

    serial_write("System halted. Reboot the VM after collecting this report.\n");
    kernel_halt();
}

__attribute__((noreturn))
static void report_boot_error(const char *message) {
    kernel_panic(message, NULL);
}

int dodo_x86_64_exception_handler(
    struct dodo_x86_64_exception_frame *frame) {
    if (frame == NULL) {
        kernel_panic("null CPU exception frame", NULL);
    }

    /*
     * This DPL3 gate exists only for the ring-3 architecture self-test.
     * Returning to CPL0 targets a trampoline that restores the saved kernel
     * stack, because IRETQ to the same privilege level doesn't pop user RSP/SS.
     */
    if (frame->vector == DODO_X86_64_USER_TEST_VECTOR) {
        if ((frame->cs & UINT64_C(3)) != UINT64_C(3) ||
            user_mode_test_armed == 0u) {
            kernel_panic("unexpected vector-128 software interrupt", frame);
        }

        /*
         * Minimal syscall ABI for this prototype:
         *   RAX = WRITE, RDI = user pointer, RSI = byte length (1..96)
         * The pointer is copied only after every active-CR3 paging level is
         * present and user-accessible. No user pointer is directly dereferenced.
         */
        if (frame->rax == DODO_X86_64_SYSCALL_WRITE) {
            if (frame->rsi == 0u ||
                frame->rsi > DODO_X86_64_SYSCALL_MAX_WRITE) {
                if (frame->rdi < DODO_X86_64_USER_ADDRESS_LIMIT &&
                    frame->rsi == DODO_X86_64_SYSCALL_MAX_WRITE + 1u) {
                    user_invalid_length_denied = 1;
                    serial_write("DODO: oversized user write length rejected\n");
                } else if (frame->rsi == 0u &&
                           frame->rdi < DODO_X86_64_USER_ADDRESS_LIMIT) {
                    user_zero_length_denied = 1;
                    serial_write("DODO: zero-length user write rejected\n");
                }
                frame->rax = DODO_X86_64_SYSCALL_ERROR;
                return DODO_EXCEPTION_RESUME;
            }

            char kernel_buffer[DODO_X86_64_SYSCALL_MAX_WRITE + 1u];
            const enum dodo_paging_status copy_status =
                dodo_x86_64_copy_from_active_user(
                    kernel_buffer, frame->rdi, frame->rsi);
            if (copy_status != DODO_PAGING_OK) {
                if (frame->rdi == UINT64_C(0xffffffff80000000) &&
                    frame->rsi == 1u) {
                    user_invalid_pointer_denied = 1;
                    serial_write("DODO: invalid user syscall pointer rejected\n");
                } else if (frame->rdi ==
                               DODO_X86_64_USER_ADDRESS_LIMIT - 1u &&
                           frame->rsi == 2u) {
                    user_address_limit_denied = 1;
                    serial_write("DODO: user buffer crossing address limit rejected\n");
                } else if (frame->rdi < DODO_X86_64_USER_ADDRESS_LIMIT &&
                           frame->rsi == 1u) {
                    user_unmapped_pointer_denied = 1;
                    serial_write("DODO: unmapped user pointer rejected\n");
                }
                frame->rax = DODO_X86_64_SYSCALL_ERROR;
                return DODO_EXCEPTION_RESUME;
            }

            if ((frame->rdi & (DODO_PAGE_SIZE - 1u)) + frame->rsi >
                DODO_PAGE_SIZE) {
                user_cross_page_write_passed = 1;
                serial_write("DODO: user write crossed 4 KiB boundary\n");
            }

            kernel_buffer[frame->rsi] = '\0';
            serial_write("DODO USER: ");
            serial_write(kernel_buffer);
            serial_write("\n");
            user_write_syscall_passed = 1;
            frame->rax = frame->rsi;
            return DODO_EXCEPTION_RESUME;
        }

        if (frame->rax == DODO_X86_64_SYSCALL_EXIT) {
            user_mode_test_armed = 0;
            user_mode_return_marker = frame->rdi;
            serial_write("DODO: userspace exit syscall reached kernel\n");
            return DODO_EXCEPTION_RETURN_TO_KERNEL;
        }

        /* Unknown syscall numbers are rejected without panicking the kernel. */
        if (frame->rax == DODO_X86_64_SYSCALL_UNKNOWN_TEST) {
            user_unknown_syscall_denied = 1;
            serial_write("DODO: unknown user syscall rejected\n");
        }
        frame->rax = DODO_X86_64_SYSCALL_ERROR;
        return DODO_EXCEPTION_RESUME;
    }

    if (frame->vector == 32u) {
        /* PIT IRQ0: keep the handler short and acknowledge the master PIC. */
        dodo_x86_64_timer_handle_irq();
        return DODO_EXCEPTION_RESUME;
    }

    if (frame->vector >= 32u) {
        kernel_panic("invalid CPU exception vector", frame);
    }

    if (frame->vector == 3u && breakpoint_selftest_armed != 0u) {
        breakpoint_selftest_armed = 0;
        serial_write("DODO: IDT breakpoint self-check passed; RIP=");
        serial_write_hex64(frame->rip);
        serial_write("\n");
        return 1;
    }

    /*
     * Only resume a page fault when it occurred at the exact address and with
     * access bits expected by the explicitly armed test. Every other fault is
     * fatal, including any unexpected page fault while executing user code.
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

            if (expected_page_fault_kind == DODO_EXPECT_USER_SUPERVISOR_FAULT &&
                (frame->cs & UINT64_C(3)) == UINT64_C(3) &&
                (error & UINT64_C(0x7)) == UINT64_C(0x5)) {
                page_fault_selftest_armed = 0;
                user_supervisor_read_test_passed = 1;
                serial_write("DODO: ring-3 read of supervisor memory was blocked\n");
                return DODO_EXCEPTION_RETURN_TO_KERNEL;
            }

            if (expected_page_fault_kind == DODO_EXPECT_USER_UNMAPPED_FAULT &&
                (frame->cs & UINT64_C(3)) == UINT64_C(3) &&
                (error & UINT64_C(0x7)) == UINT64_C(0x4)) {
                page_fault_selftest_armed = 0;
                user_cross_root_isolation_passed = 1;
                serial_write("DODO: second user root could not read first root mapping\n");
                return DODO_EXCEPTION_RETURN_TO_KERNEL;
            }
        }
    }

    kernel_panic(exception_names[frame->vector], frame);
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

static void reclaim_bootloader_memory(void) {
    struct dodo_limine_memmap_response *response =
        limine_memmap_request.response;
    if (response == NULL ||
        response->entry_count > DODO_PHYS_MAX_REGIONS ||
        (response->entry_count != 0u && response->entries == NULL)) {
        report_boot_error("cannot safely inspect the final bootloader memory map");
    }

    const uint64_t free_pages_before = physical_allocator.free_pages;
    uint64_t reclaimed_pages = 0;
    uint64_t reclaimed_regions = 0;
    uint64_t first_reclaimed_region_index = UINT64_MAX;
    uint64_t first_reclaimed_region_base = 0;
    uint64_t first_reclaimed_region_end = 0;

    /*
     * Do this only after switching to DODO's cloned page-table tree and
     * installing DODO's GDT/IDT and kernel stack. Limine's boot tables, GDT,
     * stack, and response structures live in bootloader-reclaimable memory.
     * No pointer from the response may be dereferenced after this function.
     */
    for (uint64_t i = 0; i < response->entry_count; ++i) {
        struct dodo_limine_memmap_entry *entry = response->entries[i];
        if (entry == NULL) {
            report_boot_error("memory map contains a null entry during reclamation");
        }

        if (entry->type != DODO_LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
            entry->length == 0u) {
            continue;
        }

        if (first_reclaimed_region_index == UINT64_MAX) {
            first_reclaimed_region_index = physical_allocator.region_count;
            first_reclaimed_region_base = entry->base;
            first_reclaimed_region_end = entry->base + entry->length;
        }

        const enum dodo_phys_status status = dodo_phys_add_region(
            &physical_allocator, entry->base, entry->length);
        if (status != DODO_PHYS_OK) {
            serial_write("DODO: reclaimed-region validation error = ");
            serial_write_u64((uint64_t)status);
            serial_write("\n");
            report_boot_error("could not safely reclaim bootloader memory region");
        }

        const uint64_t region_pages = entry->length / DODO_PHYS_PAGE_SIZE;
        if (region_pages > UINT64_MAX - reclaimed_pages) {
            report_boot_error("reclaimed page count overflowed");
        }
        reclaimed_pages += region_pages;
        reclaimed_regions++;
    }

    if (reclaimed_regions == 0u ||
        reclaimed_pages > UINT64_MAX - free_pages_before ||
        physical_allocator.free_pages != free_pages_before + reclaimed_pages) {
        report_boot_error("bootloader memory reclamation count did not validate");
    }

    /*
     * Prove the new range isn't only being counted: allocate one page directly
     * from the first adopted region, then return it. The normal allocator's
     * free stack should be able to reuse that frame afterward.
     */
    const uint64_t free_pages_before_probe = physical_allocator.free_pages;
    const uint64_t allocated_pages_before_probe = physical_allocator.allocated_pages;
    uint64_t reclaimed_probe_page = UINT64_MAX;
    const enum dodo_phys_status probe_status =
        dodo_phys_alloc_page_from_region(
            &physical_allocator, first_reclaimed_region_index,
            &reclaimed_probe_page);
    if (probe_status != DODO_PHYS_OK ||
        reclaimed_probe_page < first_reclaimed_region_base ||
        reclaimed_probe_page >= first_reclaimed_region_end ||
        (reclaimed_probe_page & (DODO_PHYS_PAGE_SIZE - 1u)) != 0u) {
        report_boot_error("reclaimed region failed its direct allocation self-check");
    }

    if (dodo_phys_free_page(&physical_allocator, reclaimed_probe_page) !=
        DODO_PHYS_OK ||
        physical_allocator.free_pages != free_pages_before_probe ||
        physical_allocator.allocated_pages != allocated_pages_before_probe) {
        report_boot_error("reclaimed region probe failed to restore allocator counts");
    }

    serial_write("DODO: reclaimed region allocation self-check passed; page = ");
    serial_write_hex64(reclaimed_probe_page);
    serial_write("\n");

    serial_write("DODO: bootloader reclaimable regions adopted = ");
    serial_write_u64(reclaimed_regions);
    serial_write("\nDODO: bootloader reclaimable pages returned to allocator = ");
    serial_write_u64(reclaimed_pages);
    serial_write("\n");

    /*
     * These protocol responses lived in the reclaimable regions we just added.
     * Null the references as a guard against accidentally using freed metadata.
     * The HHDM offset has already been copied into the paging subsystem.
     */
    limine_memmap_request.response = NULL;
    limine_hhdm_request.response = NULL;
    serial_write("DODO: Limine boot response pointers invalidated\n");
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

    /*
     * First inject a deterministic allocation failure into a disposable clone.
     * The CR3 root must stay untouched and every temporary frame must return to
     * the allocator before we try the real page-table clone.
     */
    const uint64_t free_pages_before_failure = physical_allocator.free_pages;
    const uint64_t allocated_pages_before_failure =
        physical_allocator.allocated_pages;
    struct dodo_paging_frame_context failure_context = {
        .allocator = &physical_allocator,
        .allocation_limit = 2,
        .successful_allocations = 0
    };
    uint64_t discarded_root = 0;
    uint64_t discarded_tables = 0;
    status = dodo_x86_64_paging_clone_tree(
        allocate_paging_frame, release_paging_frame, &failure_context,
        &discarded_root, &discarded_tables);
    if (status != DODO_PAGING_NO_TABLE_FRAMES ||
        physical_allocator.free_pages != free_pages_before_failure ||
        physical_allocator.allocated_pages != allocated_pages_before_failure) {
        report_boot_error("failed page-table clone did not roll back all frames");
    }
    serial_write("DODO: page-table clone rollback self-check passed\n");

    /*
     * Clone the complete page-table hierarchy into DODO-owned frames. The leaf
     * mappings keep their physical targets, so kernel text, stack, HHDM,
     * response structures and IDT remain reachable across the CR3 switch.
     */
    struct dodo_paging_frame_context normal_context = {
        .allocator = &physical_allocator,
        .allocation_limit = UINT64_MAX,
        .successful_allocations = 0
    };
    uint64_t new_root_physical = 0;
    uint64_t tables_cloned = 0;
    status = dodo_x86_64_paging_clone_tree(
        allocate_paging_frame, release_paging_frame, &normal_context,
        &new_root_physical, &tables_cloned);
    if (status != DODO_PAGING_OK) {
        serial_write("DODO: page-table tree clone error code = ");
        serial_write_u64((uint64_t)status);
        serial_write("\n");
        report_boot_error("could not clone and activate DODO's page-table tree");
    }

    serial_write("DODO: DODO-owned page-table tree installed; root = ");
    serial_write_hex64(new_root_physical);
    serial_write("\nDODO: page-table frames cloned = ");
    serial_write_u64(tables_cloned);
    serial_write("\n");

    /*
     * The paging self-test writes to its fourth frame through a virtual alias.
     * Supplying the active root as that data frame must be rejected before any
     * writes occur, and the caller's output must remain untouched on failure.
     */
    const uint64_t invalid_self_test_frames[4] = {
        bootstrap_reserved_frames[0],
        bootstrap_reserved_frames[1],
        bootstrap_reserved_frames[2],
        new_root_physical
    };
    uint64_t rejected_test_address = UINT64_MAX;
    status = dodo_x86_64_paging_self_test(
        invalid_self_test_frames, &rejected_test_address);
    if (status != DODO_PAGING_INVALID_ADDRESS ||
        rejected_test_address != UINT64_MAX) {
        report_boot_error("paging self-test accepted the live root as its data frame");
    }
    serial_write("DODO: live-root data-frame rejection self-check passed\n");

    uint64_t mapped_virtual_address = 0;
    status = dodo_x86_64_paging_self_test(
        bootstrap_reserved_frames, &mapped_virtual_address);
    if (status != DODO_PAGING_OK) {
        serial_write("DODO: virtual mapping test error code = ");
        serial_write_u64((uint64_t)status);
        serial_write("\n");
        report_boot_error("virtual page mapping self-test failed");
    }

    shared_kernel_test_virtual_address = mapped_virtual_address;
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

static void emit_user_test_byte(
    uint8_t *buffer, size_t capacity, size_t *length, uint8_t value) {
    if (*length >= capacity) {
        report_boot_error("userspace test bytecode exceeded its fixed buffer");
    }
    buffer[(*length)++] = value;
}

static void emit_user_test_u32(
    uint8_t *buffer, size_t capacity, size_t *length, uint32_t value) {
    for (uint64_t i = 0; i < 4u; ++i) {
        emit_user_test_byte(buffer, capacity, length,
            (uint8_t)(value >> (i * 8u)));
    }
}

static void emit_user_test_u64(
    uint8_t *buffer, size_t capacity, size_t *length, uint64_t value) {
    for (uint64_t i = 0; i < 8u; ++i) {
        emit_user_test_byte(buffer, capacity, length,
            (uint8_t)(value >> (i * 8u)));
    }
}

static void test_user_mode_transitions(void) {
    const uint64_t free_pages_before_user_tests = physical_allocator.free_pages;
    const uint64_t allocated_pages_before_user_tests =
        physical_allocator.allocated_pages;

    uint64_t user_root_frame = 0;
    uint64_t user_code_frame = 0;
    uint64_t user_stack_frame = 0;
    uint64_t user_table_frames[3] = { 0, 0, 0 };

    if (dodo_phys_alloc_page(&physical_allocator, &user_root_frame) != DODO_PHYS_OK ||
        dodo_phys_alloc_page(&physical_allocator, &user_code_frame) != DODO_PHYS_OK ||
        dodo_phys_alloc_page(&physical_allocator, &user_stack_frame) != DODO_PHYS_OK) {
        report_boot_error("could not allocate user root/code/stack frames");
    }
    for (uint64_t i = 0; i < 3u; ++i) {
        if (dodo_phys_alloc_page(&physical_allocator, &user_table_frames[i]) !=
            DODO_PHYS_OK) {
            report_boot_error("could not allocate user page-table frames");
        }
    }

    enum dodo_paging_status paging_status =
        dodo_x86_64_paging_create_user_root(user_root_frame);
    if (paging_status != DODO_PAGING_OK) {
        serial_write("DODO: user root creation error = ");
        serial_write_u64((uint64_t)paging_status);
        serial_write("\n");
        report_boot_error("could not create isolated user page-table root");
    }

    const uint64_t kernel_root_frame = dodo_x86_64_kernel_root_physical();
    if (kernel_root_frame == 0u ||
        dodo_x86_64_paging_switch_root(user_root_frame) != DODO_PAGING_OK) {
        report_boot_error("could not activate isolated user root for mapping setup");
    }

    uint64_t user_region_base = 0;
    paging_status = dodo_x86_64_find_unused_user_region(&user_region_base);
    if (paging_status != DODO_PAGING_OK) {
        serial_write("DODO: user region selection error = ");
        serial_write_u64((uint64_t)paging_status);
        serial_write("\n");
        report_boot_error("could not find an empty user PML4 slot");
    }

    volatile uint8_t *user_code =
        (volatile uint8_t *)dodo_x86_64_hhdm_pointer(user_code_frame);
    volatile uint8_t *user_stack =
        (volatile uint8_t *)dodo_x86_64_hhdm_pointer(user_stack_frame);
    volatile uint64_t *user_root_table =
        (volatile uint64_t *)dodo_x86_64_hhdm_pointer(user_root_frame);
    volatile uint64_t *kernel_root_table =
        (volatile uint64_t *)dodo_x86_64_hhdm_pointer(kernel_root_frame);
    if (user_code == NULL || user_stack == NULL ||
        user_root_table == NULL || kernel_root_table == NULL) {
        report_boot_error("cannot inspect or initialize address-space frames through HHDM");
    }

    for (uint64_t i = 0; i < DODO_PAGE_SIZE; ++i) {
        user_code[i] = 0;
        user_stack[i] = 0;
    }

    const uint64_t user_code_virtual = user_region_base;
    const uint64_t user_stack_virtual = user_region_base + DODO_PAGE_SIZE;
    uint64_t frames_used = 0;
    paging_status = dodo_x86_64_map_page(
        user_code_virtual, user_code_frame, DODO_PAGE_USER_ACCESSIBLE,
        user_table_frames, 3u, &frames_used);
    if (paging_status != DODO_PAGING_OK || frames_used != 3u) {
        serial_write("DODO: user code mapping error = ");
        serial_write_u64((uint64_t)paging_status);
        serial_write("\n");
        report_boot_error("could not create a user executable page mapping");
    }

    const uint64_t user_stack_flags = DODO_PAGE_USER_ACCESSIBLE |
        DODO_PAGE_WRITABLE |
        (dodo_x86_64_nx_enabled() ? DODO_PAGE_NO_EXECUTE : 0u);
    paging_status = dodo_x86_64_map_page(
        user_stack_virtual, user_stack_frame, user_stack_flags,
        NULL, 0, &frames_used);
    if (paging_status != DODO_PAGING_OK || frames_used != 0u) {
        serial_write("DODO: user stack mapping error = ");
        serial_write_u64((uint64_t)paging_status);
        serial_write("\n");
        report_boot_error("could not create a user stack mapping");
    }

    /*
     * Prove the low-half mapping was added only to the user root. The kernel
     * root must still have no entry for this PML4 slot, while the user root's
     * entry must be present and marked U/S.
     */
    const uint64_t user_pml4_index =
        (user_code_virtual >> 39) & UINT64_C(0x1ff);
    if (user_pml4_index >= UINT64_C(256) ||
        kernel_root_table[user_pml4_index] != 0u ||
        (user_root_table[user_pml4_index] & UINT64_C(1)) == 0u ||
        (user_root_table[user_pml4_index] & (UINT64_C(1) << 2)) == 0u) {
        report_boot_error("user mapping leaked into kernel root or lacks U/S permission");
    }
    serial_write("DODO: separate user root mapping self-check passed\n");

    /*
     * Assemble a tiny userspace program for the current test ABI:
     *   write(user_message, len)  -> return len
     *   write(kernel_pointer, 1)  -> return ERROR, no dereference
     *   write(user_message, 97)   -> return ERROR, length cap
     *   unknown syscall          -> return ERROR, kernel stays alive
     *   exit(7)                   -> return to the test harness with status 7
     *
     * This program is assembled from a few x86-64 opcodes in-kernel so this
     * milestone needs no userspace compiler or C runtime yet.
     */
    static const char user_message[] = "DODO says hello from CPL3";
    const uint64_t user_message_length = sizeof(user_message) - 1u;
    /* Start eight bytes before the page boundary to test a cross-page copy. */
    const uint64_t user_message_offset = DODO_PAGE_SIZE - UINT64_C(8);
    const uint64_t user_message_address =
        user_code_virtual + user_message_offset;
    uint8_t syscall_test_code[256] = { 0 };
    size_t code_length = 0;
    const size_t code_capacity = sizeof(syscall_test_code);

    /* write(user_message, message_length) */
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xb8); /* mov eax, imm32 */
    emit_user_test_u32(syscall_test_code, code_capacity, &code_length,
        (uint32_t)DODO_X86_64_SYSCALL_WRITE);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x48);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xbf); /* movabs rdi, imm64 */
    emit_user_test_u64(syscall_test_code, code_capacity, &code_length, user_message_address);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xbe); /* mov esi, imm32 */
    emit_user_test_u32(syscall_test_code, code_capacity, &code_length, (uint32_t)user_message_length);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xcd);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x80);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x83); /* cmp eax, message length */
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xf8);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, (uint8_t)user_message_length);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x74); /* je over UD2 on success */
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x02);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x0f);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x0b);

    /* write(kernel_pointer, 1) must be rejected before dereferencing the pointer. */
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x48);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xbf);
    emit_user_test_u64(syscall_test_code, code_capacity, &code_length,
        UINT64_C(0xffffffff80000000));
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xbe);
    emit_user_test_u32(syscall_test_code, code_capacity, &code_length, 1u);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xb8);
    emit_user_test_u32(syscall_test_code, code_capacity, &code_length,
        (uint32_t)DODO_X86_64_SYSCALL_WRITE);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xcd);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x80);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x48); /* cmp rax, -1 */
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x83);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xf8);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xff);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x74);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x02);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x0f);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x0b);

    /* write(user_message, MAX_WRITE + 1) must fail at the length check. */
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xb8);
    emit_user_test_u32(syscall_test_code, code_capacity, &code_length,
        (uint32_t)DODO_X86_64_SYSCALL_WRITE);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x48);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xbf);
    emit_user_test_u64(syscall_test_code, code_capacity, &code_length, user_message_address);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xbe);
    emit_user_test_u32(syscall_test_code, code_capacity, &code_length,
        (uint32_t)(DODO_X86_64_SYSCALL_MAX_WRITE + 1u));
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xcd);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x80);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x48);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x83);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xf8);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xff);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x74);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x02);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x0f);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x0b);

    /*
     * A two-byte user span beginning at USER_ADDRESS_LIMIT - 1 crosses from
     * the valid lower half into the non-user address range and must be rejected
     * before any page-table walk or byte copy.
     */
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xb8);
    emit_user_test_u32(syscall_test_code, code_capacity, &code_length,
        (uint32_t)DODO_X86_64_SYSCALL_WRITE);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x48);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xbf);
    emit_user_test_u64(syscall_test_code, code_capacity, &code_length,
        DODO_X86_64_USER_ADDRESS_LIMIT - 1u);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xbe);
    emit_user_test_u32(syscall_test_code, code_capacity, &code_length, 2u);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xcd);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x80);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x48);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x83);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xf8);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xff);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x74);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x02);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x0f);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x0b);

    /* A zero-length write is invalid in this small prototype ABI. */
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xb8);
    emit_user_test_u32(syscall_test_code, code_capacity, &code_length,
        (uint32_t)DODO_X86_64_SYSCALL_WRITE);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x48);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xbf);
    emit_user_test_u64(syscall_test_code, code_capacity, &code_length, user_message_address);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x31);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xf6);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xcd);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x80);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x48);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x83);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xf8);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xff);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x74);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x02);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x0f);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x0b);

    /*
     * Valid user-half address but no mapped leaf. Copy-in must fail without
     * dereferencing the pointer or faulting the kernel.
     */
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xb8);
    emit_user_test_u32(syscall_test_code, code_capacity, &code_length,
        (uint32_t)DODO_X86_64_SYSCALL_WRITE);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x48);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xbf);
    emit_user_test_u64(syscall_test_code, code_capacity, &code_length,
        user_code_virtual + 2u * DODO_PAGE_SIZE);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xbe);
    emit_user_test_u32(syscall_test_code, code_capacity, &code_length, 1u);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xcd);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x80);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x48);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x83);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xf8);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xff);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x74);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x02);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x0f);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x0b);

    /* Unknown syscall numbers must return ERROR rather than panic the kernel. */
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xb8);
    emit_user_test_u32(syscall_test_code, code_capacity, &code_length,
        (uint32_t)DODO_X86_64_SYSCALL_UNKNOWN_TEST);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xcd);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x80);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x48);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x83);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xf8);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xff);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x74);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x02);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x0f);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x0b);

    /* exit(7) returns to the boot harness and exposes the caller's status. */
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xb8);
    emit_user_test_u32(syscall_test_code, code_capacity, &code_length,
        (uint32_t)DODO_X86_64_SYSCALL_EXIT);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xbf);
    emit_user_test_u32(syscall_test_code, code_capacity, &code_length,
        (uint32_t)DODO_X86_64_USER_TEST_EXIT_STATUS);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0xcd);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x80);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x0f);
    emit_user_test_byte(syscall_test_code, code_capacity, &code_length, 0x0b);

    if (code_length >= user_message_offset ||
        user_message_offset + sizeof(user_message) > 2u * DODO_PAGE_SIZE) {
        report_boot_error("userspace test bytecode/data layout exceeds two mapped pages");
    }
    for (size_t i = 0; i < code_length; ++i) {
        user_code[i] = syscall_test_code[i];
    }
    for (size_t i = 0; i < sizeof(user_message); ++i) {
        const uint64_t absolute_offset = user_message_offset + i;
        if (absolute_offset < DODO_PAGE_SIZE) {
            user_code[absolute_offset] = (uint8_t)user_message[i];
        } else {
            user_stack[absolute_offset - DODO_PAGE_SIZE] =
                (uint8_t)user_message[i];
        }
    }

    /* Restore the kernel root before calling the assembly transition stub. */
    if (dodo_x86_64_paging_switch_root(kernel_root_frame) != DODO_PAGING_OK) {
        report_boot_error("could not restore kernel page-table root before CPL3 entry");
    }

    const uint64_t user_stack_pointer =
        user_stack_virtual + DODO_PAGE_SIZE - UINT64_C(16);
    user_mode_return_marker = UINT64_MAX;
    user_mode_test_armed = 1;
    user_write_syscall_passed = 0;
    user_invalid_pointer_denied = 0;
    user_invalid_length_denied = 0;
    user_zero_length_denied = 0;
    user_address_limit_denied = 0;
    user_unmapped_pointer_denied = 0;
    user_unknown_syscall_denied = 0;
    dodo_x86_64_enter_user(user_code_virtual, user_stack_pointer, user_root_frame);

    if (user_mode_test_armed != 0u ||
        user_mode_return_marker != DODO_X86_64_USER_TEST_EXIT_STATUS) {
        report_boot_error("ring-3 exit syscall or controlled interrupt return failed");
    }
    serial_write("DODO: userspace exit status verified\n");
    if (user_write_syscall_passed == 0u) {
        report_boot_error("valid user write syscall was not serviced");
    }
    serial_write("DODO: validated user write syscall self-check passed\n");
    if (user_cross_page_write_passed == 0u) {
        report_boot_error("user write crossing a page boundary was not verified");
    }
    serial_write("DODO: cross-page user write self-check passed\n");
    if (user_invalid_pointer_denied == 0u) {
        report_boot_error("kernel pointer passed to user syscall was not rejected");
    }
    serial_write("DODO: invalid user syscall pointer rejection self-check passed\n");
    if (user_address_limit_denied == 0u) {
        report_boot_error("user buffer crossing the lower-half limit was not rejected");
    }
    serial_write("DODO: user address-limit rejection self-check passed\n");
    if (user_unmapped_pointer_denied == 0u) {
        report_boot_error("unmapped user buffer was not rejected");
    }
    serial_write("DODO: unmapped user pointer rejection self-check passed\n");
    if (user_zero_length_denied == 0u) {
        report_boot_error("zero-length user write was not rejected");
    }
    serial_write("DODO: zero-length user write rejection self-check passed\n");
    if (user_invalid_length_denied == 0u) {
        report_boot_error("oversized user write length was not rejected");
    }
    serial_write("DODO: oversized user write length rejection self-check passed\n");
    if (user_unknown_syscall_denied == 0u) {
        report_boot_error("unknown userspace syscall was not rejected safely");
    }
    serial_write("DODO: unknown user syscall rejection self-check passed\n");
    serial_write("DODO: ring-3 entry and controlled return self-check passed\n");

    /*
     * Try to read supervisor-only kernel memory from CPL3. If the U/S bits are
     * correct, this read raises a protection page fault (P=1, W=0, U=1).
     * The handler returns to the kernel only for the exact expected CR2/error
     * code; executing the following UD2 indicates that isolation failed.
     *
     * movabs rax, 0xffffffff80000000; mov rax, [rax]; ud2
     */
    const uint8_t supervisor_read_test_code[] = {
        UINT8_C(0x48), UINT8_C(0xb8),
        UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x80),
        UINT8_C(0xff), UINT8_C(0xff), UINT8_C(0xff), UINT8_C(0xff),
        UINT8_C(0x48), UINT8_C(0x8b), UINT8_C(0x00),
        UINT8_C(0x0f), UINT8_C(0x0b)
    };
    for (uint64_t i = 0; i < DODO_PAGE_SIZE; ++i) {
        user_code[i] = 0;
    }
    for (size_t i = 0; i < sizeof(supervisor_read_test_code); ++i) {
        user_code[i] = supervisor_read_test_code[i];
    }

    expected_page_fault_address = UINT64_C(0xffffffff80000000);
    expected_page_fault_kind = DODO_EXPECT_USER_SUPERVISOR_FAULT;
    page_fault_selftest_armed = 1;
    user_supervisor_read_test_passed = 0;
    dodo_x86_64_enter_user(user_code_virtual, user_stack_pointer, user_root_frame);

    if (page_fault_selftest_armed != 0u ||
        user_supervisor_read_test_passed == 0u) {
        report_boot_error("user mode read a supervisor-only page or faulted unexpectedly");
    }
    serial_write("DODO: ring-3 supervisor-isolation self-check passed\n");

    /*
     * Build a second independent user root. It maps its own code at +2 pages
     * and stack at +3 pages, leaving the first root's code VA unmapped. The
     * test code in root B then tries to read root A's code VA; the expected
     * not-present user page fault proves that a low-half mapping did not leak
     * across CR3 roots.
     */
    uint64_t second_user_root_frame = 0;
    uint64_t second_user_code_frame = 0;
    uint64_t second_user_stack_frame = 0;
    uint64_t second_user_table_frames[3] = { 0, 0, 0 };

    if (dodo_phys_alloc_page(&physical_allocator, &second_user_root_frame) != DODO_PHYS_OK ||
        dodo_phys_alloc_page(&physical_allocator, &second_user_code_frame) != DODO_PHYS_OK ||
        dodo_phys_alloc_page(&physical_allocator, &second_user_stack_frame) != DODO_PHYS_OK) {
        report_boot_error("could not allocate frames for second user root");
    }
    for (uint64_t i = 0; i < 3u; ++i) {
        if (dodo_phys_alloc_page(&physical_allocator, &second_user_table_frames[i]) !=
            DODO_PHYS_OK) {
            report_boot_error("could not allocate second-root page tables");
        }
    }

    paging_status = dodo_x86_64_paging_create_user_root(second_user_root_frame);
    if (paging_status != DODO_PAGING_OK ||
        dodo_x86_64_paging_switch_root(second_user_root_frame) != DODO_PAGING_OK) {
        report_boot_error("could not create/activate second user root");
    }

    uint64_t second_user_region_base = 0;
    paging_status = dodo_x86_64_find_unused_user_region(&second_user_region_base);
    if (paging_status != DODO_PAGING_OK) {
        report_boot_error("could not select low-half region in second user root");
    }

    const uint64_t second_user_code_virtual =
        second_user_region_base + 2u * DODO_PAGE_SIZE;
    const uint64_t second_user_stack_virtual =
        second_user_region_base + 3u * DODO_PAGE_SIZE;
    volatile uint8_t *second_user_code = (volatile uint8_t *)
        dodo_x86_64_hhdm_pointer(second_user_code_frame);
    volatile uint8_t *second_user_stack = (volatile uint8_t *)
        dodo_x86_64_hhdm_pointer(second_user_stack_frame);
    if (second_user_code == NULL || second_user_stack == NULL) {
        report_boot_error("cannot initialize second user pages through HHDM");
    }

    for (uint64_t i = 0; i < DODO_PAGE_SIZE; ++i) {
        second_user_code[i] = 0;
        second_user_stack[i] = 0;
    }

    /*
     * Global TLB entries can survive a CR3 switch when PGE is enabled. User
     * leaves must therefore never request the global bit across address spaces.
     */
    frames_used = UINT64_MAX;
    paging_status = dodo_x86_64_map_page(
        second_user_code_virtual, second_user_code_frame,
        DODO_PAGE_USER_ACCESSIBLE | DODO_PAGE_GLOBAL,
        second_user_table_frames, 3u, &frames_used);
    if (paging_status != DODO_PAGING_INVALID_ADDRESS || frames_used != 0u) {
        report_boot_error("user mapping accepted the global TLB flag");
    }
    serial_write("DODO: user-global mapping rejection self-check passed\n");

    /*
     * Never accept either active root as a table-frame allocation candidate:
     * zeroing a caller-supplied frame must not be able to erase live mappings.
     */
    const uint64_t forbidden_root_frame[1] = { kernel_root_frame };
    frames_used = UINT64_MAX;
    paging_status = dodo_x86_64_map_page(
        second_user_code_virtual, second_user_code_frame,
        DODO_PAGE_USER_ACCESSIBLE, forbidden_root_frame, 1u, &frames_used);
    if (paging_status != DODO_PAGING_INVALID_ADDRESS || frames_used != 0u) {
        report_boot_error("page mapper accepted the active kernel root as a table frame");
    }
    serial_write("DODO: live-root table-frame rejection self-check passed\n");

    /*
     * Force a partial page-table construction failure: one frame is enough
     * for the PML4 child but not the next level. The mapper must unlink that
     * child and report the frame it consumed so the caller can reuse it.
     */
    frames_used = 0;
    paging_status = dodo_x86_64_map_page(
        second_user_code_virtual, second_user_code_frame,
        DODO_PAGE_USER_ACCESSIBLE, second_user_table_frames, 1u, &frames_used);
    if (paging_status != DODO_PAGING_NO_TABLE_FRAMES || frames_used != 1u) {
        report_boot_error("partial user mapping failure was not reported cleanly");
    }

    uint64_t region_after_rollback = 0;
    paging_status = dodo_x86_64_find_unused_user_region(&region_after_rollback);
    if (paging_status != DODO_PAGING_OK ||
        region_after_rollback != second_user_region_base) {
        report_boot_error("failed user mapping left a dangling PML4 entry");
    }
    serial_write("DODO: partial page-table mapping rollback self-check passed\n");

    paging_status = dodo_x86_64_map_page(
        second_user_code_virtual, second_user_code_frame,
        DODO_PAGE_USER_ACCESSIBLE, second_user_table_frames, 3u, &frames_used);
    if (paging_status != DODO_PAGING_OK || frames_used != 3u) {
        report_boot_error("could not map second user code page");
    }

    const uint64_t second_stack_flags = DODO_PAGE_USER_ACCESSIBLE |
        DODO_PAGE_WRITABLE |
        (dodo_x86_64_nx_enabled() ? DODO_PAGE_NO_EXECUTE : 0u);
    paging_status = dodo_x86_64_map_page(
        second_user_stack_virtual, second_user_stack_frame,
        second_stack_flags, NULL, 0, &frames_used);
    if (paging_status != DODO_PAGING_OK || frames_used != 0u) {
        report_boot_error("could not map second user stack page");
    }

    /*
     * User roots share higher-half page-table branches with the kernel root.
     * Mapping or unmapping a kernel VA while this root is active must be
     * rejected, otherwise the shared kernel mapping could be changed.
     */
    frames_used = UINT64_MAX;
    paging_status = dodo_x86_64_map_page(
        shared_kernel_test_virtual_address + 2u * DODO_PAGE_SIZE,
        second_user_code_frame, DODO_PAGE_WRITABLE,
        NULL, 0u, &frames_used);
    if (paging_status != DODO_PAGING_INVALID_ADDRESS || frames_used != 0u) {
        report_boot_error("user root was allowed to map into the shared kernel half");
    }

    uint64_t shared_kernel_physical = UINT64_MAX;
    uint64_t shared_kernel_flags = UINT64_MAX;
    paging_status = dodo_x86_64_unmap_page(
        shared_kernel_test_virtual_address, &shared_kernel_physical, &shared_kernel_flags);
    if (paging_status != DODO_PAGING_INVALID_ADDRESS ||
        shared_kernel_physical != UINT64_MAX ||
        shared_kernel_flags != UINT64_MAX) {
        report_boot_error("user root was allowed to unmap a shared kernel mapping");
    }
    serial_write("DODO: shared kernel mapping mutation rejection self-check passed\n");

    /*
     * Creating a root is destructive because it initializes the whole page.
     * Passing the root currently loaded in CR3 must be rejected before that
     * initialization, or the kernel can erase its active mappings.
     */
    paging_status =
        dodo_x86_64_paging_create_user_root(second_user_root_frame);
    if (paging_status != DODO_PAGING_INVALID_ADDRESS) {
        report_boot_error("user-root creation accepted the active CR3 root");
    }

    uint64_t active_root_leaf = UINT64_MAX;
    uint64_t active_root_flags = UINT64_MAX;
    paging_status = dodo_x86_64_unmap_page(
        second_user_code_virtual, &active_root_leaf, &active_root_flags);
    if (paging_status != DODO_PAGING_OK ||
        active_root_leaf != second_user_code_frame) {
        report_boot_error("active-root rejection damaged existing user mappings");
    }
    paging_status = dodo_x86_64_map_page(
        second_user_code_virtual, second_user_code_frame,
        DODO_PAGE_USER_ACCESSIBLE, NULL, 0u, &frames_used);
    if (paging_status != DODO_PAGING_OK || frames_used != 0u) {
        report_boot_error("could not restore mapping after active-root rejection test");
    }
    serial_write("DODO: active-root recreation rejection self-check passed\n");

    /*
     * movabs rax, first_root_code_va; mov rax, [rax]; ud2
     * The immediate is encoded little-endian, so derive it from the first
     * root's actual user code address instead of assuming a fixed PML4 slot.
     */
    second_user_code[0] = UINT8_C(0x48);
    second_user_code[1] = UINT8_C(0xb8);
    for (uint64_t i = 0; i < sizeof(uint64_t); ++i) {
        second_user_code[2u + i] =
            (uint8_t)(user_code_virtual >> (i * 8u));
    }
    second_user_code[10] = UINT8_C(0x48);
    second_user_code[11] = UINT8_C(0x8b);
    second_user_code[12] = UINT8_C(0x00);
    second_user_code[13] = UINT8_C(0x0f);
    second_user_code[14] = UINT8_C(0x0b);

    if (dodo_x86_64_paging_switch_root(kernel_root_frame) != DODO_PAGING_OK) {
        report_boot_error("could not restore kernel root before second CPL3 probe");
    }

    expected_page_fault_address = user_code_virtual;
    expected_page_fault_kind = DODO_EXPECT_USER_UNMAPPED_FAULT;
    page_fault_selftest_armed = 1;
    user_cross_root_isolation_passed = 0;
    const uint64_t second_user_stack_pointer =
        second_user_stack_virtual + DODO_PAGE_SIZE - UINT64_C(16);
    dodo_x86_64_enter_user(
        second_user_code_virtual, second_user_stack_pointer,
        second_user_root_frame);

    if (page_fault_selftest_armed != 0u ||
        user_cross_root_isolation_passed == 0u) {
        report_boot_error("second user root could access first root's mapping");
    }
    serial_write("DODO: independent user-root isolation self-check passed\n");

    /*
     * Exercise leaf unmapping and reuse in root B after the CPL3 test is done.
     * Unmapping must invalidate the TLB but must not free the data frame or the
     * intermediate tables; a second mapping can therefore reuse those tables.
     */
    if (dodo_x86_64_paging_switch_root(second_user_root_frame) != DODO_PAGING_OK) {
        report_boot_error("could not activate second root for unmap self-check");
    }

    uint64_t unmapped_physical = UINT64_MAX;
    uint64_t unmapped_flags = UINT64_MAX;
    paging_status = dodo_x86_64_unmap_page(
        second_user_code_virtual, &unmapped_physical, &unmapped_flags);
    if (paging_status != DODO_PAGING_OK ||
        unmapped_physical != second_user_code_frame ||
        (unmapped_flags & DODO_PAGE_USER_ACCESSIBLE) == 0u) {
        report_boot_error("user page unmapping did not return its original frame and flags");
    }

    frames_used = UINT64_MAX;
    paging_status = dodo_x86_64_map_page(
        second_user_code_virtual, second_user_code_frame,
        DODO_PAGE_USER_ACCESSIBLE, NULL, 0u, &frames_used);
    if (paging_status != DODO_PAGING_OK || frames_used != 0u) {
        report_boot_error("unmapped user page could not be mapped again using retained tables");
    }

    paging_status = dodo_x86_64_unmap_page(
        second_user_code_virtual, &unmapped_physical, &unmapped_flags);
    if (paging_status != DODO_PAGING_OK ||
        unmapped_physical != second_user_code_frame) {
        report_boot_error("second user page unmapping failed");
    }

    unmapped_physical = UINT64_MAX;
    unmapped_flags = UINT64_MAX;
    paging_status = dodo_x86_64_unmap_page(
        second_user_code_virtual, &unmapped_physical, &unmapped_flags);
    if (paging_status != DODO_PAGING_NOT_MAPPED ||
        unmapped_physical != UINT64_MAX || unmapped_flags != UINT64_MAX) {
        report_boot_error("unmap accepted an absent leaf or changed failure outputs");
    }

    paging_status = dodo_x86_64_unmap_page(
        DODO_X86_64_USER_ADDRESS_LIMIT, &unmapped_physical, &unmapped_flags);
    if (paging_status != DODO_PAGING_INVALID_ADDRESS ||
        unmapped_physical != UINT64_MAX || unmapped_flags != UINT64_MAX) {
        report_boot_error("unmap accepted a non-canonical user-half boundary");
    }
    serial_write("DODO: user-page unmap/remap self-check passed\n");

    /*
     * No user root is active after the trampoline returns. Reinstall the kernel
     * CR3 explicitly, then release only frames allocated for these two test
     * roots; the shared supervisor-only upper-half tables are not ours to free.
     */
    if (dodo_x86_64_paging_switch_root(kernel_root_frame) != DODO_PAGING_OK) {
        report_boot_error("could not confirm kernel root before user-root teardown");
    }

    for (uint64_t i = 0; i < 3u; ++i) {
        if (dodo_phys_free_page(&physical_allocator, user_table_frames[i]) !=
                DODO_PHYS_OK ||
            dodo_phys_free_page(&physical_allocator, second_user_table_frames[i]) !=
                DODO_PHYS_OK) {
            report_boot_error("could not release user-root page-table frames");
        }
    }

    if (dodo_phys_free_page(&physical_allocator, user_code_frame) != DODO_PHYS_OK ||
        dodo_phys_free_page(&physical_allocator, user_stack_frame) != DODO_PHYS_OK ||
        dodo_phys_free_page(&physical_allocator, user_root_frame) != DODO_PHYS_OK ||
        dodo_phys_free_page(&physical_allocator, second_user_code_frame) != DODO_PHYS_OK ||
        dodo_phys_free_page(&physical_allocator, second_user_stack_frame) != DODO_PHYS_OK ||
        dodo_phys_free_page(&physical_allocator, second_user_root_frame) != DODO_PHYS_OK) {
        report_boot_error("could not release user-root test frames");
    }

    if (physical_allocator.free_pages != free_pages_before_user_tests ||
        physical_allocator.allocated_pages != allocated_pages_before_user_tests) {
        report_boot_error("user-root teardown changed physical allocator accounting");
    }

    serial_write("DODO: user-root teardown/reclaim self-check passed\n");
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
    serial_write("\nDODO OS: kernel reached entry point\n");

    serial_write("DODO: begin own GDT installation\n");
    dodo_x86_64_gdt_init();
    serial_write("DODO: own GDT load instruction returned\n");
    if (!dodo_x86_64_gdt_verify()) {
        report_boot_error("DODO's GDT installation self-check failed");
    }
    serial_write("DODO: own GDT loaded; kernel and user selectors verified\n");

    dodo_x86_64_idt_init();

    if (limine_base_revision[2] != 0u) {
        report_boot_error("Limine base revision 3 was not accepted");
    }

    serial_write("DODO: Limine base revision accepted\n");
    serial_write("DODO: COM1 serial diagnostics ready\n");
    serial_write("DODO: exception IDT loaded\n");
    inspect_memory_map();
    test_virtual_mapping();
    test_user_mode_transitions();
    test_exception_handler();
    reclaim_bootloader_memory();
    serial_write("DODO: all kernel bootstrap self-checks passed\n");
    dodo_x86_64_timer_init();
    serial_write("DODO: legacy PIC remapped; PIT configured for 100 Hz\n");
    run_serial_shell();
}
