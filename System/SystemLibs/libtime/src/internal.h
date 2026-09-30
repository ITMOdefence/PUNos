#ifndef PUNOS_LIBTIME_INTERNAL_H
#define PUNOS_LIBTIME_INTERNAL_H

/*
 * Internal conversions of libtime. Not part of the public API: they are
 * declared here only so the runtime test can check the rounding on exact
 * values, which a real clock or an interrupted sleep cannot produce on demand.
 */

#include <sys/time.h>

#include <punos/syscall/types.h>

/*
 * Clock reading -> whole milliseconds, nanoseconds truncated.
 * CORE_OVERFLOW for negative seconds or a result above u64.
 */
ErrorCode libtime_internal_floor_milliseconds(const struct timespec *value, u64 *out_ms);

/*
 * Remaining sleep time -> whole milliseconds, nanoseconds rounded up.
 * value must not be negative and must fit in u64 milliseconds; it is never
 * more than the requested sleep, which came from u64 milliseconds.
 */
u64 libtime_internal_ceil_milliseconds(const struct timespec *value);

#endif
