/*
 * Runtime test of the libsync operations against DevDoc/SystemLibs/libsync.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 * Threads come from libthread: it is not a dependency of libsync itself
 * (sync.c calls only futex()), but the test needs real contention to catch
 * futex bugs that a single thread cannot reach. Run this test many times in
 * a row (host x30, QEMU x5, per System/SystemLibs/README.md): races do not
 * show up every time.
 */
#define CHECK_TEST_NAME "libsync"
#include "../../testing/check.h"

#include <sys/sync.h>
#include <sys/thread.h>

/* ------------------------------------------------------------------------ */
/* Mutex: basics                                                            */
/* ------------------------------------------------------------------------ */

static void test_mutex_basic(void)
{
    Mutex mutex = NULL;
    Mutex null_mutex = NULL;

    EXPECT(1, mutex_create(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(2, mutex_create(&mutex) == CORE_OK && mutex != NULL);
    EXPECT(3, mutex_lock(mutex, SYNC_WAIT_FOREVER) == CORE_OK);
    EXPECT(4, mutex_unlock(mutex) == CORE_OK);
    EXPECT(5, mutex_lock(NULL, SYNC_WAIT_FOREVER) == CORE_INVALID_ARGUMENT);
    EXPECT(6, mutex_unlock(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(7, mutex_destroy(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(8, mutex_destroy(&null_mutex) == CORE_INVALID_ARGUMENT);
    EXPECT(9, mutex_destroy(&mutex) == CORE_OK);
    EXPECT(10, mutex == NULL);
}

/* ------------------------------------------------------------------------ */
/* Mutex: real contention                                                   */
/* ------------------------------------------------------------------------ */

#define INCREMENTS_PER_THREAD 20000
#define COUNTER_THREADS 4

struct CounterArgs {
    Mutex mutex;
    i32 *counter;
};

static i32 increment_thread(void *argument)
{
    struct CounterArgs *args = argument;
    i32 i;

    for (i = 0; i < INCREMENTS_PER_THREAD; ++i) {
        if (mutex_lock(args->mutex, SYNC_WAIT_FOREVER) != CORE_OK) {
            return 1;
        }
        *args->counter = *args->counter + 1;
        if (mutex_unlock(args->mutex) != CORE_OK) {
            return 1;
        }
    }
    return 0;
}

static void test_mutex_contention(void)
{
    Mutex mutex = NULL;
    i32 counter = 0;
    struct CounterArgs args;
    ThreadHandle threads[COUNTER_THREADS];
    i32 i;

    EXPECT(20, mutex_create(&mutex) == CORE_OK);
    args.mutex = mutex;
    args.counter = &counter;

    for (i = 0; i < COUNTER_THREADS; ++i) {
        EXPECT(21, thread_create(increment_thread, &args, &threads[i]) == CORE_OK);
    }
    for (i = 0; i < COUNTER_THREADS; ++i) {
        ThreadExitStatus status = -1;

        EXPECT(22, thread_join(&threads[i], &status) == CORE_OK && status == 0);
    }
    /* If the mutex ever let two threads increment at once, this undercounts. */
    EXPECT(23, counter == COUNTER_THREADS * INCREMENTS_PER_THREAD);
    EXPECT(24, mutex_destroy(&mutex) == CORE_OK);
}

/* ------------------------------------------------------------------------ */
/* Mutex: timeout                                                           */
/* ------------------------------------------------------------------------ */

struct HolderArgs {
    Mutex mutex;
    SyncEvent locked_signal;
    SyncEvent release_signal;
};

static i32 holder_thread(void *argument)
{
    struct HolderArgs *args = argument;

    if (mutex_lock(args->mutex, SYNC_WAIT_FOREVER) != CORE_OK) {
        return 1;
    }
    if (event_signal(args->locked_signal) != CORE_OK) {
        return 1;
    }
    if (event_wait(args->release_signal, SYNC_WAIT_FOREVER) != CORE_OK) {
        return 1;
    }
    if (mutex_unlock(args->mutex) != CORE_OK) {
        return 1;
    }
    return 0;
}

static void test_mutex_timeout(void)
{
    Mutex mutex = NULL;
    SyncEvent locked_signal = NULL;
    SyncEvent release_signal = NULL;
    struct HolderArgs args;
    ThreadHandle holder = NULL;
    ThreadExitStatus status = -1;

    EXPECT(30, mutex_create(&mutex) == CORE_OK);
    EXPECT(31, event_create(&locked_signal) == CORE_OK);
    EXPECT(32, event_create(&release_signal) == CORE_OK);
    args.mutex = mutex;
    args.locked_signal = locked_signal;
    args.release_signal = release_signal;

    EXPECT(33, thread_create(holder_thread, &args, &holder) == CORE_OK);
    EXPECT(34, event_wait(locked_signal, SYNC_WAIT_FOREVER) == CORE_OK);

    /* The holder thread owns the mutex now: a short timed lock must time out. */
    EXPECT(35, mutex_lock(mutex, 50) == CORE_TIMEOUT);

    EXPECT(36, event_signal(release_signal) == CORE_OK);
    EXPECT(37, thread_join(&holder, &status) == CORE_OK && status == 0);

    /* The mutex is free now: locking it (even with a timeout set) must succeed. */
    EXPECT(38, mutex_lock(mutex, 1000) == CORE_OK);
    EXPECT(39, mutex_unlock(mutex) == CORE_OK);

    EXPECT(40, event_destroy(&locked_signal) == CORE_OK);
    EXPECT(41, event_destroy(&release_signal) == CORE_OK);
    EXPECT(42, mutex_destroy(&mutex) == CORE_OK);
}

/* ------------------------------------------------------------------------ */
/* SyncEvent: basics                                                        */
/* ------------------------------------------------------------------------ */

static void test_event_basic(void)
{
    SyncEvent event = NULL;
    SyncEvent null_event = NULL;

    EXPECT(50, event_create(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(51, event_create(&event) == CORE_OK);
    EXPECT(52, event_wait(event, 50) == CORE_TIMEOUT); /* not signalled yet */
    EXPECT(53, event_signal(event) == CORE_OK);
    /* Already signalled: returns immediately even with a 0 timeout. */
    EXPECT(54, event_wait(event, 0) == CORE_OK);
    EXPECT(55, event_reset(event) == CORE_OK);
    EXPECT(56, event_wait(event, 50) == CORE_TIMEOUT);
    EXPECT(57, event_wait(NULL, 0) == CORE_INVALID_ARGUMENT);
    EXPECT(58, event_signal(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(59, event_reset(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(60, event_destroy(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(61, event_destroy(&null_event) == CORE_INVALID_ARGUMENT);
    EXPECT(62, event_destroy(&event) == CORE_OK);
}

/* ------------------------------------------------------------------------ */
/* SyncEvent: several waiters woken by one signal                          */
/* ------------------------------------------------------------------------ */

#define EVENT_WAITERS 4

struct WaitArgs {
    SyncEvent event;
    volatile i32 *woken_count;
};

static i32 waiter_thread(void *argument)
{
    struct WaitArgs *args = argument;

    if (event_wait(args->event, 5000) != CORE_OK) {
        return 1;
    }
    __atomic_fetch_add(args->woken_count, 1, __ATOMIC_RELAXED);
    return 0;
}

static void test_event_multi_waiter(void)
{
    SyncEvent event = NULL;
    volatile i32 woken_count = 0;
    struct WaitArgs args;
    ThreadHandle threads[EVENT_WAITERS];
    i32 i;

    EXPECT(65, event_create(&event) == CORE_OK);
    args.event = event;
    args.woken_count = &woken_count;

    for (i = 0; i < EVENT_WAITERS; ++i) {
        EXPECT(66, thread_create(waiter_thread, &args, &threads[i]) == CORE_OK);
    }
    /* A manual-reset event stays signalled, so a late waiter still sees it
     * (contract 7): this sleep only biases the test toward exercising the
     * real wake path instead of the "already signalled" fast path. */
    {
        DurationMs remaining;

        (void)time_sleep(50, &remaining);
    }
    EXPECT(67, event_signal(event) == CORE_OK);
    for (i = 0; i < EVENT_WAITERS; ++i) {
        ThreadExitStatus status = -1;

        EXPECT(68, thread_join(&threads[i], &status) == CORE_OK && status == 0);
    }
    EXPECT(69, woken_count == EVENT_WAITERS);
    EXPECT(70, event_destroy(&event) == CORE_OK);
}

/* ------------------------------------------------------------------------ */
/* Condition: basics, one signal wakes one waiter                          */
/* ------------------------------------------------------------------------ */

struct CondArgs {
    Mutex mutex;
    Condition condition;
    volatile bool *ready;
};

static i32 consumer_thread(void *argument)
{
    struct CondArgs *args = argument;

    if (mutex_lock(args->mutex, SYNC_WAIT_FOREVER) != CORE_OK) {
        return 1;
    }
    while (!*args->ready) {
        if (condition_wait(args->condition, args->mutex, SYNC_WAIT_FOREVER) != CORE_OK) {
            (void)mutex_unlock(args->mutex);
            return 1;
        }
    }
    if (mutex_unlock(args->mutex) != CORE_OK) {
        return 1;
    }
    return 0;
}

static void test_condition_basic(void)
{
    Mutex mutex = NULL;
    Condition condition = NULL;
    Condition null_condition = NULL;
    volatile bool ready = false;
    struct CondArgs args;
    ThreadHandle consumer = NULL;
    ThreadExitStatus status = -1;

    EXPECT(75, condition_create(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(76, condition_create(&condition) == CORE_OK);
    EXPECT(77, mutex_create(&mutex) == CORE_OK);

    args.mutex = mutex;
    args.condition = condition;
    args.ready = &ready;

    EXPECT(78, thread_create(consumer_thread, &args, &consumer) == CORE_OK);

    /* The shared "ready" flag is only ever read/written under the mutex, so
     * this is race-free regardless of how the sleep and the consumer
     * thread's scheduling interleave: either the consumer sees ready == true
     * right away and never waits, or it is already waiting and gets signalled. */
    {
        DurationMs remaining;

        (void)time_sleep(50, &remaining);
    }

    EXPECT(79, mutex_lock(mutex, SYNC_WAIT_FOREVER) == CORE_OK);
    ready = true;
    EXPECT(80, condition_signal_one(condition) == CORE_OK);
    EXPECT(81, mutex_unlock(mutex) == CORE_OK);

    EXPECT(82, thread_join(&consumer, &status) == CORE_OK && status == 0);

    EXPECT(83, condition_wait(NULL, mutex, 0) == CORE_INVALID_ARGUMENT);
    EXPECT(84, condition_wait(condition, NULL, 0) == CORE_INVALID_ARGUMENT);
    EXPECT(85, condition_signal_one(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(86, condition_signal_all(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(87, condition_destroy(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(88, condition_destroy(&null_condition) == CORE_INVALID_ARGUMENT);
    EXPECT(89, condition_destroy(&condition) == CORE_OK);
    EXPECT(90, mutex_destroy(&mutex) == CORE_OK);
}

/* ------------------------------------------------------------------------ */
/* Condition: signal_all wakes every waiter                                 */
/* ------------------------------------------------------------------------ */

#define BROADCAST_WAITERS 4

struct CondBroadcastArgs {
    Mutex mutex;
    Condition condition;
    volatile bool *ready;
    volatile i32 *woken_count;
};

static i32 broadcast_waiter_thread(void *argument)
{
    struct CondBroadcastArgs *args = argument;

    if (mutex_lock(args->mutex, SYNC_WAIT_FOREVER) != CORE_OK) {
        return 1;
    }
    while (!*args->ready) {
        if (condition_wait(args->condition, args->mutex, SYNC_WAIT_FOREVER) != CORE_OK) {
            (void)mutex_unlock(args->mutex);
            return 1;
        }
    }
    if (mutex_unlock(args->mutex) != CORE_OK) {
        return 1;
    }
    __atomic_fetch_add(args->woken_count, 1, __ATOMIC_RELAXED);
    return 0;
}

static void test_condition_broadcast(void)
{
    Mutex mutex = NULL;
    Condition condition = NULL;
    volatile bool ready = false;
    volatile i32 woken_count = 0;
    struct CondBroadcastArgs args;
    ThreadHandle threads[BROADCAST_WAITERS];
    i32 i;

    EXPECT(95, mutex_create(&mutex) == CORE_OK);
    EXPECT(96, condition_create(&condition) == CORE_OK);
    args.mutex = mutex;
    args.condition = condition;
    args.ready = &ready;
    args.woken_count = &woken_count;

    for (i = 0; i < BROADCAST_WAITERS; ++i) {
        EXPECT(97, thread_create(broadcast_waiter_thread, &args, &threads[i]) == CORE_OK);
    }
    {
        DurationMs remaining;

        (void)time_sleep(50, &remaining);
    }
    EXPECT(98, mutex_lock(mutex, SYNC_WAIT_FOREVER) == CORE_OK);
    ready = true;
    EXPECT(99, condition_signal_all(condition) == CORE_OK);
    EXPECT(100, mutex_unlock(mutex) == CORE_OK);

    for (i = 0; i < BROADCAST_WAITERS; ++i) {
        ThreadExitStatus status = -1;

        EXPECT(101, thread_join(&threads[i], &status) == CORE_OK && status == 0);
    }
    EXPECT(102, woken_count == BROADCAST_WAITERS);
    EXPECT(103, mutex_destroy(&mutex) == CORE_OK);
    EXPECT(104, condition_destroy(&condition) == CORE_OK);
}

/* ------------------------------------------------------------------------ */
/* Condition: timeout still relocks the mutex                              */
/* ------------------------------------------------------------------------ */

static void test_condition_timeout(void)
{
    Mutex mutex = NULL;
    Condition condition = NULL;

    EXPECT(110, mutex_create(&mutex) == CORE_OK);
    EXPECT(111, condition_create(&condition) == CORE_OK);

    EXPECT(112, mutex_lock(mutex, SYNC_WAIT_FOREVER) == CORE_OK);
    EXPECT(113, condition_wait(condition, mutex, 50) == CORE_TIMEOUT);
    /* Even after a timeout, condition_wait() must return with the mutex
     * locked again (specification contract 8, POSIX pthread_cond_timedwait
     * semantics): an extra, unbalanced unlock here would otherwise succeed
     * on an already-unlocked mutex and corrupt its state. */
    EXPECT(114, mutex_unlock(mutex) == CORE_OK);
    EXPECT(115, mutex_lock(mutex, 100) == CORE_OK);
    EXPECT(116, mutex_unlock(mutex) == CORE_OK);

    EXPECT(117, mutex_destroy(&mutex) == CORE_OK);
    EXPECT(118, condition_destroy(&condition) == CORE_OK);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    test_mutex_basic();
    test_mutex_contention();
    test_mutex_timeout();
    test_event_basic();
    test_event_multi_waiter();
    test_condition_basic();
    test_condition_broadcast();
    test_condition_timeout();

    check_passed();
    return 0;
}
