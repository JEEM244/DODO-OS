#ifndef DODO_ARCH_X86_64_KEYBOARD_H
#define DODO_ARCH_X86_64_KEYBOARD_H

/* Initialize the PS/2 set-1 scancode input path and unmask legacy IRQ1. */
void dodo_x86_64_keyboard_init(void);

/* Called by the interrupt dispatcher for IDT vector 33. */
void dodo_x86_64_keyboard_handle_irq(void);

/* Non-blocking: returns 1 and stores one translated ASCII character if ready. */
int dodo_x86_64_keyboard_try_getc(char *character);

#endif
