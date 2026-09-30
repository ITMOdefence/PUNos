/*
 * Compile-time checks of the libsync ABI against DevDoc/SystemLibs/libsync.md.
 * This file only has to compile; it produces no runnable program.
 */
#include <sys/sync.h>

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)

_Static_assert(SAME_TYPE(SYNC_WAIT_FOREVER, DurationMs), "SYNC_WAIT_FOREVER type");
_Static_assert(SYNC_WAIT_FOREVER == (DurationMs)0xffffffffffffffffULL,
               "SYNC_WAIT_FOREVER value");

/* Every handle is an opaque pointer, released exactly once by its *_destroy(). */
_Static_assert(sizeof(Mutex) == sizeof(void *), "Mutex must be a pointer");
_Static_assert(sizeof(SyncEvent) == sizeof(void *), "SyncEvent must be a pointer");
_Static_assert(sizeof(Condition) == sizeof(void *), "Condition must be a pointer");
