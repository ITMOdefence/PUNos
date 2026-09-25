#include <punos/syscall/errno.h>
#include <runtime/thread-state.h>

#include "internal/syscall_result.h"

i32 *sys_errno_location(void)
{
    struct RuntimeThreadState *state;

    __asm__ volatile("movq %%fs:0, %0" : "=r"(state));
    return &state->errno_value;
}

long __punos_syscall_result(long result)
{
    if (result >= -4095 && result < 0) {
        errno = (i32)-result;
        return -1;
    }

    return result;
}
