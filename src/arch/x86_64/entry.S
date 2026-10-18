.section .text
.global _start
.type _start, @function
.extern kernel_main

_start:
    cli
    lea stack_top(%rip), %rsp
    andq $-16, %rsp
    xorq %rbp, %rbp
    call kernel_main

1:
    cli
    hlt
    jmp 1b

.size _start, . - _start

.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

.section .note.GNU-stack,"",@progbits
