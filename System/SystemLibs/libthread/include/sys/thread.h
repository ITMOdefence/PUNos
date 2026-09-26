#ifndef PUNOS_SYS_THREAD_H
#define PUNOS_SYS_THREAD_H

/*
 * libthread (sys/thread) - native threads: create, join, get the result.
 *
 * Specification: DevDoc/SystemLibs/libthread.md
 *
 * A thread runs its ThreadEntry once; the value it returns is the thread's
 * result, delivered by thread_join(). There is no way to stop a thread from
 * outside: it ends when its entry returns.
 *
 * Every thread has its own stack and its own RuntimeThreadState (so its own
 * errno), both allocated through libmem and owned by the ThreadHandle until
 * thread_join(). Stacks have a fixed size of 256 KiB and no guard page: a
 * thread that overflows its stack corrupts other memory instead of faulting.
 *
 * The only syscall wrappers used are clone(), gettid() and futex().
 * All operations return ErrorCode; see <sys/core.h> for the conventions.
 */

#include <sys/core.h>

typedef i32 ThreadId;
typedef i32 ThreadExitStatus;

/* Function run by a new thread. Its return value becomes the thread's result. */
typedef i32 (*ThreadEntry)(void *argument);

/*
 * Opaque handle of a created thread. It must be passed to thread_join()
 * exactly once, which releases it and sets it to NULL.
 */
typedef struct ThreadHandle *ThreadHandle;

#define THREAD_EXIT_SUCCESS 0
#define THREAD_EXIT_FAILURE 1

/*
 * Stores the id of the calling thread in *out_thread_id. In the main thread it
 * equals the process id.
 *
 * Errors: CORE_INVALID_ARGUMENT if out_thread_id is NULL.
 */
ErrorCode thread_current_id(ThreadId *out_thread_id);

/*
 * Creates a thread that runs entry(argument) and stores its handle in
 * *out_thread. argument is not owned by the library and must stay valid until
 * the thread has finished.
 *
 * Errors: CORE_INVALID_ARGUMENT if entry or out_thread is NULL; errors of
 * libmem; core_error_from_errno() of clone().
 */
ErrorCode thread_create(ThreadEntry entry, void *argument, ThreadHandle *out_thread);

/*
 * Waits until the thread has finished, stores the value its entry returned in
 * *out_status, releases the thread's stack and state and sets *thread to NULL.
 * Signals do not end the wait early.
 *
 * Errors: CORE_INVALID_ARGUMENT if an argument or *thread is NULL;
 * CORE_INVALID_STATE if called from the thread being joined (it would wait
 * for itself forever); core_error_from_errno() of futex() for an unexpected
 * failure, in which case the handle is kept.
 */
ErrorCode thread_join(ThreadHandle *thread, ThreadExitStatus *out_status);

#endif
