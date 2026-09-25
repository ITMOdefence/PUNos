#include "internal/bootstrap.h"
#include "internal/raw_syscall.h"
#include "x86_64/syscall_numbers.h"

#define PUNOS_ARCH_SET_FS 0x1002UL

long __punos_raw_arch_set_fs(void *thread_state)
{
    return __punos_syscall2(PUNOS_SYS_ARCH_PRCTL, PUNOS_ARCH_SET_FS,
                            (unsigned long)thread_state);
}

_Noreturn void __punos_raw_exit_group(int status)
{
    __punos_syscall1(PUNOS_SYS_EXIT_GROUP, (unsigned long)status);
    __builtin_unreachable();
}
