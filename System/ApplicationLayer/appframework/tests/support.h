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
