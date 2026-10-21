#ifndef DODO_ARCH_X86_64_INTERRUPTS_H
#define DODO_ARCH_X86_64_INTERRUPTS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Fixed prefix of the frame built by src/arch/x86_64/interrupts.S.
 * On CPL3-to-CPL0 transitions the CPU also pushes the prior user RSP and SS
 * after RFLAGS; those tail fields are deliberately not exposed as ordinary
 * members because the controlled return path abandons that frame.
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

/*
 * The assembly interrupt stub pushes these registers in a fixed order.
 * Fail the build if a future edit silently breaks the C/assembly frame ABI.
 */
/* Lock every saved-register offset before the context-switch path uses it. */
#define DODO_ASSERT_FRAME_OFFSET(member, expected) \
    _Static_assert(offsetof(struct dodo_x86_64_exception_frame, member) == (expected), \
                   "interrupt frame offset mismatch: " #member)

DODO_ASSERT_FRAME_OFFSET(r15, 0u);
DODO_ASSERT_FRAME_OFFSET(r14, 8u);
DODO_ASSERT_FRAME_OFFSET(r13, 16u);
DODO_ASSERT_FRAME_OFFSET(r12, 24u);
DODO_ASSERT_FRAME_OFFSET(r11, 32u);
DODO_ASSERT_FRAME_OFFSET(r10, 40u);
DODO_ASSERT_FRAME_OFFSET(r9, 48u);
DODO_ASSERT_FRAME_OFFSET(r8, 56u);
DODO_ASSERT_FRAME_OFFSET(rsi, 64u);
DODO_ASSERT_FRAME_OFFSET(rdi, 72u);
DODO_ASSERT_FRAME_OFFSET(rbp, 80u);
DODO_ASSERT_FRAME_OFFSET(rdx, 88u);
DODO_ASSERT_FRAME_OFFSET(rcx, 96u);
DODO_ASSERT_FRAME_OFFSET(rbx, 104u);
DODO_ASSERT_FRAME_OFFSET(rax, 112u);
DODO_ASSERT_FRAME_OFFSET(vector, 120u);
DODO_ASSERT_FRAME_OFFSET(error_code, 128u);
DODO_ASSERT_FRAME_OFFSET(rip, 136u);
DODO_ASSERT_FRAME_OFFSET(cs, 144u);
DODO_ASSERT_FRAME_OFFSET(rflags, 152u);

#undef DODO_ASSERT_FRAME_OFFSET

_Static_assert(sizeof(struct dodo_x86_64_exception_frame) == 160u,
               "interrupt frame prefix size must match interrupts.S");

#define DODO_EXCEPTION_FATAL 0
#define DODO_EXCEPTION_RESUME 1
#define DODO_EXCEPTION_RETURN_TO_KERNEL 2
#define DODO_EXCEPTION_SWITCH_CONTEXT 3

void dodo_x86_64_idt_init(void);

/*
 * Return RESUME to restore the saved CPU frame with iretq, or
 * RETURN_TO_KERNEL to abandon a controlled CPL3 frame and restore the saved
 * kernel call stack via dodo_x86_64_user_return_trampoline.
 */
int dodo_x86_64_exception_handler(
    struct dodo_x86_64_exception_frame *frame);

#endif
