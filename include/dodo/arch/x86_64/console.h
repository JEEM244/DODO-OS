#ifndef DODO_ARCH_X86_64_CONSOLE_H
#define DODO_ARCH_X86_64_CONSOLE_H

struct dodo_limine_framebuffer_response;

/* Initialize a text console on a compatible Limine RGB framebuffer. */
int dodo_x86_64_console_init(
    struct dodo_limine_framebuffer_response *response);

/* Mirror one character to the framebuffer when the console is available. */
void dodo_x86_64_console_putc(char character);

/* Clear the display and return the cursor to the upper-left corner. */
void dodo_x86_64_console_clear(void);

#endif
