.section .text.boot
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
.balign 16
stack_bottom:
    .skip 65536
stack_top:

.section .note.GNU-stack,"",@progbits
