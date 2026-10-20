#include <stdint.h>

#include "dodo/arch/x86_64/keyboard.h"

#define DODO_KBD_DATA_PORT 0x60u
#define DODO_PIC_MASTER_COMMAND 0x20u
#define DODO_PIC_MASTER_DATA 0x21u
#define DODO_KBD_BUFFER_SIZE 128u

static volatile char input_buffer[DODO_KBD_BUFFER_SIZE];
static volatile uint8_t input_head;
static volatile uint8_t input_tail;
static uint8_t left_shift;
static uint8_t right_shift;
static uint8_t caps_lock;
static uint8_t extended_scancode;

static const char keymap[128] = {
    [0x01] = 0x1b, [0x02] = '1', [0x03] = '2', [0x04] = '3',
    [0x05] = '4', [0x06] = '5', [0x07] = '6', [0x08] = '7',
    [0x09] = '8', [0x0a] = '9', [0x0b] = '0', [0x0c] = '-',
    [0x0d] = '=', [0x0e] = '\b', [0x0f] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1a] = '[', [0x1b] = ']',
    [0x1c] = '\r', [0x1e] = 'a', [0x1f] = 's', [0x20] = 'd',
    [0x21] = 'f', [0x22] = 'g', [0x23] = 'h', [0x24] = 'j',
    [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'',
    [0x29] = '`', [0x2b] = '\\', [0x2c] = 'z', [0x2d] = 'x',
    [0x2e] = 'c', [0x2f] = 'v', [0x30] = 'b', [0x31] = 'n',
    [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' ', [0x37] = '*'
};

static const char shifted_keymap[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0a] = '(', [0x0b] = ')', [0x0c] = '_', [0x0d] = '+',
    [0x1a] = '{', [0x1b] = '}', [0x27] = ':', [0x28] = '"',
    [0x29] = '~', [0x2b] = '|', [0x33] = '<', [0x34] = '>',
    [0x35] = '?'
};

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

void dodo_x86_64_keyboard_init(void) {
    input_head = 0;
    input_tail = 0;
    left_shift = 0;
    right_shift = 0;
    caps_lock = 0;
    extended_scancode = 0;

    /* IRQ1 must remain masked until its IDT gate and handler are installed. */
    uint8_t mask = inb(DODO_PIC_MASTER_DATA);
    mask = (uint8_t)(mask & (uint8_t)~0x02u);
    outb(DODO_PIC_MASTER_DATA, mask);
}

static void enqueue_character(char character) {
    const uint8_t next = (uint8_t)((input_head + 1u) % DODO_KBD_BUFFER_SIZE);
    if (next == input_tail) {
        return; /* Drop new input instead of overwriting unread characters. */
    }
    input_buffer[input_head] = character;
    input_head = next;
}

void dodo_x86_64_keyboard_handle_irq(void) {
    const uint8_t scan = inb(DODO_KBD_DATA_PORT);
    if (scan == 0xe0u) {
        extended_scancode = 1;
    } else if (extended_scancode != 0u) {
        extended_scancode = 0;
    } else if (scan == 0x2au) {
        left_shift = 1;
    } else if (scan == 0xaau) {
        left_shift = 0;
    } else if (scan == 0x36u) {
        right_shift = 1;
    } else if (scan == 0xb6u) {
        right_shift = 0;
    } else if (scan == 0x3au) {
        caps_lock ^= 1u;
    } else if ((scan & 0x80u) == 0u && scan < 128u) {
        const uint8_t shifted = (uint8_t)(left_shift | right_shift);
        char character = shifted_keymap[scan];
        if (character == '\0') {
            character = keymap[scan];
            if (character >= 'a' && character <= 'z' &&
                (shifted != 0u) != (caps_lock != 0u)) {
                character = (char)(character - 'a' + 'A');
            }
        }
        if (character != '\0') {
            enqueue_character(character);
        }
    }

    /* IRQ1 is delivered by the master PIC. */
    outb(DODO_PIC_MASTER_COMMAND, 0x20u);
}

int dodo_x86_64_keyboard_try_getc(char *character) {
    if (character == 0 || input_tail == input_head) {
        return 0;
    }
    *character = input_buffer[input_tail];
    input_tail = (uint8_t)((input_tail + 1u) % DODO_KBD_BUFFER_SIZE);
    return 1;
}
