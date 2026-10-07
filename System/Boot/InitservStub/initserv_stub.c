#include <punos/syscall/syscall.h>

/*
 * Smoke-test stand-in for /punos/bin/initserv. It runs every self-test packaged
 * in the initramfs, reports each result on the console and then stays alive.
 */

struct SelfTest {
    const char *path;
    const char *passed;
    const char *failed;
};

static const struct SelfTest self_tests[] = {
    { "/punos/bin/syscall-selftest",
      "PUNos initserv stub: syscall self-test passed\n",
      "PUNos initserv stub: syscall self-test failed\n" },
    { "/punos/bin/libcore-selftest",
      "PUNos initserv stub: libcore self-test passed\n",
      "PUNos initserv stub: libcore self-test failed\n" },
    { "/punos/bin/libmath-selftest",
      "PUNos initserv stub: libmath self-test passed\n",
      "PUNos initserv stub: libmath self-test failed\n" },
    { "/punos/bin/libmem-selftest",
      "PUNos initserv stub: libmem self-test passed\n",
      "PUNos initserv stub: libmem self-test failed\n" },
    { "/punos/bin/libstr-selftest",
      "PUNos initserv stub: libstr self-test passed\n",
      "PUNos initserv stub: libstr self-test failed\n" },
    { "/punos/bin/libfile-selftest",
      "PUNos initserv stub: libfile self-test passed\n",
      "PUNos initserv stub: libfile self-test failed\n" },
    { "/punos/bin/libtime-selftest",
      "PUNos initserv stub: libtime self-test passed\n",
      "PUNos initserv stub: libtime self-test failed\n" },
    { "/punos/bin/libprocess-selftest",
      "PUNos initserv stub: libprocess self-test passed\n",
      "PUNos initserv stub: libprocess self-test failed\n" },
    { "/punos/bin/libthread-selftest",
      "PUNos initserv stub: libthread self-test passed\n",
      "PUNos initserv stub: libthread self-test failed\n" },
    { "/punos/bin/libsync-selftest",
      "PUNos initserv stub: libsync self-test passed\n",
      "PUNos initserv stub: libsync self-test failed\n" },
    { "/punos/bin/libeventloop-selftest",
      "PUNos initserv stub: libeventloop self-test passed\n",
      "PUNos initserv stub: libeventloop self-test failed\n" },
    { "/punos/bin/libinputdevice-selftest",
      "PUNos initserv stub: libinputdevice self-test passed\n",
      "PUNos initserv stub: libinputdevice self-test failed\n" },
    { "/punos/bin/libv4l2-selftest",
      "PUNos initserv stub: libv4l2 self-test passed\n",
      "PUNos initserv stub: libv4l2 self-test failed\n" },
    { "/punos/bin/ipc-selftest",
      "PUNos initserv stub: ipc self-test passed\n",
      "PUNos initserv stub: ipc self-test failed\n" },
    { "/punos/bin/log-selftest",
      "PUNos initserv stub: log self-test passed\n",
      "PUNos initserv stub: log self-test failed\n" },
    { "/punos/bin/input-selftest",
      "PUNos initserv stub: input self-test passed\n",
      "PUNos initserv stub: input self-test failed\n" },
    { "/punos/bin/camera-selftest",
      "PUNos initserv stub: camera self-test passed\n",
      "PUNos initserv stub: camera self-test failed\n" },
};

static size_t text_length(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static void print(const char *text)
{
    (void)write(STDERR_FILENO, text, text_length(text));
}

static i32 run_self_test(const char *path)
{
    char *const test_argv[] = { (char *)path, 0 };
    char *const empty_env[] = { 0 };
    pid_t child;
    i32 status = -1;

    child = fork();
    if (child == 0) {
        (void)execve(path, test_argv, empty_env);
        exit(126);
    }
    return child > 0 && waitpid(child, &status, 0) == child && status == 0;
}

int main(int argc, char **argv, char **envp)
{
    static const struct timespec delay = { .tv_sec = 60, .tv_nsec = 0 };
    size_t index;

    (void)argc;
    (void)argv;
    (void)envp;
    print("PUNos initserv stub: smoke-test process is running\n");
    for (index = 0; index < sizeof(self_tests) / sizeof(self_tests[0]); ++index) {
        if (run_self_test(self_tests[index].path)) {
            print(self_tests[index].passed);
        } else {
            print(self_tests[index].failed);
        }
    }
    for (;;) {
        (void)nanosleep(&delay, 0);
    }
}
