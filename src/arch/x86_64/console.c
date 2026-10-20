#include <stdint.h>

#include "dodo/arch/x86_64/console.h"
#include "dodo/arch/x86_64/font8x8.h"
#include "dodo/boot/limine_protocol.h"

#define DODO_CONSOLE_CHAR_WIDTH 8u
#define DODO_CONSOLE_CHAR_HEIGHT 16u
#define DODO_CONSOLE_MAX_DIMENSION 16384u
#define DODO_CONSOLE_ESCAPE_NONE 0u
#define DODO_CONSOLE_ESCAPE_SEEN 1u
#define DODO_CONSOLE_ESCAPE_CSI 2u

struct dodo_console_surface {
    volatile uint8_t *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint8_t bytes_per_pixel;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint64_t cursor_x;
    uint64_t cursor_y;
    uint8_t active;
};

static struct dodo_console_surface surface;
static uint8_t escape_state;
static char escape_parameters[8];
static uint8_t escape_length;

static uint32_t make_mask(uint8_t size, uint8_t shift) {
    if (size == 0u || size > UINT8_C(8) || shift >= UINT8_C(32) ||
        (uint16_t)shift + (uint16_t)size > UINT16_C(32)) {
        return 0u;
    }

    return ((UINT32_C(1) << size) - UINT32_C(1)) << shift;
}

static uint32_t make_channel(
    uint8_t value, uint8_t mask_size, uint8_t mask_shift) {
    const uint32_t maximum = (UINT32_C(1) << mask_size) - UINT32_C(1);
    const uint32_t scaled =
        ((uint32_t)value * maximum + UINT32_C(127)) / UINT32_C(255);
    return scaled << mask_shift;
}

static uint32_t rgb(uint8_t red, uint8_t green, uint8_t blue) {
    return make_channel(red, surface.red_mask_size, surface.red_mask_shift) |
        make_channel(green, surface.green_mask_size, surface.green_mask_shift) |
        make_channel(blue, surface.blue_mask_size, surface.blue_mask_shift);
}

static uint32_t background_color(void) {
    return rgb(8u, 13u, 21u);
}

static uint32_t foreground_color(void) {
    return rgb(224u, 235u, 246u);
}

static void write_pixel(uint64_t x, uint64_t y, uint32_t color) {
    if (x >= surface.width || y >= surface.height) {
        return;
    }

    volatile uint8_t *pixel = surface.address + y * surface.pitch +
        x * surface.bytes_per_pixel;
    for (uint8_t byte = 0u; byte < surface.bytes_per_pixel; ++byte) {
        pixel[byte] = (uint8_t)(color >> (byte * 8u));
    }
}

static void fill_rectangle(
    uint64_t x, uint64_t y, uint64_t width, uint64_t height, uint32_t color) {
    const uint64_t requested_end_x = x + width;
    const uint64_t requested_end_y = y + height;
    const uint64_t end_x =
        requested_end_x < surface.width ? requested_end_x : surface.width;
    const uint64_t end_y =
        requested_end_y < surface.height ? requested_end_y : surface.height;

    for (uint64_t row = y; row < end_y; ++row) {
        for (uint64_t column = x; column < end_x; ++column) {
            write_pixel(column, row, color);
        }
    }
}

static void copy_row_bytes(
    volatile uint8_t *destination,
    const volatile uint8_t *source,
    uint64_t byte_count) {
    for (uint64_t byte = 0u; byte < byte_count; ++byte) {
        destination[byte] = source[byte];
    }
}

static void scroll_one_line(void) {
    if (surface.height <= DODO_CONSOLE_CHAR_HEIGHT) {
        fill_rectangle(
            0u, 0u, surface.width, surface.height, background_color());
        surface.cursor_y = 0u;
        return;
    }

    const uint64_t rows_to_keep = surface.height - DODO_CONSOLE_CHAR_HEIGHT;
    for (uint64_t row = 0u; row < rows_to_keep; ++row) {
        copy_row_bytes(
            surface.address + row * surface.pitch,
            surface.address + (row + DODO_CONSOLE_CHAR_HEIGHT) * surface.pitch,
            surface.pitch);
    }

    fill_rectangle(
        0u, rows_to_keep, surface.width, DODO_CONSOLE_CHAR_HEIGHT,
        background_color());
    surface.cursor_y = rows_to_keep;
}

static void advance_line(void) {
    surface.cursor_x = 0u;
    surface.cursor_y += DODO_CONSOLE_CHAR_HEIGHT;
    if (surface.cursor_y + DODO_CONSOLE_CHAR_HEIGHT > surface.height) {
        scroll_one_line();
    }
}

static void draw_character(char character) {
    uint8_t code = (uint8_t)character;
    if (code >= UINT8_C(128)) {
        code = (uint8_t)'?';
    }

    if (surface.cursor_x + DODO_CONSOLE_CHAR_WIDTH > surface.width) {
        advance_line();
    }

    const uint64_t origin_x = surface.cursor_x;
    const uint64_t origin_y = surface.cursor_y;
    const uint32_t foreground = foreground_color();
    const uint32_t background = background_color();

    for (uint8_t row = 0u; row < UINT8_C(8); ++row) {
        const uint8_t bitmap = dodo_font8x8_basic[code][row];
        for (uint8_t repeat = 0u; repeat < UINT8_C(2); ++repeat) {
            for (uint8_t column = 0u; column < UINT8_C(8); ++column) {
                const uint32_t color =
                    (bitmap & (uint8_t)(UINT8_C(1) << column)) != 0u
                    ? foreground : background;
                write_pixel(
                    origin_x + column,
                    origin_y + (uint64_t)row * 2u + repeat,
                    color);
            }
        }
    }

    surface.cursor_x += DODO_CONSOLE_CHAR_WIDTH;
}

static void clear_current_line(void) {
    if (surface.cursor_y < surface.height) {
        fill_rectangle(
            surface.cursor_x, surface.cursor_y,
            surface.width - surface.cursor_x, DODO_CONSOLE_CHAR_HEIGHT,
            background_color());
    }
}

void dodo_x86_64_console_clear(void) {
    if (surface.active == 0u) {
        return;
    }

    fill_rectangle(
        0u, 0u, surface.width, surface.height, background_color());
    surface.cursor_x = 0u;
    surface.cursor_y = 0u;
}

static void reset_escape(void) {
    escape_state = DODO_CONSOLE_ESCAPE_NONE;
    escape_length = 0u;
}

static int parameters_equal(const char *expected, uint8_t length) {
    uint8_t index = 0u;
    while (expected[index] != '\0') {
        if (index >= length || escape_parameters[index] != expected[index]) {
            return 0;
        }
        ++index;
    }
    return index == length;
}

static void handle_escape_character(char character) {
    if (escape_state == DODO_CONSOLE_ESCAPE_SEEN) {
        if (character == '[') {
            escape_state = DODO_CONSOLE_ESCAPE_CSI;
            escape_length = 0u;
        } else {
            reset_escape();
        }
        return;
    }

    if (escape_state != DODO_CONSOLE_ESCAPE_CSI) {
        reset_escape();
        return;
    }

    if ((character >= '0' && character <= '9') || character == ';') {
        if ((uint64_t)escape_length < sizeof(escape_parameters)) {
            escape_parameters[escape_length++] = character;
        } else {
            reset_escape();
        }
        return;
    }

    if (character == 'J' && parameters_equal("2", escape_length)) {
        dodo_x86_64_console_clear();
    } else if (character == 'H' &&
        (escape_length == 0u ||
         parameters_equal("1", escape_length) ||
         parameters_equal("1;1", escape_length))) {
        surface.cursor_x = 0u;
        surface.cursor_y = 0u;
    } else if (character == 'K' && escape_length == 0u) {
        clear_current_line();
    }

    reset_escape();
}

int dodo_x86_64_console_init(
    struct dodo_limine_framebuffer_response *response) {
    if (response == 0 || response->framebuffer_count == 0u ||
        response->framebuffers == 0 || response->framebuffers[0] == 0) {
        return 0;
    }

    const struct dodo_limine_framebuffer *framebuffer =
        response->framebuffers[0];
    if (framebuffer->address == 0 ||
        framebuffer->memory_model != DODO_LIMINE_FRAMEBUFFER_MEMORY_MODEL_RGB ||
        (framebuffer->bpp != 24u && framebuffer->bpp != 32u) ||
        framebuffer->width < DODO_CONSOLE_CHAR_WIDTH ||
        framebuffer->height < DODO_CONSOLE_CHAR_HEIGHT ||
        framebuffer->width > DODO_CONSOLE_MAX_DIMENSION ||
        framebuffer->height > DODO_CONSOLE_MAX_DIMENSION) {
        return 0;
    }

    const uint8_t bytes_per_pixel = (uint8_t)(framebuffer->bpp / 8u);
    if (framebuffer->width > UINT64_MAX / bytes_per_pixel ||
        framebuffer->pitch < framebuffer->width * bytes_per_pixel ||
        framebuffer->pitch == 0u ||
        framebuffer->height > UINT64_MAX / framebuffer->pitch) {
        return 0;
    }

    const uint32_t red_mask =
        make_mask(framebuffer->red_mask_size, framebuffer->red_mask_shift);
    const uint32_t green_mask =
        make_mask(framebuffer->green_mask_size, framebuffer->green_mask_shift);
    const uint32_t blue_mask =
        make_mask(framebuffer->blue_mask_size, framebuffer->blue_mask_shift);
    if (red_mask == 0u || green_mask == 0u || blue_mask == 0u ||
        (red_mask & green_mask) != 0u ||
        (red_mask & blue_mask) != 0u ||
        (green_mask & blue_mask) != 0u ||
        (uint16_t)framebuffer->red_mask_shift + framebuffer->red_mask_size >
            framebuffer->bpp ||
        (uint16_t)framebuffer->green_mask_shift + framebuffer->green_mask_size >
            framebuffer->bpp ||
        (uint16_t)framebuffer->blue_mask_shift + framebuffer->blue_mask_size >
            framebuffer->bpp) {
        return 0;
    }

    surface.address = (volatile uint8_t *)framebuffer->address;
    surface.width = framebuffer->width;
    surface.height = framebuffer->height;
    surface.pitch = framebuffer->pitch;
    surface.bytes_per_pixel = bytes_per_pixel;
    surface.red_mask_size = framebuffer->red_mask_size;
    surface.red_mask_shift = framebuffer->red_mask_shift;
    surface.green_mask_size = framebuffer->green_mask_size;
    surface.green_mask_shift = framebuffer->green_mask_shift;
    surface.blue_mask_size = framebuffer->blue_mask_size;
    surface.blue_mask_shift = framebuffer->blue_mask_shift;
    surface.cursor_x = 0u;
    surface.cursor_y = 0u;
    escape_state = DODO_CONSOLE_ESCAPE_NONE;
    escape_length = 0u;
    surface.active = 1u;
    dodo_x86_64_console_clear();
    return 1;
}

void dodo_x86_64_console_putc(char character) {
    if (surface.active == 0u) {
        return;
    }

    if (escape_state != DODO_CONSOLE_ESCAPE_NONE) {
        handle_escape_character(character);
        return;
    }

    if ((uint8_t)character == UINT8_C(0x1b)) {
        escape_state = DODO_CONSOLE_ESCAPE_SEEN;
        escape_length = 0u;
        return;
    }

    switch (character) {
        case '\r':
            surface.cursor_x = 0u;
            return;
        case '\n':
            advance_line();
            return;
        case '\b':
            if (surface.cursor_x >= DODO_CONSOLE_CHAR_WIDTH) {
                surface.cursor_x -= DODO_CONSOLE_CHAR_WIDTH;
                fill_rectangle(
                    surface.cursor_x, surface.cursor_y,
                    DODO_CONSOLE_CHAR_WIDTH, DODO_CONSOLE_CHAR_HEIGHT,
                    background_color());
            }
            return;
        case '\t': {
            const uint64_t current_column =
                surface.cursor_x / DODO_CONSOLE_CHAR_WIDTH;
            const uint64_t target_column =
                (current_column / 4u + 1u) * 4u;
            do {
                draw_character(' ');
            } while (surface.cursor_x / DODO_CONSOLE_CHAR_WIDTH <
                     target_column);
            return;
        }
        default:
            if ((uint32_t)(uint8_t)character >= UINT32_C(0x20) &&
                (uint32_t)(uint8_t)character <= UINT32_C(0x7e)) {
                draw_character(character);
            }
            return;
    }
}
