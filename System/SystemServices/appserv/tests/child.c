#include <punos/syscall/syscall.h>

static volatile i32 stopped;
static void stop(i32 signal) { (void)signal; stopped = 1; }

static i32 equal(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) { ++a; ++b; }
    return *a == *b;
}

int main(int argc, char **argv, char **envp)
{
    if (argc < 2 || envp[0] == 0 || !equal(envp[0], "MODE=fixed") || envp[1] != 0)
        return 91;
    if (equal(argv[1], "exit")) return 37;
    if (equal(argv[1], "signal")) {
        (void)kill(getpid(), SIGTERM);
        return 92;
    }
    if (argc != 3 || !equal(argv[1], "wait")) return 93;
    struct sigaction action = { .sa_handler = stop };
    if (sigaction(SIGTERM, &action, 0) != 0) return 94;
    i32 marker = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (marker < 0) return 95;
    (void)write(marker, "ready", 5);
    (void)close(marker);
    while (!stopped) {
        const struct timespec delay = { 0, 10000000 };
        (void)nanosleep(&delay, 0);
    }
    return 23;
}
