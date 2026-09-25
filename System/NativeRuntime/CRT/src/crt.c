#include <punos/syscall/syscall.h>
#include <runtime/thread-state.h>

struct RuntimeThreadState punos_main_thread_state;

extern int main(int argc, char **argv, char **envp);

_Noreturn void crt_start(int argc, char **argv, char **envp)
{
    int status = main(argc, argv, envp);

    exit((i32)status);
}
