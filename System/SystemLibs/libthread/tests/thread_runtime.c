/*
 * Runtime test of libthread against DevDoc/SystemLibs/libthread.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 * Check numbers are grouped: 1-9 current id, 10-29 create and join,
 * 30-49 per-thread state (errno, stack alignment, id), 50-59 join really
 * waits, 60-69 resources released, 70-89 many threads, 90-99 signals during
 * join, 100-119 self-join and invalid arguments.
 *
 * Checks that run inside a created thread cannot exit the process directly
 * with a meaningful code (exit() ends every thread); they report through the
 * thread's result instead and the main thread checks it.
 */
#define CHECK_TEST_NAME "libthread"
#include "../../testing/check.h"

#include <sys/thread.h>

#include "../src/internal.h"

#define PAGE 4096
#define THREADS 16
#define INCREMENTS 10000

static void sleep_ms(i64 milliseconds)
{
    struct timespec delay = { .tv_sec = milliseconds / 1000,
                              .tv_nsec = (milliseconds % 1000) * 1000000 };
    (void)nanosleep(&delay, NULL);
}

static i64 now_ms(void)
{
    struct timespec now;

    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static bool page_is_mapped(const void *address)
{
    uintptr_t page = (uintptr_t)address & ~(uintptr_t)(PAGE - 1);

    return mprotect((void *)page, PAGE, PROT_READ | PROT_WRITE) == 0 || errno != ENOMEM;
}

/* ------------------------------------------------------------------------ */
/* Thread entries                                                           */
/* ------------------------------------------------------------------------ */

static i32 return_argument(void *argument)
{
    return (i32)(uintptr_t)argument;
}

static i32 write_through(void *argument)
{
    *(i32 *)argument = 1234;
    return THREAD_EXIT_SUCCESS;
}

struct StateProbe {
    ThreadId main_id;
    i32 *main_errno_location;
};

/* Returns 0 if the thread's own state is right, otherwise which check failed. */
static i32 check_thread_state(void *argument)
{
    struct StateProbe *probe = argument;
    _Alignas(16) u8 aligned[16];
    ThreadId id = 0;

    if (errno != 0) {
        return 31; /* errno of a new thread starts at 0 */
    }
    if (sys_errno_location() == probe->main_errno_location) {
        return 32; /* errno is not shared with the creating thread */
    }
    errno = ENOMEM;
    if (errno != ENOMEM) {
        return 33;
    }
    if (thread_current_id(&id) != CORE_OK || id <= 0 || id == probe->main_id) {
        return 34;
    }
    if (((uintptr_t)aligned & 15) != 0) {
        return 35; /* the ABI stack alignment holds in the new thread */
    }
    return 0;
}

static volatile i32 finished_flag;

static i32 sleep_then_flag(void *argument)
{
    sleep_ms((i64)(uintptr_t)argument);
    __atomic_store_n(&finished_flag, 1, __ATOMIC_RELEASE);
    return 7;
}

static u64 counter;

static i32 increment_many(void *argument)
{
    i32 index;

    for (index = 0; index < INCREMENTS; ++index) {
        __atomic_fetch_add(&counter, 1, __ATOMIC_RELAXED);
    }
    return (i32)(uintptr_t)argument;
}

static ThreadHandle shared_handle;

/* Waits until the main thread publishes this thread's handle, then joins itself. */
static i32 join_self(void *argument)
{
    ThreadHandle own;
    ThreadExitStatus status;

    (void)argument;
    while ((own = __atomic_load_n(&shared_handle, __ATOMIC_ACQUIRE)) == NULL) {
        sleep_ms(1);
    }
    return thread_join(&own, &status) == CORE_INVALID_STATE && own != NULL ? 0 : 1;
}

/* ------------------------------------------------------------------------ */
/* Tests                                                                    */
/* ------------------------------------------------------------------------ */

static void test_current_id(void)
{
    ThreadId id = 0;

    EXPECT(1, thread_current_id(&id) == CORE_OK && id == getpid());
    EXPECT(2, thread_current_id(NULL) == CORE_INVALID_ARGUMENT);
}

static void test_create_and_join(void)
{
    ThreadHandle thread = NULL;
    ThreadExitStatus status = -1;
    i32 target = 0;

    EXPECT(10, thread_create(return_argument, (void *)(uintptr_t)42, &thread) == CORE_OK);
    EXPECT(11, thread != NULL);
    EXPECT(12, thread_join(&thread, &status) == CORE_OK);
    EXPECT(13, thread == NULL && status == 42);

    /* Negative results and the argument pointer pass through unchanged. */
    EXPECT(14, thread_create(return_argument, (void *)(uintptr_t)(u32)-5, &thread) == CORE_OK);
    EXPECT(15, thread_join(&thread, &status) == CORE_OK && status == -5);
    EXPECT(16, thread_create(write_through, &target, &thread) == CORE_OK);
    EXPECT(17, thread_join(&thread, &status) == CORE_OK &&
                   status == THREAD_EXIT_SUCCESS && target == 1234);
}

static void test_thread_state(void)
{
    struct StateProbe probe;
    ThreadHandle thread;
    ThreadExitStatus status = -1;

    probe.main_errno_location = sys_errno_location();
    EXPECT(30, thread_current_id(&probe.main_id) == CORE_OK);
    errno = EPERM;
    EXPECT(36, thread_create(check_thread_state, &probe, &thread) == CORE_OK);
    EXPECT(37, thread_join(&thread, &status) == CORE_OK);
    /* status is 0 or the number of the check that failed inside the thread. */
    EXPECT(31, status != 31);
    EXPECT(32, status != 32);
    EXPECT(33, status != 33);
    EXPECT(34, status != 34);
    EXPECT(35, status != 35);
    EXPECT(38, status == 0);
    /* The thread's errno did not leak into this thread. */
    EXPECT(39, errno == EPERM);
}

static void test_join_waits(void)
{
    ThreadHandle thread;
    ThreadExitStatus status = 0;
    i64 start;

    finished_flag = 0;
    start = now_ms();
    EXPECT(50, thread_create(sleep_then_flag, (void *)(uintptr_t)300, &thread) == CORE_OK);
    EXPECT(51, thread_join(&thread, &status) == CORE_OK && status == 7);
    EXPECT(52, __atomic_load_n(&finished_flag, __ATOMIC_ACQUIRE) == 1);
    EXPECT(53, now_ms() - start >= 300);
}

static void test_resources_released(void)
{
    ThreadHandle thread;
    ThreadExitStatus status;
    u8 *stack;
    u8 *handle_page;

    EXPECT(60, thread_create(return_argument, NULL, &thread) == CORE_OK);
    stack = thread->stack.data;
    handle_page = thread->handle_block.data;
    EXPECT(61, thread->stack.size == LIBTHREAD_INTERNAL_STACK_SIZE);
    EXPECT(62, thread_join(&thread, &status) == CORE_OK);
    EXPECT(63, !page_is_mapped(stack));
    EXPECT(64, !page_is_mapped(stack + LIBTHREAD_INTERNAL_STACK_SIZE - PAGE));
    EXPECT(65, !page_is_mapped(handle_page));
}

static void test_many_threads(void)
{
    ThreadHandle threads[THREADS];
    ThreadExitStatus status;
    uintptr_t index;
    i32 round;

    counter = 0;
    for (index = 0; index < THREADS; ++index) {
        EXPECT(70, thread_create(increment_many, (void *)index, &threads[index]) == CORE_OK);
    }
    for (index = 0; index < THREADS; ++index) {
        EXPECT(71, thread_join(&threads[index], &status) == CORE_OK &&
                       status == (i32)index);
    }
    EXPECT(72, counter == (u64)THREADS * INCREMENTS);

    /* Many short-lived threads: no leak or race in create/join. */
    for (round = 0; round < 200; ++round) {
        ThreadHandle thread;

        EXPECT(73, thread_create(return_argument, (void *)(uintptr_t)round, &thread) == CORE_OK);
        EXPECT(74, thread_join(&thread, &status) == CORE_OK && status == round);
    }
}

static void on_signal(i32 signal)
{
    (void)signal;
}

/*
 * A helper process signals this process every 50 ms while the main thread is
 * in thread_join(). The futex wait is interrupted again and again; join must
 * keep waiting until the thread has really finished.
 */
static void test_join_with_signals(void)
{
    const struct timespec period = { .tv_sec = 0, .tv_nsec = 50000000 };
    struct sigaction action = { 0 };
    struct sigaction previous;
    ThreadHandle thread;
    ThreadExitStatus status = 0;
    pid_t parent = getpid();
    pid_t signaller;
    i32 helper_status;
    ErrorCode result;
    i64 start;

    action.sa_handler = on_signal;
    EXPECT(90, sigaction(SIGCHLD, &action, &previous) == 0);
    finished_flag = 0;
    start = now_ms();
    EXPECT(91, thread_create(sleep_then_flag, (void *)(uintptr_t)500, &thread) == CORE_OK);

    signaller = fork();
    if (signaller == 0) {
        for (;;) {
            (void)nanosleep(&period, NULL);
            (void)kill(parent, SIGCHLD);
        }
    }
    EXPECT(92, signaller > 0);

    result = thread_join(&thread, &status);
    (void)kill(signaller, SIGKILL);
    (void)waitpid(signaller, &helper_status, 0);
    (void)sigaction(SIGCHLD, &previous, NULL);

    EXPECT(93, result == CORE_OK && thread == NULL && status == 7);
    EXPECT(94, __atomic_load_n(&finished_flag, __ATOMIC_ACQUIRE) == 1);
    EXPECT(95, now_ms() - start >= 500);
}

static void test_invalid(void)
{
    ThreadHandle thread = NULL;
    ThreadHandle joined;
    ThreadExitStatus status = -1;

    /* A thread joining itself gets CORE_INVALID_STATE instead of hanging. */
    shared_handle = NULL;
    EXPECT(100, thread_create(join_self, NULL, &thread) == CORE_OK);
    joined = thread;
    __atomic_store_n(&shared_handle, thread, __ATOMIC_RELEASE);
    EXPECT(101, thread_join(&joined, &status) == CORE_OK && status == 0);

    EXPECT(102, thread_create(NULL, NULL, &thread) == CORE_INVALID_ARGUMENT);
    EXPECT(103, thread_create(return_argument, NULL, NULL) == CORE_INVALID_ARGUMENT);
    thread = NULL;
    EXPECT(104, thread_join(&thread, &status) == CORE_INVALID_ARGUMENT);
    EXPECT(105, thread_join(NULL, &status) == CORE_INVALID_ARGUMENT);
    EXPECT(106, thread_create(return_argument, NULL, &thread) == CORE_OK);
    EXPECT(107, thread_join(&thread, NULL) == CORE_INVALID_ARGUMENT && thread != NULL);
    EXPECT(108, thread_join(&thread, &status) == CORE_OK);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    test_current_id();
    test_create_and_join();
    test_thread_state();
    test_join_waits();
    test_resources_released();
    test_many_threads();
    test_join_with_signals();
    test_invalid();

    check_passed();
    return 0;
}
