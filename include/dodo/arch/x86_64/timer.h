#ifndef DODO_ARCH_X86_64_TIMER_H
#define DODO_ARCH_X86_64_TIMER_H

#include <stdint.h>

#define DODO_X86_64_TIMER_HZ 100u

/* Configure PIT channel 0 and enable only the legacy PIC's IRQ0 line. */
void dodo_x86_64_timer_init(void);

/* Called by the common interrupt dispatcher for IDT vector 32. */
void dodo_x86_64_timer_handle_irq(void);

/* Return the number of timer interrupts observed since initialization. */
uint64_t dodo_x86_64_timer_ticks(void);

#endif
