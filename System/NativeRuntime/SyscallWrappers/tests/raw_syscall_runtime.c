#include <punos/syscall/constants.h>
#include <punos/syscall/errno.h>
#include <runtime/thread-state.h>

#include "../src/internal/bootstrap.h"
#include "../src/internal/raw_syscall.h"
#include "../src/internal/syscall_result.h"
#include "../src/x86_64/syscall_numbers.h"

#define PROT_READ_WRITE 3
#define MAP_PRIVATE_ANONYMOUS 34
#define PAGE_SIZE 4096
#define SIG_SETMASK 2
#define KERNEL_SIGSET_SIZE 8

static int kernel_error(long result)
{
    return result >= -4095 && result < 0;
}

_Noreturn void _start(void)
{
    static const char message[] = "raw syscall and errno runtime test passed\n";
    static struct RuntimeThreadState state;
    long pid = __punos_syscall0(PUNOS_SYS_GETPID);
    long mapping;
    unsigned long signal_mask;
    long written;

    state.self = &state;
    state.errno_value = 0;
    state.reserved = 0;
    if (__punos_raw_arch_set_fs(&state) != 0) {
        __punos_raw_exit_group(10);
    }
    if (sys_errno_location() != &state.errno_value || errno != 0) {
        __punos_raw_exit_group(11);
    }
    if (__punos_syscall_result(__punos_syscall0(-1)) != -1 ||
        errno != ENOSYS) {
        __punos_raw_exit_group(12);
    }
    if (__punos_syscall_result(42) != 42 || errno != ENOSYS) {
        __punos_raw_exit_group(13);
    }

    if (pid <= 0) {
        __punos_syscall1(PUNOS_SYS_EXIT_GROUP, 1);
        __builtin_unreachable();
    }

    mapping = __punos_syscall6(PUNOS_SYS_MMAP, 0, PAGE_SIZE, PROT_READ_WRITE,
                               MAP_PRIVATE_ANONYMOUS, (unsigned long)-1, 0);
    if (kernel_error(mapping)) {
        __punos_syscall1(PUNOS_SYS_EXIT_GROUP, 2);
        __builtin_unreachable();
    }

    *(volatile unsigned char *)mapping = 0x5a;
    if (*(volatile unsigned char *)mapping != 0x5a) {
        __punos_syscall1(PUNOS_SYS_EXIT_GROUP, 3);
        __builtin_unreachable();
    }

    mapping = __punos_syscall5(PUNOS_SYS_MREMAP, (unsigned long)mapping,
                               PAGE_SIZE,
                               PAGE_SIZE, 0, 0);
    if (kernel_error(mapping)) {
        __punos_syscall1(PUNOS_SYS_EXIT_GROUP, 4);
        __builtin_unreachable();
    }

    if (__punos_syscall4(PUNOS_SYS_RT_SIGPROCMASK, SIG_SETMASK, 0,
                         (unsigned long)&signal_mask,
                         KERNEL_SIGSET_SIZE) != 0) {
        __punos_syscall1(PUNOS_SYS_EXIT_GROUP, 5);
        __builtin_unreachable();
    }

    if (__punos_syscall2(PUNOS_SYS_MUNMAP, (unsigned long)mapping,
                         PAGE_SIZE) != 0) {
        __punos_syscall1(PUNOS_SYS_EXIT_GROUP, 6);
        __builtin_unreachable();
    }

    written = __punos_syscall3(PUNOS_SYS_WRITE, STDOUT_FILENO,
                               (unsigned long)message, sizeof(message) - 1);
    if (written != (long)(sizeof(message) - 1)) {
        __punos_syscall1(PUNOS_SYS_EXIT_GROUP, 7);
        __builtin_unreachable();
    }

    __punos_syscall1(PUNOS_SYS_EXIT_GROUP, 0);
    __builtin_unreachable();
}
