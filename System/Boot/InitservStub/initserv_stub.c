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
    { "/punos/bin/librandom-selftest",
      "PUNos initserv stub: librandom self-test passed\n",
      "PUNos initserv stub: librandom self-test failed\n" },
    { "/punos/bin/libgraphics-selftest",
      "PUNos initserv stub: libgraphics self-test passed\n",
      "PUNos initserv stub: libgraphics self-test failed\n" },
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
    { "/punos/bin/displayserv-selftest",
      "PUNos initserv stub: displayserv self-test passed\n",
      "PUNos initserv stub: displayserv self-test failed\n" },
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

static i32 make_token(char token_text[65])
{
    static const char hex[] = "0123456789abcdef";
    u8 token[32];
    size_t index;

    if (getrandom(token, sizeof(token), 0) != (ssize_t)sizeof(token)) return 0;
    for (index = 0; index < sizeof(token); ++index) {
        token_text[index * 2] = hex[token[index] >> 4];
        token_text[index * 2 + 1] = hex[token[index] & 15];
    }
    token_text[64] = '\0';
    return 1;
}

static i32 make_token_argument(const char *prefix, const char token_text[65],
                               char argument[96])
{
    size_t index = 0;
    size_t token_index;

    while (prefix[index] != '\0') {
        if (index + 1 >= 96) return 0;
        argument[index] = prefix[index];
        ++index;
    }
    for (token_index = 0; token_index < 64; ++token_index) {
        if (index + 1 >= 96) return 0;
        argument[index++] = token_text[token_index];
    }
    argument[index] = '\0';
    return 1;
}

static pid_t start_displayserv(char presenter_argument[96])
{
    static char path[] = "/punos/bin/displayserv";
    char *const service_argv[] = {
        path, (char *)"--backend=software", presenter_argument, 0
    };
    char *const empty_env[] = { 0 };
    pid_t child = fork();

    if (child == 0) {
        (void)execve(path, service_argv, empty_env);
        exit(126);
    }
    return child;
}

static pid_t start_compositorserv(char presenter_argument[96],
                                  char controller_argument[96])
{
    static char path[] = "/punos/bin/compositorserv";
    char *const service_argv[] = {
        path, presenter_argument, controller_argument, 0
    };
    char *const empty_env[] = { 0 };
    pid_t child = fork();

    if (child == 0) {
        (void)execve(path, service_argv, empty_env);
        exit(126);
    }
    return child;
}

static pid_t start_windowserv(char controller_argument[96],
                              char shell_argument[96])
{
    static char path[] = "/punos/bin/windowserv";
    char *const service_argv[] = {
        path, controller_argument, shell_argument, 0
    };
    char *const empty_env[] = { 0 };
    pid_t child = fork();

    if (child == 0) {
        (void)execve(path, service_argv, empty_env);
        exit(126);
    }
    return child;
}

int main(int argc, char **argv, char **envp)
{
    static const struct timespec delay = { .tv_sec = 60, .tv_nsec = 0 };
    char presenter_token[65];
    char controller_token[65];
    char shell_token[65];
    char presenter_argument[96];
    char controller_argument[96];
    char shell_argument[96];
    pid_t displayserv_pid;
    pid_t compositorserv_pid;
    pid_t windowserv_pid;
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
    if (!make_token(presenter_token) || !make_token(controller_token) ||
        !make_token(shell_token) ||
        !make_token_argument("--presenter-token=", presenter_token,
                             presenter_argument) ||
        !make_token_argument("--controller-token=", controller_token,
                             controller_argument) ||
        !make_token_argument("--shell-token=", shell_token,
                             shell_argument)) {
        print("PUNos initserv stub: GUI token generation failed\n");
        return 3;
    }
    displayserv_pid = start_displayserv(presenter_argument);
    if (displayserv_pid <= 0) {
        print("PUNos initserv stub: displayserv start failed\n");
        return 3;
    }
    print("PUNos initserv stub: displayserv started\n");
    compositorserv_pid = start_compositorserv(presenter_argument,
                                              controller_argument);
    if (compositorserv_pid <= 0) {
        print("PUNos initserv stub: compositorserv start failed\n");
        return 3;
    }
    print("PUNos initserv stub: compositorserv started\n");
    windowserv_pid = start_windowserv(controller_argument, shell_argument);
    if (windowserv_pid <= 0) {
        print("PUNos initserv stub: windowserv start failed\n");
        return 3;
    }
    print("PUNos initserv stub: windowserv started\n");
    for (;;) {
        i32 status;
        pid_t child = waitpid(displayserv_pid, &status, WNOHANG);
        if (child == displayserv_pid) {
            print("PUNos initserv stub: displayserv exited, restarting\n");
            displayserv_pid = start_displayserv(presenter_argument);
            if (displayserv_pid <= 0) return 3;
        }
        child = waitpid(compositorserv_pid, &status, WNOHANG);
        if (child == compositorserv_pid) {
            print("PUNos initserv stub: compositorserv exited, restarting\n");
            compositorserv_pid = start_compositorserv(
                presenter_argument, controller_argument);
            if (compositorserv_pid <= 0) return 3;
        }
        child = waitpid(windowserv_pid, &status, WNOHANG);
        if (child == windowserv_pid) {
            print("PUNos initserv stub: windowserv exited, restarting\n");
            windowserv_pid = start_windowserv(controller_argument,
                                              shell_argument);
            if (windowserv_pid <= 0) return 3;
        }
        (void)nanosleep(&delay, 0);
    }
}
