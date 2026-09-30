/*
 * Compile-time checks of the libtime ABI against DevDoc/SystemLibs/libtime.md.
 * This file only has to compile; it produces no runnable program.
 */
#include <sys/time.h>

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)

_Static_assert(SAME_TYPE((DurationMs)0, u64), "DurationMs must be u64");
_Static_assert(SAME_TYPE((MonotonicTime)0, u64), "MonotonicTime must be u64");
_Static_assert(SAME_TYPE((WallClockTime)0, u64), "WallClockTime must be u64");
_Static_assert(SAME_TYPE((Deadline)0, MonotonicTime), "Deadline must be MonotonicTime");
_Static_assert(TIME_MILLISECONDS_PER_SECOND == 1000, "TIME_MILLISECONDS_PER_SECOND");
_Static_assert(SAME_TYPE(TIME_MILLISECONDS_PER_SECOND, u64), "TIME_MILLISECONDS_PER_SECOND type");
