/*
 * Runtime test of libtime against DevDoc/SystemLibs/libtime.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 * Check numbers are grouped: 1-19 clocks, 20-39 sleep, 40-59 deadlines,
 * 60-79 millisecond conversions.
 *
 * Timing checks use generous upper bounds: under QEMU without KVM the system
 * is slow and a sleep may overrun by a lot, but it must never end early.
 */
#define CHECK_TEST_NAME "libtime"
#include "../../testing/check.h"

#include <sys/time.h>

#include "../src/internal.h"

/* 2020-01-01T00:00:00Z. Any correctly set clock is later than this. */
#define WALL_CLOCK_2020_MS ((WallClockTime)1577836800000ULL)
#define SLACK_MS 5000

static MonotonicTime now(void)
{
    MonotonicTime value = 0;

    EXPECT(1, time_monotonic_now(&value) == CORE_OK);
    return value;
}

static void test_clocks(void)
{
    MonotonicTime first;
    MonotonicTime second;
    WallClockTime wall = 0;

    first = now();
    second = now();
    EXPECT(2, second >= first);
    EXPECT(3, time_monotonic_now(NULL) == CORE_INVALID_ARGUMENT);

    EXPECT(4, time_wall_clock_now(&wall) == CORE_OK);
    EXPECT(5, wall > WALL_CLOCK_2020_MS);
    /*
     * The two clocks are different: the monotonic one counts from boot, so it
     * is far below any wall-clock time since 2020 (it would take 50 years of
     * uptime to get there).
     */
    EXPECT(7, now() < WALL_CLOCK_2020_MS);
    EXPECT(6, time_wall_clock_now(NULL) == CORE_INVALID_ARGUMENT);
}

static void test_sleep(void)
{
    DurationMs remaining = 12345;
    MonotonicTime start;
    MonotonicTime elapsed;

    start = now();
    EXPECT(20, time_sleep(50, &remaining) == CORE_OK);
    elapsed = now() - start;
    EXPECT(21, remaining == 0);
    EXPECT(22, elapsed >= 50);
    EXPECT(23, elapsed < 50 + SLACK_MS);

    /* Longer than a second: exercises the seconds part of the conversion. */
    start = now();
    EXPECT(24, time_sleep(1100, &remaining) == CORE_OK);
    elapsed = now() - start;
    EXPECT(25, remaining == 0 && elapsed >= 1100 && elapsed < 1100 + SLACK_MS);

    remaining = 7;
    EXPECT(26, time_sleep(0, &remaining) == CORE_OK && remaining == 0);
    EXPECT(27, time_sleep(1, NULL) == CORE_INVALID_ARGUMENT);
}

static void on_signal(i32 signal)
{
    (void)signal;
}

/*
 * A child keeps signalling the parent every 100 ms, so the parent's sleep is
 * interrupted even if the first signal arrives before the sleep starts.
 */
static void test_sleep_interrupted(void)
{
    const struct timespec period = { .tv_sec = 0, .tv_nsec = 100000000 };
    struct sigaction action = { 0 };
    struct sigaction previous;
    DurationMs remaining = 0;
    MonotonicTime start;
    MonotonicTime elapsed;
    ErrorCode result;
    pid_t parent = getpid();
    pid_t child;
    i32 status;

    action.sa_handler = on_signal;
    EXPECT(30, sigaction(SIGCHLD, &action, &previous) == 0);

    child = fork();
    if (child == 0) {
        for (;;) {
            (void)nanosleep(&period, NULL);
            (void)kill(parent, SIGCHLD);
        }
    }
    EXPECT(31, child > 0);

    start = now();
    result = time_sleep(10000, &remaining);
    elapsed = now() - start;

    (void)kill(child, SIGKILL);
    (void)waitpid(child, &status, 0);
    (void)sigaction(SIGCHLD, &previous, NULL);

    EXPECT(32, result == CORE_INTERRUPTED);
    EXPECT(33, remaining > 0 && remaining <= 10000);
    EXPECT(34, elapsed < 10000);
    /*
     * Slept time plus remaining time covers the whole request. elapsed is a
     * difference of truncated readings, so it is more than the real sleep
     * minus 1 ms; remaining is rounded up. Together they reach 10000 exactly
     * only if remaining is rounded up, not down.
     */
    EXPECT(35, elapsed + remaining >= 10000);
}

static void test_deadlines(void)
{
    Deadline deadline = 0;
    DurationMs remaining = 1;
    DurationMs slept = 0;
    bool expired = false;

    /* A deadline of "now" has already expired. */
    EXPECT(40, time_deadline_after(0, &deadline) == CORE_OK);
    EXPECT(41, time_deadline_expired(deadline, &expired) == CORE_OK && expired);
    EXPECT(42, time_deadline_remaining(deadline, &remaining) == CORE_OK && remaining == 0);

    /* A deadline far in the past. */
    EXPECT(43, time_deadline_expired(0, &expired) == CORE_OK && expired);
    EXPECT(44, time_deadline_remaining(0, &remaining) == CORE_OK && remaining == 0);

    /* A future deadline: not expired, remaining close to the duration. */
    EXPECT(45, time_deadline_after(10000, &deadline) == CORE_OK);
    EXPECT(46, time_deadline_expired(deadline, &expired) == CORE_OK && !expired);
    EXPECT(47, time_deadline_remaining(deadline, &remaining) == CORE_OK);
    EXPECT(48, remaining <= 10000 && remaining > 10000 - SLACK_MS);

    /* Sleeping for the remaining time reaches the deadline. */
    EXPECT(49, time_deadline_after(200, &deadline) == CORE_OK);
    EXPECT(50, time_deadline_remaining(deadline, &remaining) == CORE_OK);
    EXPECT(51, time_sleep(remaining, &slept) == CORE_OK);
    EXPECT(52, time_deadline_expired(deadline, &expired) == CORE_OK && expired);

    EXPECT(53, time_deadline_after(USIZE_MAX, &deadline) == CORE_OVERFLOW);
    EXPECT(54, time_deadline_after(1, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(55, time_deadline_expired(0, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(56, time_deadline_remaining(0, NULL) == CORE_INVALID_ARGUMENT);
}

static bool floor_is(time_t seconds, i64 nanoseconds, u64 expected)
{
    struct timespec value = { .tv_sec = seconds, .tv_nsec = nanoseconds };
    u64 result = 0;

    return libtime_internal_floor_milliseconds(&value, &result) == CORE_OK &&
           result == expected;
}

static u64 ceil_of(time_t seconds, i64 nanoseconds)
{
    struct timespec value = { .tv_sec = seconds, .tv_nsec = nanoseconds };

    return libtime_internal_ceil_milliseconds(&value);
}

/* Exact rounding of both conversions, independent of real clocks. */
static void test_conversions(void)
{
    struct timespec value;
    u64 result;

    /* Clock readings are truncated. */
    EXPECT(60, floor_is(0, 0, 0));
    EXPECT(61, floor_is(0, 999999, 0));
    EXPECT(62, floor_is(0, 1000000, 1));
    EXPECT(63, floor_is(1, 999999999, 1999));
    EXPECT(64, floor_is(1234, 5000000, 1234005));

    value.tv_sec = -1;
    value.tv_nsec = 0;
    EXPECT(65, libtime_internal_floor_milliseconds(&value, &result) == CORE_OVERFLOW);
    /* Seconds whose milliseconds do not fit in u64. */
    value.tv_sec = (time_t)(USIZE_MAX / 1000 + 1);
    EXPECT(66, libtime_internal_floor_milliseconds(&value, &result) == CORE_OVERFLOW);

    /* Remaining sleep is rounded up: any leftover nanosecond counts as 1 ms. */
    EXPECT(70, ceil_of(0, 0) == 0);
    EXPECT(71, ceil_of(0, 1) == 1);
    EXPECT(72, ceil_of(0, 1000000) == 1);
    EXPECT(73, ceil_of(0, 1000001) == 2);
    EXPECT(74, ceil_of(2, 0) == 2000);
    EXPECT(75, ceil_of(2, 999999999) == 3000);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    test_conversions();
    test_clocks();
    test_sleep();
    test_sleep_interrupted();
    test_deadlines();

    check_passed();
    return 0;
}
