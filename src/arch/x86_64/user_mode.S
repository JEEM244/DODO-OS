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
    /*
     * Preserve SysV callee-saved registers across this special transition.
     * The interrupt handler intentionally abandons the user register frame, so
     * the kernel caller's RBX/RBP/R12-R15 must be restored from its own stack.
     */
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, dodo_x86_64_saved_kernel_rsp(%rip)

    /* Save the kernel root, then activate the private user page-table root (RDX). */
    movq %cr3, %rax
    movq %rax, dodo_x86_64_saved_kernel_cr3(%rip)
    movq %rdx, %cr3

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

    /* Return to the kernel's root before using the saved kernel stack. */
    movq dodo_x86_64_saved_kernel_cr3(%rip), %rax
    movq %rax, %cr3
    movq dodo_x86_64_saved_kernel_rsp(%rip), %rsp

    /* Restore the kernel data/stack selectors before touching the old stack. */
    movw $0x30, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    /* Reverse the six pushes at entry and return to the original caller. */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

.section .bss
.balign 8
.global dodo_x86_64_saved_kernel_rsp
.type dodo_x86_64_saved_kernel_rsp, @object
dodo_x86_64_saved_kernel_rsp:
    .quad 0
.size dodo_x86_64_saved_kernel_rsp, . - dodo_x86_64_saved_kernel_rsp

.balign 8
.global dodo_x86_64_saved_kernel_cr3
.type dodo_x86_64_saved_kernel_cr3, @object
dodo_x86_64_saved_kernel_cr3:
    .quad 0
.size dodo_x86_64_saved_kernel_cr3, . - dodo_x86_64_saved_kernel_cr3

.section .note.GNU-stack,"",@progbits
