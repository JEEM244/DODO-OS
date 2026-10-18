#ifndef DODO_ARCH_X86_64_USER_MODE_H
#define DODO_ARCH_X86_64_USER_MODE_H

#include <stdint.h>

#define DODO_X86_64_USER_TEST_VECTOR UINT64_C(128)
#define DODO_X86_64_USER_TEST_MAGIC UINT64_C(0x00000000d0d0c0de)
#define DODO_X86_64_SYSCALL_WRITE UINT64_C(1)
#define DODO_X86_64_SYSCALL_MAX_WRITE UINT64_C(96)
#define DODO_X86_64_SYSCALL_ERROR UINT64_MAX

/*
 * Enter ring 3 using iretq. A one-shot interrupt/page-fault test handler will
 * redirect back to dodo_x86_64_user_return_trampoline, which restores the
 * original kernel stack saved by this function.
 */
void dodo_x86_64_enter_user(
    uint64_t entry_address,
    uint64_t user_stack_pointer,
    uint64_t user_root_physical);
void dodo_x86_64_user_return_trampoline(void);

#endif
