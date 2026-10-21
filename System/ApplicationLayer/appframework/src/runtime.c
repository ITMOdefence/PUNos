#include "internal.h"

/*
 * The one active AppHandle of the process. The SIGTERM handler has no context
 * argument, so it finds the handle here. It is set after the handle is fully
 * built and cleared after the previous SIGTERM handler is back, so the signal
 * handler never sees a half-built handle.
 */
static struct AppHandle *volatile active_app;

/*
 * SIGTERM handlers in progress. A handler registers here before it reads
 * active_app and unregisters when it is done with the handle, so app_destroy()
 * can wait until no handler (in any thread) can still touch the handle it is
 * about to free. All accesses are sequentially consistent: the destroying
 * thread stores NULL and then reads the count, a handler increments and then
 * reads the pointer; with weaker ordering both could miss each other.
 */
static volatile u32 handlers_running;

static bool flag_is_set(volatile u32 *flag)
{
    return __atomic_load_n(flag, __ATOMIC_ACQUIRE) != 0;
}

/* Sets the quit flag and wakes the loop. Async-signal-safe: one atomic store
 * and one write() to an eventfd; errno is the caller's to preserve. */
static ErrorCode request_quit(AppHandle app)
{
    __atomic_store_n(&app->quit_requested, 1, __ATOMIC_RELEASE);
    return eventloop_wake(app->loop);
}

/* The handler's view of the handle: returns it (or NULL) and keeps it alive
 * until libappframework_internal_signal_leave(). Async-signal-safe. */
AppHandle libappframework_internal_signal_enter(void)
{
    __atomic_fetch_add(&handlers_running, 1, __ATOMIC_SEQ_CST);
    return __atomic_load_n(&active_app, __ATOMIC_SEQ_CST);
}

void libappframework_internal_signal_leave(void)
{
    __atomic_fetch_sub(&handlers_running, 1, __ATOMIC_SEQ_CST);
}

static void on_sigterm(i32 signal)
{
    i32 saved_errno = errno;
    AppHandle app = libappframework_internal_signal_enter();

    (void)signal;
    if (app != NULL) {
        __atomic_store_n(&app->sigterm_received, 1, __ATOMIC_RELEASE);
        (void)request_quit(app);
    }
    libappframework_internal_signal_leave();
    errno = saved_errno;
}

ErrorCode libappframework_internal_runtime_attach(AppHandle app)
{
    struct AppHandle *expected = NULL;
    struct sigaction action = { 0 };

    if (!__atomic_compare_exchange_n(&active_app, &expected, app, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return CORE_INVALID_STATE; /* a handle already exists */
    }
    action.sa_handler = on_sigterm;
    if (sigaction(SIGTERM, &action, &app->previous_sigterm) != 0) {
        ErrorCode error = core_error_from_errno(errno); /* errno first */

        __atomic_store_n(&active_app, NULL, __ATOMIC_RELEASE);
        return error;
    }
    app->sigterm_installed = true;
    return CORE_OK;
}

ErrorCode libappframework_internal_runtime_detach(AppHandle app)
{
    ErrorCode result = CORE_OK;

    if (app->sigterm_installed) {
        if (sigaction(SIGTERM, &app->previous_sigterm, NULL) != 0) {
            result = core_error_from_errno(errno);
        }
        app->sigterm_installed = false;
    }
    __atomic_store_n(&active_app, NULL, __ATOMIC_SEQ_CST);
    /* A handler that started before the store may still be using the handle
     * (it runs in another thread, or was interrupted there). It only stores a
     * flag and writes to an eventfd, so this wait is short. */
    while (__atomic_load_n(&handlers_running, __ATOMIC_SEQ_CST) != 0) {
        DurationMs remaining = 0;

        (void)time_sleep(1, &remaining);
    }
    return result;
}

bool libappframework_internal_quit_requested(AppHandle app)
{
    return flag_is_set(&app->quit_requested);
}

ErrorCode app_request_quit(AppHandle app)
{
    if (app == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    /* Any thread, and a signal handler: no thread or state check. */
    return request_quit(app);
}

ErrorCode app_run(AppHandle app)
{
    ErrorCode result = libappframework_internal_enter(app, APP_OPERATION_RUN);

    if (result != CORE_OK) {
        return result;
    }
    app->state = APP_LIFECYCLE_RUNNING;

    while (!libappframework_internal_quit_requested(app)) {
        Event events[APP_EVENT_BATCH_SIZE];
        usize count = 0;

        result = eventloop_wait(app->loop, events, APP_EVENT_BATCH_SIZE,
                                EVENTLOOP_WAIT_FOREVER, &count);
        if (result == CORE_INTERRUPTED) {
            continue; /* a signal woke the wait; the quit flag decides */
        }
        if (result != CORE_OK) {
            app->state = APP_LIFECYCLE_FAILED;
            (void)libappframework_internal_log(app, APP_LOG_LEVEL_ERROR,
                                               "event loop failed");
            return result;
        }
        libappframework_internal_dispatch(app, events, count);
    }

    app->state = APP_LIFECYCLE_STOPPING;
    if (flag_is_set(&app->sigterm_received)) {
        (void)libappframework_internal_log(app, APP_LOG_LEVEL_INFO, "SIGTERM received");
    }
    return CORE_OK;
}
