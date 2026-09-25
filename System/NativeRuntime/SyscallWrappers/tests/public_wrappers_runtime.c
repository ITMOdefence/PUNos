#include <punos/syscall/syscall.h>
#include <runtime/thread-state.h>

#include "../src/internal/bootstrap.h"

#define PAGE_SIZE 4096

_Noreturn void _start(void)
{
    static const char message[] = "public syscall wrappers runtime test passed\n";
    static struct RuntimeThreadState state;
    void *mapping;

    state.self = &state;
    state.errno_value = 0;
    state.reserved = 0;
    if (__punos_raw_arch_set_fs(&state) != 0) {
        __punos_raw_exit_group(20);
    }

    if (getpid() <= 0) {
        exit(21);
    }

    mapping = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        exit(22);
    }

    *(volatile unsigned char *)mapping = 0x5a;
    if (*(volatile unsigned char *)mapping != 0x5a) {
        exit(23);
    }

    if (munmap(mapping, PAGE_SIZE) != 0) {
        exit(24);
    }

    errno = 0;
    if (munmap((void *)1, PAGE_SIZE) != -1 || errno != EINVAL) {
        exit(25);
    }

    if (write(STDOUT_FILENO, message, sizeof(message) - 1) !=
        (ssize_t)(sizeof(message) - 1)) {
        exit(26);
    }

    exit(0);
}
