.section .text
.code64

.extern dodo_x86_64_exception_handler

.macro DODO_ISR_NOERR vector
    .global dodo_isr_\vector
    .type dodo_isr_\vector, @function
dodo_isr_\vector:
    pushq $0
    pushq $\vector
    jmp dodo_isr_common
    .size dodo_isr_\vector, . - dodo_isr_\vector
.endm

.macro DODO_ISR_ERR vector
    .global dodo_isr_\vector
    .type dodo_isr_\vector, @function
dodo_isr_\vector:
    /* The CPU already pushed the exception's error code. */
    pushq $\vector
    jmp dodo_isr_common
    .size dodo_isr_\vector, . - dodo_isr_\vector
.endm

DODO_ISR_NOERR 0
DODO_ISR_NOERR 1
DODO_ISR_NOERR 2
DODO_ISR_NOERR 3
DODO_ISR_NOERR 4
DODO_ISR_NOERR 5
DODO_ISR_NOERR 6
DODO_ISR_NOERR 7
DODO_ISR_ERR   8
DODO_ISR_NOERR 9
DODO_ISR_ERR   10
DODO_ISR_ERR   11
DODO_ISR_ERR   12
DODO_ISR_ERR   13
DODO_ISR_ERR   14
DODO_ISR_NOERR 15
DODO_ISR_NOERR 16
DODO_ISR_ERR   17
DODO_ISR_NOERR 18
DODO_ISR_NOERR 19
DODO_ISR_NOERR 20
DODO_ISR_ERR   21
DODO_ISR_NOERR 22
DODO_ISR_NOERR 23
DODO_ISR_NOERR 24
DODO_ISR_NOERR 25
DODO_ISR_NOERR 26
DODO_ISR_NOERR 27
DODO_ISR_NOERR 28
DODO_ISR_ERR   29
DODO_ISR_ERR   30
DODO_ISR_NOERR 31

/* DPL3 software interrupt used only by the controlled ring-3 self-test. */
.global dodo_isr_128
.type dodo_isr_128, @function
dodo_isr_128:
    pushq $0
    pushq $128
    jmp dodo_isr_common
.size dodo_isr_128, . - dodo_isr_128

/*
 * On entry: [vector, error, RIP, CS, RFLAGS], followed by user RSP/SS
 * when the exception crossed from CPL3 to CPL0.
 * Save all general-purpose registers. r12 temporarily holds the stack frame
 * address; its pre-interrupt value is already saved on the stack.
 */
.global dodo_isr_common
.type dodo_isr_common, @function
dodo_isr_common:
    cld
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi
    movq %rsp, %r12
    andq $-16, %rsp
    call dodo_x86_64_exception_handler
    movq %r12, %rsp

    testl %eax, %eax
    jz dodo_isr_fatal

    /* Handler explicitly allowed the armed breakpoint test to resume. */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rsi
    popq %rdi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq

dodo_isr_fatal:
    cli
1:
    hlt
    jmp 1b

.size dodo_isr_common, . - dodo_isr_common

.section .rodata
.balign 8
.global dodo_isr_table
.type dodo_isr_table, @object
dodo_isr_table:
    .quad dodo_isr_0,  dodo_isr_1,  dodo_isr_2,  dodo_isr_3
    .quad dodo_isr_4,  dodo_isr_5,  dodo_isr_6,  dodo_isr_7
    .quad dodo_isr_8,  dodo_isr_9,  dodo_isr_10, dodo_isr_11
    .quad dodo_isr_12, dodo_isr_13, dodo_isr_14, dodo_isr_15
    .quad dodo_isr_16, dodo_isr_17, dodo_isr_18, dodo_isr_19
    .quad dodo_isr_20, dodo_isr_21, dodo_isr_22, dodo_isr_23
    .quad dodo_isr_24, dodo_isr_25, dodo_isr_26, dodo_isr_27
    .quad dodo_isr_28, dodo_isr_29, dodo_isr_30, dodo_isr_31
.size dodo_isr_table, . - dodo_isr_table

.section .note.GNU-stack,"",@progbits
