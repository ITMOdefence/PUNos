#ifndef PUNOS_APPFRAMEWORK_TESTS_SUPPORT_H
#define PUNOS_APPFRAMEWORK_TESTS_SUPPORT_H

/* Helpers shared by the runtime tests of appframework. */

#include <punos/syscall/constants.h>
#include <punos/syscall/syscall.h>
#include <sys/thread.h>
#include <sys/time.h>

#include "internal.h"

static inline AppConfig test_config(void)
{
    AppConfig config = { .application_id = "test-app", .log_component = "test" };

    return config;
}

/* A creation that must succeed; the checks of the tests start from a live app. */
static inline AppHandle test_app(void)
{
    AppConfig config = test_config();
    AppHandle app = NULL;

    return app_create(&config, &app) == CORE_OK ? app : NULL;
}

static inline i32 test_eventfd(u32 initial)
{
    return eventfd(initial, EFD_NONBLOCK);
}

static inline void test_eventfd_signal(i32 fd)
{
    u64 one = 1;

    (void)write(fd, &one, sizeof(one));
}

static inline void test_eventfd_drain(i32 fd)
{
    u64 value;

    (void)read(fd, &value, sizeof(value));
}

static inline void test_sleep_ms(DurationMs duration)
{
    DurationMs remaining = 0;

    /* A signal may cut the sleep short; keep sleeping for what is left. */
    while (time_sleep(duration, &remaining) == CORE_INTERRUPTED && remaining != 0) {
        duration = remaining;
    }
}

/* Runs the event loop of app by hand for about duration milliseconds: waits,
 * dispatches, and stops after one wait that finds nothing. The state of the
 * handle does not change, so the tests can alternate it with app_log() and
 * the fake peers' calls. */
static inline void test_pump(AppHandle app, DurationMs duration)
{
    for (u32 round = 0; round < 10000; ++round) {
        Event events[APP_EVENT_BATCH_SIZE];
        usize count = 0;

        if (eventloop_wait(app->loop, events, APP_EVENT_BATCH_SIZE, duration, &count) !=
                CORE_OK ||
            count == 0) {
            return;
        }
        libappframework_internal_dispatch(app, events, count);
    }
}

/* Runs entry on a new thread, returns after it finished; false if it failed to
 * start or reported a non-zero status. */
static inline bool test_run_on_thread(ThreadEntry entry, void *argument)
{
    ThreadHandle thread = NULL;
    ThreadExitStatus status = -1;

    if (thread_create(entry, argument, &thread) != CORE_OK) {
        return false;
    }
    return thread_join(&thread, &status) == CORE_OK && status == 0;
}

#endif
