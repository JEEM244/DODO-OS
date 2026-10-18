#ifndef DODO_ARCH_X86_64_GDT_H
#define DODO_ARCH_X86_64_GDT_H

#include <stdint.h>

#define DODO_X86_64_KERNEL_CODE_SELECTOR UINT16_C(0x28)
#define DODO_X86_64_KERNEL_DATA_SELECTOR UINT16_C(0x30)
#define DODO_X86_64_USER_DATA_SELECTOR UINT16_C(0x3b)
#define DODO_X86_64_USER_CODE_SELECTOR UINT16_C(0x43)
#define DODO_X86_64_TSS_SELECTOR UINT16_C(0x48)

/* Install DODO's GDT, initialize its TSS, and load TR. */
void dodo_x86_64_gdt_init(void);

/* Check GDTR, active selectors, TR, TSS limit, and ring-0 interrupt stack. */
int dodo_x86_64_gdt_verify(void);

#endif
