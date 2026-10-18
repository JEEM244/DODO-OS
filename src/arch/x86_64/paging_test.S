.section .text
.code64

.global dodo_x86_64_test_write_readonly
.type dodo_x86_64_test_write_readonly, @function
dodo_x86_64_test_write_readonly:
    movabsq $0x1122334455667788, %rax
    movq %rax, (%rdi)
    movl $1, %eax
    ret

/*
 * The page-fault handler redirects RIP here only for the armed write-protection
 * test. No extra return address was pushed by this routine itself.
 */
.global dodo_x86_64_readonly_write_fault_resume
.type dodo_x86_64_readonly_write_fault_resume, @function
dodo_x86_64_readonly_write_fault_resume:
    xorl %eax, %eax
    ret

.global dodo_x86_64_test_execute_nx
.type dodo_x86_64_test_execute_nx, @function
dodo_x86_64_test_execute_nx:
    call *%rdi
    movl $1, %eax
    ret

/*
 * If instruction fetch is blocked by NX, the fault happened after CALL pushed
 * its return address. This resume label removes that one word before returning.
 */
.global dodo_x86_64_nx_execute_fault_resume
.type dodo_x86_64_nx_execute_fault_resume, @function
dodo_x86_64_nx_execute_fault_resume:
    addq $8, %rsp
    xorl %eax, %eax
    ret

.section .note.GNU-stack,"",@progbits
