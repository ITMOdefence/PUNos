#ifndef PUNOS_SYS_TIME_H
#define PUNOS_SYS_TIME_H

/*
 * libtime (sys/time) - current time, durations, deadlines and sleeping.
 *
 * Specification: DevDoc/SystemLibs/libtime.md
 *
 * All times and durations are whole milliseconds in u64. There are two clocks:
 *
 *   MonotonicTime  CLOCK_MONOTONIC. Never jumps; use it for timeouts,
 *                  deadlines and measuring intervals. Its zero point is
 *                  arbitrary (in practice, boot), so it is meaningless as a date.
 *   WallClockTime  CLOCK_REALTIME, milliseconds since the Unix epoch. Use it
 *                  only to label events: it can be changed at any moment, so
 *                  it must never drive a deadline.
 *
 * Nanosecond clock readings are truncated to whole milliseconds.
 * The only syscall wrappers used are clock_gettime() and nanosleep().
 * All operations return ErrorCode; see <sys/core.h> for the conventions.
 *
 * Timers that deliver events to an event loop are not part of libtime; they
 * belong to libeventloop.
 */

#include <sys/core.h>

typedef u64 DurationMs;
typedef u64 MonotonicTime;
typedef u64 WallClockTime;

/* Absolute moment on the monotonic clock at which a timeout expires. */
typedef MonotonicTime Deadline;

#define TIME_MILLISECONDS_PER_SECOND ((u64)1000)

/*
 * Stores the current monotonic time in *out_time.
 *
 * Errors: CORE_INVALID_ARGUMENT if out_time is NULL; codes from
 * core_error_from_errno() if clock_gettime() fails.
 */
ErrorCode time_monotonic_now(MonotonicTime *out_time);

/*
 * Stores the current wall-clock time (milliseconds since 1970-01-01 UTC) in
 * *out_time.
 *
 * Errors: CORE_INVALID_ARGUMENT if out_time is NULL; CORE_OVERFLOW if the
 * system clock is set before 1970; codes from core_error_from_errno() if
 * clock_gettime() fails.
 */
ErrorCode time_wall_clock_now(WallClockTime *out_time);

/*
 * Suspends the calling thread for duration milliseconds.
 *
 * On success stores 0 in *out_remaining. If a signal handler interrupts the
 * sleep, returns CORE_INTERRUPTED and stores the time still left, rounded up
 * to whole milliseconds, so it is not 0 while any time is left.
 *
 * Errors: CORE_INVALID_ARGUMENT if out_remaining is NULL; CORE_INTERRUPTED as
 * above; codes from core_error_from_errno() if nanosleep() fails otherwise.
 */
ErrorCode time_sleep(DurationMs duration, DurationMs *out_remaining);

/*
 * Stores now + duration on the monotonic clock in *out_deadline.
 *
 * Errors: CORE_INVALID_ARGUMENT if out_deadline is NULL; CORE_OVERFLOW if the
 * sum does not fit in u64; errors of time_monotonic_now().
 */
ErrorCode time_deadline_after(DurationMs duration, Deadline *out_deadline);

/*
 * Stores true in *out_expired if the monotonic clock has reached deadline
 * (now >= deadline), false otherwise.
 *
 * Errors: CORE_INVALID_ARGUMENT if out_expired is NULL; errors of
 * time_monotonic_now().
 */
ErrorCode time_deadline_expired(Deadline deadline, bool *out_expired);

/*
 * Stores the time left until deadline in *out_remaining, or 0 if the deadline
 * has already expired.
 *
 * Errors: CORE_INVALID_ARGUMENT if out_remaining is NULL; errors of
 * time_monotonic_now().
 */
ErrorCode time_deadline_remaining(Deadline deadline, DurationMs *out_remaining);

#endif
