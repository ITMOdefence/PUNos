/*
 * Compile-time checks of the libthread ABI against DevDoc/SystemLibs/libthread.md.
 * This file only has to compile; it produces no runnable program.
 */
#include <sys/thread.h>

#include <runtime/thread-state.h>

#include "../src/internal.h"

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)

static i32 sample_entry(void *argument)
{
    (void)argument;
    return 0;
}

_Static_assert(SAME_TYPE((ThreadId)0, i32), "ThreadId must be i32");
_Static_assert(SAME_TYPE((ThreadExitStatus)0, i32), "ThreadExitStatus must be i32");
_Static_assert(SAME_TYPE(&sample_entry, ThreadEntry), "ThreadEntry signature");
_Static_assert(sizeof(ThreadHandle) == sizeof(void *), "ThreadHandle is a pointer");
_Static_assert(THREAD_EXIT_SUCCESS == 0 && THREAD_EXIT_FAILURE == 1, "THREAD_EXIT_*");

/* The kernel writes the tid field as a 32-bit value and the futex waits on it. */
_Static_assert(sizeof(((struct ThreadHandle *)0)->tid) == 4, "tid must be 32-bit");
_Static_assert(__builtin_offsetof(struct ThreadHandle, tid) % 4 == 0, "tid alignment");
/* The TLS base must be the shared RuntimeThreadState, unmodified (contract 14). */
_Static_assert(SAME_TYPE(((struct ThreadHandle *)0)->state, struct RuntimeThreadState),
               "state type");
_Static_assert(__builtin_offsetof(struct ThreadHandle, state) % 8 == 0, "state alignment");
_Static_assert(LIBTHREAD_INTERNAL_STACK_SIZE % 4096 == 0, "stack is whole pages");
