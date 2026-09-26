#ifndef PUNOS_LIBTHREAD_INTERNAL_H
#define PUNOS_LIBTHREAD_INTERNAL_H

/*
 * Internal layout of a ThreadHandle. Not part of the public API: it is
 * declared here only so the runtime test can check that thread_join()
 * really releases the stack and the handle.
 */

#include <sys/memory.h>
#include <sys/thread.h>

#include <runtime/thread-state.h>

/* Size of every thread stack. Anonymous pages are only backed when touched. */
#define LIBTHREAD_INTERNAL_STACK_SIZE ((usize)256 * 1024)

struct ThreadHandle {
    /*
     * Kernel-maintained thread id: written at creation (CLONE_PARENT_SETTID,
     * CLONE_CHILD_SETTID), set to 0 with a futex wake when the thread has
     * exited and no longer uses its stack (CLONE_CHILD_CLEARTID).
     */
    volatile i32 tid;
    ThreadExitStatus result;       /* written by the thread before it exits */
    ThreadEntry entry;
    void *argument;
    MemoryBlock handle_block;      /* the block holding this structure */
    MemoryBlock stack;
    struct RuntimeThreadState state; /* the thread's TLS base (errno) */
};

#endif
