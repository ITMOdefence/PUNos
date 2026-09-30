#include <sys/sync.h>

#include <sys/memory.h>

#include <punos/syscall/syscall.h>

/*
 * Mutex: the classic three-state futex mutex (Drepper, "Futexes Are
 * Tricky"). state is 0 (unlocked), 1 (locked, no waiters) or 2 (locked,
 * at least one waiter that must be woken on unlock).
 */
struct Mutex {
    MemoryBlock block;
    volatile u32 state;
};

/* SyncEvent: a manual-reset flag. state is 0 (not signalled) or 1 (signalled). */
struct SyncEvent {
    MemoryBlock block;
    volatile u32 state;
};

/*
 * Condition: a generation counter. Every signal bumps it; a waiter captures
 * the counter before releasing the mutex and futex_wait()s for it to
 * change, so a signal that happens right after the capture is never missed.
 */
struct Condition {
    MemoryBlock block;
    volatile u32 sequence;
};

#define NANOSECONDS_PER_MILLISECOND 1000000
/* A wake count large enough to mean "everyone", without setting the sign
 * bit of the kernel's (signed) internal wake counter. */
#define FUTEX_WAKE_ALL ((u32)0x7fffffff)

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static ErrorCode milliseconds_to_timespec(DurationMs milliseconds, struct timespec *out_value)
{
    out_value->tv_sec = (time_t)(milliseconds / TIME_MILLISECONDS_PER_SECOND);
    out_value->tv_nsec =
        (i64)(milliseconds % TIME_MILLISECONDS_PER_SECOND) * NANOSECONDS_PER_MILLISECOND;
    return CORE_OK;
}

/*
 * Waits once for *address to change away from expected, honouring a
 * deadline (ignored when forever is true). Returns CORE_OK whenever the
 * caller should simply re-check its condition: a real wake, a spurious
 * wake, EAGAIN (the value had already changed) and EINTR (a signal) all
 * take this path. Returns CORE_TIMEOUT once the deadline has passed, and
 * any other core_error_from_errno() code for a genuine futex() failure.
 */
static ErrorCode wait_once(volatile u32 *address, u32 expected, bool forever, Deadline deadline)
{
    struct timespec timeout_value;
    struct timespec *timeout_ptr = NULL;

    if (!forever) {
        DurationMs remaining;
        bool expired;

        if (time_deadline_expired(deadline, &expired) != CORE_OK) {
            return CORE_IO_ERROR;
        }
        if (expired) {
            return CORE_TIMEOUT;
        }
        if (time_deadline_remaining(deadline, &remaining) != CORE_OK) {
            return CORE_IO_ERROR;
        }
        (void)milliseconds_to_timespec(remaining, &timeout_value);
        timeout_ptr = &timeout_value;
    }

    if (futex((u32 *)address, FUTEX_WAIT, expected, timeout_ptr, NULL, 0) != 0) {
        i32 error = errno;

        if (error == ETIMEDOUT) {
            return CORE_TIMEOUT;
        }
        if (error != EAGAIN && error != EINTR) {
            return core_error_from_errno(error);
        }
    }
    return CORE_OK;
}

/* ------------------------------------------------------------------------ */
/* Mutex                                                                    */
/* ------------------------------------------------------------------------ */

ErrorCode mutex_create(Mutex *out_mutex)
{
    MemoryBlock block;
    ErrorCode result;
    Mutex mutex;

    if (out_mutex == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = memory_allocate(sizeof(struct Mutex), &block);
    if (result != CORE_OK) {
        return result;
    }
    mutex = (Mutex)block.data; /* zero-filled: state starts at 0 (unlocked) */
    mutex->block = block;
    *out_mutex = mutex;
    return CORE_OK;
}

ErrorCode mutex_destroy(Mutex *mutex)
{
    MemoryBlock block;

    if (mutex == NULL || *mutex == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    block = (*mutex)->block;
    (void)memory_free(&block);
    *mutex = NULL;
    return CORE_OK;
}

ErrorCode mutex_lock(Mutex mutex, DurationMs timeout_ms)
{
    bool forever = (timeout_ms == SYNC_WAIT_FOREVER);
    Deadline deadline = 0;
    u32 c;

    if (mutex == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (!forever && time_deadline_after(timeout_ms, &deadline) != CORE_OK) {
        return CORE_IO_ERROR;
    }

    c = 0;
    if (__atomic_compare_exchange_n(&mutex->state, &c, 1, false, __ATOMIC_ACQUIRE,
                                     __ATOMIC_RELAXED)) {
        return CORE_OK; /* uncontended fast path */
    }

    /* c holds the value observed above (1 or 2, since 0 would have won the race). */
    if (c != 2) {
        c = __atomic_exchange_n(&mutex->state, 2, __ATOMIC_RELAXED);
    }
    while (c != 0) {
        ErrorCode wait_result = wait_once(&mutex->state, 2, forever, deadline);

        if (wait_result != CORE_OK) {
            return wait_result;
        }
        c = __atomic_exchange_n(&mutex->state, 2, __ATOMIC_ACQUIRE);
    }
    return CORE_OK;
}

ErrorCode mutex_unlock(Mutex mutex)
{
    if (mutex == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (__atomic_fetch_sub(&mutex->state, 1, __ATOMIC_RELEASE) != 1) {
        /* state was 2 (contended): release it and wake exactly one waiter. */
        __atomic_store_n(&mutex->state, 0, __ATOMIC_RELEASE);
        if (futex((u32 *)&mutex->state, FUTEX_WAKE, 1, NULL, NULL, 0) < 0) {
            return core_error_from_errno(errno);
        }
    }
    return CORE_OK;
}

/* ------------------------------------------------------------------------ */
/* SyncEvent                                                                */
/* ------------------------------------------------------------------------ */

ErrorCode event_create(SyncEvent *out_event)
{
    MemoryBlock block;
    ErrorCode result;
    SyncEvent event;

    if (out_event == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = memory_allocate(sizeof(struct SyncEvent), &block);
    if (result != CORE_OK) {
        return result;
    }
    event = (SyncEvent)block.data; /* zero-filled: state starts not signalled */
    event->block = block;
    *out_event = event;
    return CORE_OK;
}

ErrorCode event_destroy(SyncEvent *event)
{
    MemoryBlock block;

    if (event == NULL || *event == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    block = (*event)->block;
    (void)memory_free(&block);
    *event = NULL;
    return CORE_OK;
}

ErrorCode event_wait(SyncEvent event, DurationMs timeout_ms)
{
    bool forever = (timeout_ms == SYNC_WAIT_FOREVER);
    Deadline deadline = 0;

    if (event == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (!forever && time_deadline_after(timeout_ms, &deadline) != CORE_OK) {
        return CORE_IO_ERROR;
    }

    while (__atomic_load_n(&event->state, __ATOMIC_ACQUIRE) == 0) {
        ErrorCode wait_result = wait_once(&event->state, 0, forever, deadline);

        if (wait_result != CORE_OK) {
            return wait_result;
        }
    }
    return CORE_OK;
}

ErrorCode event_signal(SyncEvent event)
{
    if (event == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (__atomic_exchange_n(&event->state, 1, __ATOMIC_RELEASE) == 0) {
        /* Only the 0 -> 1 transition can have waiters blocked on "still 0". */
        if (futex((u32 *)&event->state, FUTEX_WAKE, FUTEX_WAKE_ALL, NULL, NULL, 0) < 0) {
            return core_error_from_errno(errno);
        }
    }
    return CORE_OK;
}

ErrorCode event_reset(SyncEvent event)
{
    if (event == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    __atomic_store_n(&event->state, 0, __ATOMIC_RELAXED);
    return CORE_OK;
}

/* ------------------------------------------------------------------------ */
/* Condition                                                                */
/* ------------------------------------------------------------------------ */

ErrorCode condition_create(Condition *out_condition)
{
    MemoryBlock block;
    ErrorCode result;
    Condition condition;

    if (out_condition == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = memory_allocate(sizeof(struct Condition), &block);
    if (result != CORE_OK) {
        return result;
    }
    condition = (Condition)block.data; /* zero-filled: sequence starts at 0 */
    condition->block = block;
    *out_condition = condition;
    return CORE_OK;
}

ErrorCode condition_destroy(Condition *condition)
{
    MemoryBlock block;

    if (condition == NULL || *condition == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    block = (*condition)->block;
    (void)memory_free(&block);
    *condition = NULL;
    return CORE_OK;
}

ErrorCode condition_wait(Condition condition, Mutex mutex, DurationMs timeout_ms)
{
    bool forever = (timeout_ms == SYNC_WAIT_FOREVER);
    Deadline deadline = 0;
    u32 sequence;
    ErrorCode result = CORE_OK;
    ErrorCode relock_result;

    if (condition == NULL || mutex == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (!forever && time_deadline_after(timeout_ms, &deadline) != CORE_OK) {
        return CORE_IO_ERROR;
    }

    sequence = __atomic_load_n(&condition->sequence, __ATOMIC_ACQUIRE);
    result = mutex_unlock(mutex);
    if (result != CORE_OK) {
        return result; /* the mutex was not ours to release as expected */
    }

    while (__atomic_load_n(&condition->sequence, __ATOMIC_ACQUIRE) == sequence) {
        ErrorCode wait_result = wait_once(&condition->sequence, sequence, forever, deadline);

        if (wait_result != CORE_OK) {
            result = wait_result;
            break;
        }
    }

    /* Reacquire the mutex before returning no matter what happened above,
     * exactly like POSIX pthread_cond_timedwait(). */
    relock_result = mutex_lock(mutex, SYNC_WAIT_FOREVER);
    if (relock_result != CORE_OK) {
        return relock_result;
    }
    return result;
}

ErrorCode condition_signal_one(Condition condition)
{
    if (condition == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    __atomic_fetch_add(&condition->sequence, 1, __ATOMIC_RELEASE);
    if (futex((u32 *)&condition->sequence, FUTEX_WAKE, 1, NULL, NULL, 0) < 0) {
        return core_error_from_errno(errno);
    }
    return CORE_OK;
}

ErrorCode condition_signal_all(Condition condition)
{
    if (condition == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    __atomic_fetch_add(&condition->sequence, 1, __ATOMIC_RELEASE);
    if (futex((u32 *)&condition->sequence, FUTEX_WAKE, FUTEX_WAKE_ALL, NULL, NULL, 0) < 0) {
        return core_error_from_errno(errno);
    }
    return CORE_OK;
}
