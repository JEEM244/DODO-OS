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

#define DODO_EXCEPTION_FATAL 0
#define DODO_EXCEPTION_RESUME 1
#define DODO_EXCEPTION_RETURN_TO_KERNEL 2

void dodo_x86_64_idt_init(void);

/*
 * Return RESUME to restore the saved CPU frame with iretq, or
 * RETURN_TO_KERNEL to abandon a controlled CPL3 frame and restore the saved
 * kernel call stack via dodo_x86_64_user_return_trampoline.
 */
int dodo_x86_64_exception_handler(
    struct dodo_x86_64_exception_frame *frame);

#endif
