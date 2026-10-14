#include <punos/syscall/syscall.h>
int main(int argc,char **argv,char **envp)
{
    (void)argc;(void)argv;(void)envp;
    for (;;) { const struct timespec delay={1,0};(void)nanosleep(&delay,0); }
}
