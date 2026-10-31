#include <punos/syscall/syscall.h>
#include <runtime/thread-state.h>

struct RuntimeThreadState punos_main_thread_state;

extern int main(int argc, char **argv, char **envp);

void *memset(void *destination, int value, size_t size)
{
    volatile u8 *bytes = destination;

    for (size_t index = 0; index < size; ++index) {
        bytes[index] = (u8)value;
    }
    return destination;
}

void *memcpy(void *destination, const void *source, size_t size)
{
    volatile u8 *to = destination;
    const volatile u8 *from = source;

    for (size_t index = 0; index < size; ++index) {
        to[index] = from[index];
    }
    return destination;
}

_Noreturn void crt_start(int argc, char **argv, char **envp)
{
    int status = main(argc, argv, envp);

    exit((i32)status);
}
