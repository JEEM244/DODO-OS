#ifndef DODO_ARCH_X86_64_INTERRUPTS_H
#define DODO_ARCH_X86_64_INTERRUPTS_H

#include <stdint.h>

/*
 * Stack layout created by src/arch/x86_64/interrupts.S.
 * This frame describes exceptions handled while executing in ring 0.
 * User-mode stack fields will be added with privilege separation.
 */
struct dodo_x86_64_exception_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
};

void dodo_x86_64_idt_init(void);

/* Return 1 only when the explicitly armed INT3 self-test should resume. */
int dodo_x86_64_exception_handler(
    const struct dodo_x86_64_exception_frame *frame);

#endif
