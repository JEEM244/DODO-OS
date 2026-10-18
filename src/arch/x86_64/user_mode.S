.section .text
.code64

.global dodo_x86_64_enter_user
.type dodo_x86_64_enter_user, @function
dodo_x86_64_enter_user:
    /*
     * Save the exact kernel call stack. The test's controlled interrupt/page
     * fault returns through dodo_x86_64_user_return_trampoline, which restores
     * this RSP and returns to the caller as an ordinary function return.
     */
    movq %rsp, dodo_x86_64_saved_kernel_rsp(%rip)

    /* User code must run with user-level data selectors, not kernel selectors. */
    movw $0x3b, %ax
    movw %ax, %ds
    movw %ax, %es
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    pushq $0x3b                /* user SS, RPL 3 */
    pushq %rsi                 /* user RSP */
    pushq $0x2                  /* RFLAGS with IF disabled and reserved bit set */
    pushq $0x43                /* user CS, RPL 3 */
    pushq %rdi                 /* user RIP */
    iretq

.global dodo_x86_64_user_return_trampoline
.type dodo_x86_64_user_return_trampoline, @function
dodo_x86_64_user_return_trampoline:
    cli
    movq dodo_x86_64_saved_kernel_rsp(%rip), %rsp

    /* Restore the kernel data/stack selectors before returning to C. */
    movw $0x30, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs
    ret

.section .bss
.balign 8
.global dodo_x86_64_saved_kernel_rsp
.type dodo_x86_64_saved_kernel_rsp, @object
dodo_x86_64_saved_kernel_rsp:
    .quad 0
.size dodo_x86_64_saved_kernel_rsp, . - dodo_x86_64_saved_kernel_rsp

.section .note.GNU-stack,"",@progbits
