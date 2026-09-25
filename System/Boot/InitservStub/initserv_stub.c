#include <punos/syscall/syscall.h>

int main(int argc, char **argv, char **envp)
{
    static const char started[] =
        "PUNos initserv stub: smoke-test process is running\n";
    static const char passed[] =
        "PUNos initserv stub: syscall self-test passed\n";
    static const char failed[] =
        "PUNos initserv stub: syscall self-test failed\n";
    static const struct timespec delay = { .tv_sec = 60, .tv_nsec = 0 };
    char *const test_argv[] = { (char *)"/punos/bin/syscall-selftest", 0 };
    char *const empty_env[] = { 0 };
    pid_t child;
    i32 status = -1;

    (void)argc;
    (void)argv;
    (void)envp;
    (void)write(STDERR_FILENO, started, sizeof(started) - 1);
    child = fork();
    if (child == 0) {
        (void)execve("/punos/bin/syscall-selftest", test_argv, empty_env);
        exit(126);
    }
    if (child < 0 || waitpid(child, &status, 0) != child || status != 0) {
        (void)write(STDERR_FILENO, failed, sizeof(failed) - 1);
    } else {
        (void)write(STDERR_FILENO, passed, sizeof(passed) - 1);
    }
    for (;;) {
        (void)nanosleep(&delay, 0);
    }
}
