#include <sys/time.h>

#include <punos/syscall/syscall.h>

#include "internal.h"

#define NANOSECONDS_PER_MILLISECOND 1000000

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

/*
 * Readings before the clock's zero point (negative seconds) are not
 * representable in the unsigned time types.
 */
ErrorCode libtime_internal_floor_milliseconds(const struct timespec *value, u64 *out_ms)
{
    usize seconds_ms;

    if (value->tv_sec < 0) {
        return CORE_OVERFLOW;
    }
    if (core_size_multiply((usize)value->tv_sec, TIME_MILLISECONDS_PER_SECOND,
                           &seconds_ms) != CORE_OK) {
        return CORE_OVERFLOW;
    }
    return core_size_add(seconds_ms,
                         (usize)(value->tv_nsec / NANOSECONDS_PER_MILLISECOND),
                         out_ms);
}

/*
 * Rounds up, so a caller that sleeps again for the remaining time never wakes
 * up early.
 */
u64 libtime_internal_ceil_milliseconds(const struct timespec *value)
{
    return (u64)value->tv_sec * TIME_MILLISECONDS_PER_SECOND +
           (u64)(value->tv_nsec + NANOSECONDS_PER_MILLISECOND - 1) /
               NANOSECONDS_PER_MILLISECOND;
}

static ErrorCode read_clock(clockid_t clock, u64 *out_ms)
{
    struct timespec now;

    if (clock_gettime(clock, &now) != 0) {
        return core_error_from_errno(errno);
    }
    return libtime_internal_floor_milliseconds(&now, out_ms);
}

/* ------------------------------------------------------------------------ */
/* Clocks                                                                   */
/* ------------------------------------------------------------------------ */

ErrorCode time_monotonic_now(MonotonicTime *out_time)
{
    if (out_time == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    return read_clock(CLOCK_MONOTONIC, out_time);
}

ErrorCode time_wall_clock_now(WallClockTime *out_time)
{
    if (out_time == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    return read_clock(CLOCK_REALTIME, out_time);
}

/* ------------------------------------------------------------------------ */
/* Sleeping                                                                 */
/* ------------------------------------------------------------------------ */

ErrorCode time_sleep(DurationMs duration, DurationMs *out_remaining)
{
    struct timespec requested;
    struct timespec remaining;
    i32 error;

    if (out_remaining == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    /* u64 milliseconds / 1000 always fits the i64 seconds of timespec. */
    requested.tv_sec = (time_t)(duration / TIME_MILLISECONDS_PER_SECOND);
    requested.tv_nsec =
        (i64)(duration % TIME_MILLISECONDS_PER_SECOND) * NANOSECONDS_PER_MILLISECOND;

    if (nanosleep(&requested, &remaining) == 0) {
        *out_remaining = 0;
        return CORE_OK;
    }

    error = errno;
    if (error != EINTR) {
        return core_error_from_errno(error);
    }

    /* remaining never exceeds the request, so it converts without overflow. */
    *out_remaining = libtime_internal_ceil_milliseconds(&remaining);
    return CORE_INTERRUPTED;
}

/* ------------------------------------------------------------------------ */
/* Deadlines                                                                */
/* ------------------------------------------------------------------------ */

ErrorCode time_deadline_after(DurationMs duration, Deadline *out_deadline)
{
    MonotonicTime now;
    ErrorCode result;

    if (out_deadline == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = time_monotonic_now(&now);
    if (result != CORE_OK) {
        return result;
    }
    return core_size_add(now, duration, out_deadline);
}

ErrorCode time_deadline_expired(Deadline deadline, bool *out_expired)
{
    MonotonicTime now;
    ErrorCode result;

    if (out_expired == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = time_monotonic_now(&now);
    if (result != CORE_OK) {
        return result;
    }
    *out_expired = now >= deadline;
    return CORE_OK;
}

ErrorCode time_deadline_remaining(Deadline deadline, DurationMs *out_remaining)
{
    MonotonicTime now;
    ErrorCode result;

    if (out_remaining == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = time_monotonic_now(&now);
    if (result != CORE_OK) {
        return result;
    }
    *out_remaining = now >= deadline ? 0 : deadline - now;
    return CORE_OK;
}
