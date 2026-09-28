/*
 * Runtime test of libprocess against DevDoc/SystemLibs/libprocess.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 * Check numbers are grouped: 1-9 current id, 10-39 start and wait,
 * 40-59 argv and environment, 60-79 poll and terminate,
 * 80-99 interrupted wait, 100-119 invalid arguments,
 * 120-139 environment format.
 *
 * The test starts itself (/proc/self/exe) as the child program. The first
 * argument selects what the child does:
 *   exit <N>        exit with status N
 *   check-launch    verify its own argv and environment, exit 0 if they match
 *   check-env       verify that the edge-case environment arrived unchanged
 *   sleep <ms>      sleep, then exit 0
 *   hang            sleep until a signal kills it
 */
#define CHECK_TEST_NAME "libprocess"
#include "../../testing/check.h"

#include <sys/process.h>
#include <sys/string.h>

#define SELF "/proc/self/exe"
#define SIGTERM_NUMBER 15

/* ------------------------------------------------------------------------ */
/* Child side                                                               */
/* ------------------------------------------------------------------------ */

static bool text_equals(const char *left, const char *right)
{
    usize index = 0;

    while (left[index] != '\0' && left[index] == right[index]) {
        ++index;
    }
    return left[index] == right[index];
}

static i32 parse_number(const char *text)
{
    i32 value = 0;

    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        ++text;
    }
    return value;
}

static void sleep_ms(i64 milliseconds)
{
    struct timespec delay = { .tv_sec = milliseconds / 1000,
                              .tv_nsec = (milliseconds % 1000) * 1000000 };
    (void)nanosleep(&delay, NULL);
}

/* argv must be exactly: SELF check-launch "two words" "" "Привет"; envp: A=1, B=two. */
static i32 check_launch(i32 argc, char **argv, char **envp)
{
    if (argc != 5 || !text_equals(argv[0], SELF) ||
        !text_equals(argv[1], "check-launch") || !text_equals(argv[2], "two words") ||
        !text_equals(argv[3], "") ||
        !text_equals(argv[4], "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82") ||
        argv[5] != NULL) {
        return 10;
    }
    if (envp[0] == NULL || !text_equals(envp[0], "A=1") || envp[1] == NULL ||
        !text_equals(envp[1], "B=two") || envp[2] != NULL) {
        return 11;
    }
    return 0;
}

/* envp must be exactly: NAME=value, EMPTY=, EQ=a=b. */
static i32 check_env(char **envp)
{
    if (envp[0] == NULL || !text_equals(envp[0], "NAME=value") ||
        envp[1] == NULL || !text_equals(envp[1], "EMPTY=") ||
        envp[2] == NULL || !text_equals(envp[2], "EQ=a=b") || envp[3] != NULL) {
        return 12;
    }
    return 0;
}

static bool run_child_mode(i32 argc, char **argv, char **envp, i32 *out_status)
{
    if (argc < 2) {
        return false;
    }
    if (text_equals(argv[1], "exit") && argc == 3) {
        *out_status = parse_number(argv[2]);
    } else if (text_equals(argv[1], "check-launch")) {
        *out_status = check_launch(argc, argv, envp);
    } else if (text_equals(argv[1], "check-env")) {
        *out_status = check_env(envp);
    } else if (text_equals(argv[1], "sleep") && argc == 3) {
        sleep_ms(parse_number(argv[2]));
        *out_status = 0;
    } else if (text_equals(argv[1], "hang")) {
        for (;;) {
            sleep_ms(1000);
        }
    } else {
        *out_status = 99;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Parent side                                                              */
/* ------------------------------------------------------------------------ */

static String self_path;

static String make(const char *text)
{
    String string;

    EXPECT(1, string_from_c_str(text, &string) == CORE_OK);
    return string;
}

/* Starts SELF with up to two mode arguments and no environment. */
static ErrorCode start_self(const char *mode, const char *parameter, ProcessHandle *out)
{
    String arguments[2];
    ProcessLaunch launch = { &self_path, arguments, 0, NULL, 0 };
    ErrorCode result;

    arguments[launch.argument_count++] = make(mode);
    if (parameter != NULL) {
        arguments[launch.argument_count++] = make(parameter);
    }
    result = process_start(&launch, out);
    while (launch.argument_count != 0) {
        (void)string_destroy(&arguments[--launch.argument_count]);
    }
    return result;
}

static bool exited_with(ProcessExitStatus status, i32 code)
{
    return status.kind == PROCESS_TERMINATION_KIND_EXITED && status.value == code;
}

static void test_current_id(void)
{
    ProcessId pid = 0;

    EXPECT(2, process_current_id(&pid) == CORE_OK && pid > 0 && pid == getpid());
    EXPECT(3, process_current_id(NULL) == CORE_INVALID_ARGUMENT);
}

static void test_start_and_wait(void)
{
    ProcessHandle process = NULL;
    ProcessExitStatus status = { PROCESS_TERMINATION_KIND_SIGNALED, -1 };
    String missing;
    ProcessLaunch launch = { NULL, NULL, 0, NULL, 0 };

    EXPECT(10, start_self("exit", "0", &process) == CORE_OK && process != NULL);
    EXPECT(11, process_wait(&process, &status) == CORE_OK);
    EXPECT(12, process == NULL && exited_with(status, PROCESS_EXIT_SUCCESS));

    EXPECT(13, start_self("exit", "42", &process) == CORE_OK);
    EXPECT(14, process_wait(&process, &status) == CORE_OK && exited_with(status, 42));

    /* The full 8-bit exit code range survives. */
    EXPECT(15, start_self("exit", "255", &process) == CORE_OK);
    EXPECT(16, process_wait(&process, &status) == CORE_OK && exited_with(status, 255));

    /* execve() fails in the child: start succeeds, the child exits with 127. */
    missing = make("/punos/no/such/program");
    launch.executable = &missing;
    EXPECT(17, process_start(&launch, &process) == CORE_OK);
    EXPECT(18, process_wait(&process, &status) == CORE_OK &&
                   exited_with(status, PROCESS_EXIT_EXEC_FAILED));
    EXPECT(19, string_destroy(&missing) == CORE_OK);

    /* Several children at once, waited for in reverse order. */
    {
        ProcessHandle children[4];
        const char *codes[4] = { "1", "2", "3", "4" };
        i32 index;

        for (index = 0; index < 4; ++index) {
            EXPECT(20, start_self("exit", codes[index], &children[index]) == CORE_OK);
        }
        for (index = 3; index >= 0; --index) {
            EXPECT(21, process_wait(&children[index], &status) == CORE_OK &&
                           exited_with(status, index + 1));
        }
    }
}

static void test_launch_vectors(void)
{
    String arguments[4];
    String environment[2];
    ProcessLaunch launch = { &self_path, arguments, 4, environment, 2 };
    ProcessHandle process = NULL;
    ProcessExitStatus status;
    usize index;

    arguments[0] = make("check-launch");
    arguments[1] = make("two words");
    arguments[2] = make("");
    arguments[3] = make("\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82");
    environment[0] = make("A=1");
    environment[1] = make("B=two");

    EXPECT(40, process_start(&launch, &process) == CORE_OK);
    /* The launch data only has to live during process_start(). */
    for (index = 0; index < 4; ++index) {
        EXPECT(41, string_destroy(&arguments[index]) == CORE_OK);
    }
    EXPECT(42, string_destroy(&environment[0]) == CORE_OK &&
                   string_destroy(&environment[1]) == CORE_OK);
    EXPECT(43, process_wait(&process, &status) == CORE_OK);
    /* 10: argv did not match, 11: environment did not match. */
    EXPECT(44, status.kind == PROCESS_TERMINATION_KIND_EXITED);
    EXPECT(45, status.value != 10);
    EXPECT(46, status.value != 11);
    EXPECT(47, status.value == 0);
}

/* Reads the total mapped size in pages, the first field of /proc/self/statm. */
static usize mapped_pages(void)
{
    char buffer[64];
    ssize_t length;
    usize pages = 0;
    ssize_t index;
    i32 fd = open("/proc/self/statm", O_RDONLY, 0);

    EXPECT(120, fd >= 0);
    length = read(fd, buffer, sizeof(buffer));
    (void)close(fd);
    EXPECT(121, length > 0);
    for (index = 0; index < length && buffer[index] >= '0' && buffer[index] <= '9';
         ++index) {
        pages = pages * 10 + (usize)(buffer[index] - '0');
    }
    return pages;
}

static void test_environment_format(void)
{
    /* Valid: a value may be empty and may contain further "=". */
    const char *valid[3] = { "NAME=value", "EMPTY=", "EQ=a=b" };
    /* Invalid alone: empty, no "=", empty name. */
    const char *invalid[3] = { "", "NAME", "=value" };
    String arguments[1];
    String environment[3];
    ProcessLaunch launch = { &self_path, arguments, 1, environment, 3 };
    ProcessHandle process = NULL;
    ProcessHandle untouched;
    ProcessExitStatus status;
    usize before;
    usize round;
    usize index;

    arguments[0] = make("check-env");
    for (index = 0; index < 3; ++index) {
        environment[index] = make(valid[index]);
    }
    EXPECT(122, process_start(&launch, &process) == CORE_OK);
    EXPECT(123, process_wait(&process, &status) == CORE_OK);
    /* 12: the environment did not arrive unchanged. */
    EXPECT(124, status.kind == PROCESS_TERMINATION_KIND_EXITED && status.value != 12);
    EXPECT(125, status.value == 0);

    /* Each invalid entry alone, then in the middle of valid entries. */
    for (index = 0; index < 3; ++index) {
        EXPECT(132, string_destroy(&environment[index]) == CORE_OK);
        environment[index] = make(invalid[index]);
    }
    untouched = (ProcessHandle)&launch;
    before = mapped_pages();
    for (round = 0; round < 64; ++round) {
        for (index = 0; index < 3; ++index) {
            launch.environment = &environment[index];
            launch.environment_count = 1;
            process = untouched;
            EXPECT(126, process_start(&launch, &process) == CORE_INVALID_ARGUMENT);
            EXPECT(127, process == untouched);
        }
    }
    for (index = 0; index < 3; ++index) {
        String mixed[3];

        mixed[0] = make(valid[0]);
        mixed[1] = make(invalid[index]);
        mixed[2] = make(valid[2]);
        launch.environment = mixed;
        launch.environment_count = 3;
        process = untouched;
        EXPECT(128, process_start(&launch, &process) == CORE_INVALID_ARGUMENT);
        EXPECT(129, process == untouched);
        EXPECT(130, string_destroy(&mixed[0]) == CORE_OK &&
                       string_destroy(&mixed[1]) == CORE_OK &&
                       string_destroy(&mixed[2]) == CORE_OK);
    }
    /* The argv/envp block and the handle block of every rejected start were freed. */
    EXPECT(131, mapped_pages() == before);

    for (index = 0; index < 3; ++index) {
        EXPECT(133, string_destroy(&environment[index]) == CORE_OK);
    }
    EXPECT(134, string_destroy(&arguments[0]) == CORE_OK);
}

static void test_poll_and_terminate(void)
{
    ProcessHandle process = NULL;
    ProcessExitStatus status = { PROCESS_TERMINATION_KIND_EXITED, -1 };
    bool finished = true;
    i32 polls = 0;

    /* A child that runs for a while: not finished at first, then finished. */
    EXPECT(60, start_self("sleep", "300", &process) == CORE_OK);
    EXPECT(61, process_poll(&process, &finished, &status) == CORE_OK && !finished);
    EXPECT(62, process != NULL);
    while (process != NULL && polls < 1000) {
        EXPECT(63, process_poll(&process, &finished, &status) == CORE_OK);
        if (!finished) {
            sleep_ms(20);
        }
        ++polls;
    }
    EXPECT(64, finished && process == NULL && exited_with(status, 0));

    /* terminate sends SIGTERM and does not wait; the handle stays valid. */
    EXPECT(65, start_self("hang", NULL, &process) == CORE_OK);
    EXPECT(66, process_terminate(process) == CORE_OK && process != NULL);
    EXPECT(67, process_wait(&process, &status) == CORE_OK);
    EXPECT(68, status.kind == PROCESS_TERMINATION_KIND_SIGNALED &&
                   status.value == SIGTERM_NUMBER);
}

static void on_signal(i32 signal)
{
    (void)signal;
}

/*
 * A helper child keeps signalling the test every 100 ms, so process_wait() on
 * a hanging child is interrupted and must return CORE_INTERRUPTED with the
 * handle intact.
 */
static void test_interrupted_wait(void)
{
    const struct timespec period = { .tv_sec = 0, .tv_nsec = 100000000 };
    struct sigaction action = { 0 };
    struct sigaction previous;
    ProcessHandle process = NULL;
    ProcessHandle kept;
    ProcessExitStatus status;
    ErrorCode result;
    pid_t parent = getpid();
    pid_t signaller;
    i32 helper_status;

    action.sa_handler = on_signal;
    EXPECT(80, sigaction(SIGCHLD, &action, &previous) == 0);
    EXPECT(81, start_self("hang", NULL, &process) == CORE_OK);
    kept = process;

    signaller = fork();
    if (signaller == 0) {
        for (;;) {
            (void)nanosleep(&period, NULL);
            (void)kill(parent, SIGCHLD);
        }
    }
    EXPECT(82, signaller > 0);

    result = process_wait(&process, &status);
    (void)kill(signaller, SIGKILL);
    (void)waitpid(signaller, &helper_status, 0);
    (void)sigaction(SIGCHLD, &previous, NULL);

    EXPECT(83, result == CORE_INTERRUPTED);
    EXPECT(84, process == kept);
    EXPECT(85, process_terminate(process) == CORE_OK);
    EXPECT(86, process_wait(&process, &status) == CORE_OK && process == NULL);
    EXPECT(87, status.kind == PROCESS_TERMINATION_KIND_SIGNALED);
}

static void test_invalid_arguments(void)
{
    ProcessHandle process = NULL;
    ProcessHandle null_handle = NULL;
    ProcessExitStatus status;
    String bogus = { "abc", 1 }; /* data[1] is not NUL */
    ProcessLaunch launch = { &self_path, NULL, 0, NULL, 0 };
    bool finished;
    pid_t stray;
    i32 stray_status;

    EXPECT(100, process_start(NULL, &process) == CORE_INVALID_ARGUMENT);
    EXPECT(101, process_start(&launch, NULL) == CORE_INVALID_ARGUMENT);
    launch.argument_count = 1; /* arguments is NULL */
    EXPECT(102, process_start(&launch, &process) == CORE_INVALID_ARGUMENT);
    launch.argument_count = 0;
    launch.environment_count = 1; /* environment is NULL */
    EXPECT(103, process_start(&launch, &process) == CORE_INVALID_ARGUMENT);
    launch.environment_count = 0;
    launch.executable = &bogus;
    EXPECT(104, process_start(&launch, &process) == CORE_INVALID_ARGUMENT);
    launch.executable = NULL;
    EXPECT(105, process_start(&launch, &process) == CORE_INVALID_ARGUMENT);
    launch.executable = &self_path;
    launch.arguments = &bogus;
    launch.argument_count = 1;
    EXPECT(106, process_start(&launch, &process) == CORE_INVALID_ARGUMENT);
    launch.argument_count = USIZE_MAX;
    EXPECT(107, process_start(&launch, &process) == CORE_OVERFLOW);

    EXPECT(108, process_wait(NULL, &status) == CORE_INVALID_ARGUMENT);
    EXPECT(109, process_wait(&null_handle, &status) == CORE_INVALID_ARGUMENT);
    EXPECT(110, process_poll(&null_handle, &finished, &status) == CORE_INVALID_ARGUMENT);
    EXPECT(111, process_terminate(NULL) == CORE_INVALID_ARGUMENT);

    /* No zombie or stray child was left behind by the failed starts. */
    stray = waitpid(-1, &stray_status, WNOHANG);
    EXPECT(112, stray < 0 && errno == ECHILD);
}

int main(int argc, char **argv, char **envp)
{
    i32 child_status;

    if (run_child_mode(argc, argv, envp, &child_status)) {
        return child_status;
    }

    self_path = make(SELF);
    test_current_id();
    test_start_and_wait();
    test_launch_vectors();
    test_environment_format();
    test_poll_and_terminate();
    test_interrupted_wait();
    test_invalid_arguments();
    EXPECT(9, string_destroy(&self_path) == CORE_OK);

    check_passed();
    return 0;
}
