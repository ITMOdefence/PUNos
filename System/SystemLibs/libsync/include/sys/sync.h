#ifndef PUNOS_SYS_SYNC_H
#define PUNOS_SYS_SYNC_H

/*
 * libsync (sys/sync) - thread synchronization: mutex, manual-reset event and
 * condition variable, all built on the futex() Syscall Wrapper.
 *
 * Specification: DevDoc/SystemLibs/libsync.md
 *
 * Every timeout is a DurationMs from <sys/time.h>; SYNC_WAIT_FOREVER means
 * no timeout. All operations return ErrorCode; see <sys/core.h> for the
 * conventions.
 */

#include <sys/core.h>
#include <sys/time.h>

/* Opaque handles, released exactly once with their *_destroy(). */
typedef struct Mutex *Mutex;
typedef struct SyncEvent *SyncEvent;
typedef struct Condition *Condition;

#define SYNC_WAIT_FOREVER ((DurationMs)0xffffffffffffffffULL)

/*
 * Creates an unlocked mutex.
 *
 * Errors: CORE_INVALID_ARGUMENT if out_mutex is NULL; errors of libmem.
 */
ErrorCode mutex_create(Mutex *out_mutex);

/*
 * Destroys *mutex and sets it to NULL. The caller must ensure the mutex is
 * unlocked and nobody is waiting on it (specification contract 6); this is
 * not checked at runtime.
 *
 * Errors: CORE_INVALID_ARGUMENT if mutex or *mutex is NULL; errors of libmem.
 */
ErrorCode mutex_destroy(Mutex *mutex);

/*
 * Locks mutex, waiting at most timeout_ms milliseconds (SYNC_WAIT_FOREVER
 * for no limit). The mutex is not recursive: the calling thread must not
 * already hold it (specification contract 4; not detected at runtime, the
 * same way an ordinary futex mutex works).
 *
 * Errors: CORE_INVALID_ARGUMENT if mutex is NULL; CORE_TIMEOUT if
 * timeout_ms expires first; core_error_from_errno() for an unexpected
 * futex() failure.
 */
ErrorCode mutex_lock(Mutex mutex, DurationMs timeout_ms);

/*
 * Unlocks mutex. Must be called by the thread that locked it
 * (specification contract 5; not checked at runtime).
 *
 * Errors: CORE_INVALID_ARGUMENT if mutex is NULL; core_error_from_errno()
 * for an unexpected futex() failure.
 */
ErrorCode mutex_unlock(Mutex mutex);

/*
 * Creates a manual-reset event, initially not signalled.
 *
 * Errors: CORE_INVALID_ARGUMENT if out_event is NULL; errors of libmem.
 */
ErrorCode event_create(SyncEvent *out_event);

/*
 * Destroys *event and sets it to NULL.
 *
 * Errors: CORE_INVALID_ARGUMENT if event or *event is NULL; errors of
 * libmem.
 */
ErrorCode event_destroy(SyncEvent *event);

/*
 * Waits until event is signalled, at most timeout_ms milliseconds
 * (SYNC_WAIT_FOREVER for no limit). Returns immediately if the event is
 * already signalled.
 *
 * Errors: CORE_INVALID_ARGUMENT if event is NULL; CORE_TIMEOUT if
 * timeout_ms expires first; core_error_from_errno() for an unexpected
 * futex() failure.
 */
ErrorCode event_wait(SyncEvent event, DurationMs timeout_ms);

/*
 * Sets event to the signalled state and wakes every thread waiting on it.
 * The event stays signalled until event_reset() (specification contract 7).
 *
 * Errors: CORE_INVALID_ARGUMENT if event is NULL; core_error_from_errno()
 * for an unexpected futex() failure.
 */
ErrorCode event_signal(SyncEvent event);

/*
 * Sets event back to the not-signalled state.
 *
 * Errors: CORE_INVALID_ARGUMENT if event is NULL.
 */
ErrorCode event_reset(SyncEvent event);

/*
 * Creates a condition variable.
 *
 * Errors: CORE_INVALID_ARGUMENT if out_condition is NULL; errors of libmem.
 */
ErrorCode condition_create(Condition *out_condition);

/*
 * Destroys *condition and sets it to NULL.
 *
 * Errors: CORE_INVALID_ARGUMENT if condition or *condition is NULL; errors
 * of libmem.
 */
ErrorCode condition_destroy(Condition *condition);

/*
 * mutex must already be locked by the calling thread (specification
 * contract 8). Atomically unlocks it, waits for condition_signal_one(),
 * condition_signal_all() or timeout_ms to elapse, then locks mutex again
 * before returning - even if the wait timed out or failed, exactly like
 * POSIX pthread_cond_timedwait().
 *
 * Errors: CORE_INVALID_ARGUMENT if condition or mutex is NULL; CORE_TIMEOUT
 * if timeout_ms expires first; core_error_from_errno() for an unexpected
 * futex() failure.
 */
ErrorCode condition_wait(Condition condition, Mutex mutex, DurationMs timeout_ms);

/*
 * Wakes one thread waiting on condition, if any.
 *
 * Errors: CORE_INVALID_ARGUMENT if condition is NULL; core_error_from_errno()
 * for an unexpected futex() failure.
 */
ErrorCode condition_signal_one(Condition condition);

/*
 * Wakes every thread waiting on condition.
 *
 * Errors: CORE_INVALID_ARGUMENT if condition is NULL; core_error_from_errno()
 * for an unexpected futex() failure.
 */
ErrorCode condition_signal_all(Condition condition);

#endif
