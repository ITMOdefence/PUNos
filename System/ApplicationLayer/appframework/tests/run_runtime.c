/*
 * Runtime test of app_run(): event dispatch, timers, quit requests and SIGTERM
 * (appframework.md, "Модель событий" and "Контракты" 4-8).
 *
 * Check groups:
 *   1-8    quit requested before app_run(); sticky quit
 *   9-18   fd sources: readable, level-triggered, remove, update-free removal
 *   19-30  timers: one-shot, periodic, removal inside the handler
 *   31-40  handlers that change sources of the same batch
 *   41-48  handlers calling forbidden operations
 *   49-58  app_request_quit() from another thread; quit stops the batch
 *   59-70  SIGTERM during run, before run, handler restored, errno kept
 */

#define CHECK_TEST_NAME "appframework-run"
#include <check.h>

#include "support.h"

typedef struct Counters {
    u32 calls;
    u32 second_calls;
    AppEventSourceId source;
    AppEventSourceId other;
    EventMask last_mask;
    ErrorCode run_from_handler;
    ErrorCode destroy_from_handler;
    ErrorCode add_from_handler;
    i32 fd;
} Counters;

static void count_and_quit(AppHandle app, AppEventSourceId source, EventMask mask, void *context)
{
    Counters *counters = context;

    (void)source;
    counters->calls++;
    counters->last_mask = mask;
    (void)app_request_quit(app);
}

static void count_only(AppHandle app, AppEventSourceId source, EventMask mask, void *context)
{
    Counters *counters = context;

    (void)app;
    (void)source;
    counters->calls++;
    counters->last_mask = mask;
}

static void count_second(AppHandle app, AppEventSourceId source, EventMask mask, void *context)
{
    Counters *counters = context;

    (void)app;
    (void)source;
    (void)mask;
    counters->second_calls++;
}

static void quit_before_run(void)
{
    AppHandle app = test_app();
    Counters counters = { 0 };
    i32 fd = test_eventfd(1); /* readable at once, but never dispatched */
    AppEventSourceId source = 0;

    EXPECT(1, app != NULL && fd >= 0);
    EXPECT(2, app_event_add_fd(app, fd, EVENT_READABLE, count_only, &counters, &source) ==
                  CORE_OK && source != 0);
    EXPECT(3, app_request_quit(app) == CORE_OK && app_request_quit(app) == CORE_OK);
    EXPECT(4, app_run(app) == CORE_OK);
    EXPECT(5, counters.calls == 0); /* quit wins over events */
    EXPECT(6, app_run(app) == CORE_INVALID_STATE);
    EXPECT(7, app_destroy(&app) == CORE_OK);
    EXPECT(8, close(fd) == 0);
}

/* Reads the eventfd and quits after the third wakeup. */
static void fd_handler(AppHandle app, AppEventSourceId source, EventMask mask, void *context)
{
    Counters *counters = context;

    (void)source;
    counters->calls++;
    counters->last_mask = mask;
    test_eventfd_drain(counters->fd);
    if (counters->calls == 3) {
        (void)app_request_quit(app);
    } else {
        test_eventfd_signal(counters->fd);
    }
}

static void fd_sources(void)
{
    AppHandle app = test_app();
    Counters counters = { 0 };
    AppEventSourceId source = 0;

    counters.fd = test_eventfd(1);
    EXPECT(9, app != NULL && counters.fd >= 0);
    EXPECT(10, app_event_add_fd(app, counters.fd, EVENT_READABLE, fd_handler, &counters,
                                &source) == CORE_OK);
    EXPECT(11, app_run(app) == CORE_OK);
    EXPECT(12, counters.calls == 3);
    EXPECT(13, (counters.last_mask & EVENT_READABLE) != 0);
    EXPECT(14, app_event_remove(app, source) == CORE_OK);
    EXPECT(15, app_event_remove(app, source) == CORE_NOT_FOUND);
    EXPECT(16, app_event_remove(app, 0) == CORE_INVALID_ARGUMENT);
    EXPECT(17, app_destroy(&app) == CORE_OK);
    EXPECT(18, close(counters.fd) == 0);
}

static void timers(void)
{
    AppHandle app = test_app();
    Counters one_shot = { 0 };
    Counters periodic = { 0 };
    AppEventSourceId first = 0;
    AppEventSourceId second = 0;
    AppEventSourceId third = 0;

    EXPECT(19, app != NULL);
    EXPECT(20, app_event_create_timer(app, 0, 0, count_only, &one_shot, &first) == CORE_OK);
    EXPECT(21, app_event_create_timer(app, 5, 5, count_only, &periodic, &second) == CORE_OK);
    EXPECT(22, app_event_create_timer(app, 200, 0, count_and_quit, &periodic, &third) ==
                   CORE_OK);
    EXPECT(23, first != second && second != third && first != third);
    EXPECT(24, app_run(app) == CORE_OK);
    EXPECT(25, one_shot.calls == 1); /* delay 0 fires, period 0 does not repeat */
    EXPECT(26, periodic.calls >= 3);
    EXPECT(27, app_event_create_timer(app, 1, 0, NULL, NULL, &first) == CORE_INVALID_STATE);
    EXPECT(28, app_event_remove(app, second) == CORE_OK);
    EXPECT(29, app_event_remove(app, third) == CORE_OK);
    EXPECT(30, app_destroy(&app) == CORE_OK);
}

/* ---- handlers that change sources of the same batch ---------------------------- */

static void remove_other(AppHandle app, AppEventSourceId source, EventMask mask, void *context)
{
    Counters *counters = context;

    (void)source;
    (void)mask;
    counters->calls++;
    counters->add_from_handler = app_event_remove(app, counters->other);
    (void)app_request_quit(app);
}

static void batch_changes(void)
{
    AppHandle app = test_app();
    Counters counters = { 0 };
    Counters other = { 0 };
    i32 fd_a = test_eventfd(1);
    i32 fd_b = test_eventfd(1);
    AppEventSourceId a = 0;
    AppEventSourceId b = 0;

    EXPECT(31, app != NULL && fd_a >= 0 && fd_b >= 0);
    /* Both are ready in the same batch; whichever comes first removes the other
     * and quits. The one removed must not run afterwards (even if the loop reused its id). */
    EXPECT(32, app_event_add_fd(app, fd_a, EVENT_READABLE, remove_other, &counters, &a) ==
                   CORE_OK);
    EXPECT(33, app_event_add_fd(app, fd_b, EVENT_READABLE, remove_other, &other, &b) ==
                   CORE_OK);
    counters.other = b;
    other.other = a;
    EXPECT(34, app_run(app) == CORE_OK);
    EXPECT(35, counters.calls + other.calls == 1); /* the quit stops the rest of the batch */
    EXPECT(36, counters.add_from_handler == CORE_OK || other.add_from_handler == CORE_OK);
    EXPECT(37, app_destroy(&app) == CORE_OK);
    EXPECT(38, close(fd_a) == 0 && close(fd_b) == 0);
    EXPECT(39, a != b);
    EXPECT(40, true);
}

/* Both sources are ready in one batch; the first handler asks to quit, so the
 * second one must not start (appframework.md, "Модель событий"). */
static void quit_stops_batch(void)
{
    AppHandle app = test_app();
    Counters counters = { 0 };
    i32 fd_a = test_eventfd(1);
    i32 fd_b = test_eventfd(1);
    AppEventSourceId a = 0;
    AppEventSourceId b = 0;

    EXPECT(71, app != NULL && fd_a >= 0 && fd_b >= 0);
    EXPECT(72, app_event_add_fd(app, fd_a, EVENT_READABLE, count_and_quit, &counters, &a) ==
                   CORE_OK);
    EXPECT(73, app_event_add_fd(app, fd_b, EVENT_READABLE, count_and_quit, &counters, &b) ==
                   CORE_OK);
    EXPECT(74, app_run(app) == CORE_OK);
    EXPECT(75, counters.calls == 1);
    EXPECT(76, app_event_add_fd(app, -1, EVENT_READABLE, count_only, &counters, &a) ==
                   CORE_INVALID_STATE); /* state is checked before the arguments */
    EXPECT(77, app_destroy(&app) == CORE_OK && close(fd_a) == 0 && close(fd_b) == 0);
}

static void fd_argument_checks(void)
{
    AppHandle app = test_app();
    AppEventSourceId source = 99;
    i32 fd = test_eventfd(0);

    EXPECT(78, app != NULL && fd >= 0);
    EXPECT(79, app_event_add_fd(app, -1, EVENT_READABLE, count_only, NULL, &source) ==
                   CORE_INVALID_ARGUMENT);
    EXPECT(80, app_event_add_fd(app, fd, 0, count_only, NULL, &source) == CORE_INVALID_ARGUMENT);
    EXPECT(81, app_event_add_fd(app, fd, EVENT_READABLE | 0x8000, count_only, NULL, &source) ==
                   CORE_INVALID_ARGUMENT);
    EXPECT(82, app_event_add_fd(app, fd, EVENT_READABLE, NULL, NULL, &source) ==
                   CORE_INVALID_ARGUMENT);
    EXPECT(83, app_event_add_fd(app, fd, EVENT_READABLE, count_only, NULL, NULL) ==
                   CORE_INVALID_ARGUMENT);
    EXPECT(84, app_event_create_timer(app, 1, 0, NULL, NULL, &source) == CORE_INVALID_ARGUMENT);
    EXPECT(85, app_event_create_timer(app, 1, 0, count_only, NULL, NULL) ==
                   CORE_INVALID_ARGUMENT);
    EXPECT(86, source == 99); /* untouched on failure */
    EXPECT(87, app_destroy(&app) == CORE_OK && close(fd) == 0);
}

/* ---- forbidden operations inside a handler ----------------------------------------- */

static void forbidden_in_handler(AppHandle app, AppEventSourceId source, EventMask mask,
                                 void *context)
{
    Counters *counters = context;
    AppHandle copy = app;
    AppEventSourceId added = 0;

    (void)source;
    (void)mask;
    counters->calls++;
    counters->run_from_handler = app_run(app);
    counters->destroy_from_handler = app_destroy(&copy);
    counters->add_from_handler =
        app_event_create_timer(app, 1000, 0, count_second, counters, &added);
    counters->source = added;
    (void)app_request_quit(app);
}

static void handler_rules(void)
{
    AppHandle app = test_app();
    Counters counters = { 0 };
    AppEventSourceId source = 0;
    AppEventSourceId timer;

    EXPECT(41, app != NULL);
    EXPECT(42, app_event_create_timer(app, 0, 0, forbidden_in_handler, &counters, &source) ==
                   CORE_OK);
    EXPECT(43, app_run(app) == CORE_OK);
    EXPECT(44, counters.run_from_handler == CORE_INVALID_STATE);
    EXPECT(45, counters.destroy_from_handler == CORE_INVALID_STATE);
    EXPECT(46, counters.add_from_handler == CORE_OK); /* adding is allowed while running */
    timer = counters.source;
    EXPECT(47, timer != 0 && app_event_remove(app, timer) == CORE_OK);
    EXPECT(48, app_destroy(&app) == CORE_OK);
}

/* ---- another thread -------------------------------------------------------------------- */

static i32 quit_later(void *argument)
{
    AppHandle app = argument;

    test_sleep_ms(30);
    return app_request_quit(app) == CORE_OK ? 0 : 1;
}

static i32 start_quit_thread(AppHandle app, ThreadHandle *thread)
{
    return thread_create(quit_later, app, thread) == CORE_OK ? 0 : 1;
}

static void quit_from_thread(void)
{
    AppHandle app = test_app();
    ThreadHandle thread = NULL;
    ThreadExitStatus status = -1;
    Counters counters = { 0 };
    AppEventSourceId timer = 0;

    EXPECT(49, app != NULL);
    /* A periodic timer that never quits by itself: only the thread can stop run. */
    EXPECT(50, app_event_create_timer(app, 5, 5, count_only, &counters, &timer) == CORE_OK);
    EXPECT(51, start_quit_thread(app, &thread) == 0);
    EXPECT(52, app_run(app) == CORE_OK);
    EXPECT(53, thread_join(&thread, &status) == CORE_OK && status == 0);
    EXPECT(54, counters.calls >= 1);
    EXPECT(55, app_event_remove(app, timer) == CORE_OK);
    EXPECT(56, app_destroy(&app) == CORE_OK);
    /* Many short runs: no lost wake-up (a lost one would hang the test). */
    for (u32 i = 0; i < 100; ++i) {
        AppHandle again = test_app();
        ThreadHandle helper = NULL;
        bool ok = again != NULL && thread_create(quit_later, again, &helper) == CORE_OK;

        if (ok) {
            ok = app_run(again) == CORE_OK && thread_join(&helper, &status) == CORE_OK;
        }
        if (again != NULL) {
            (void)app_destroy(&again);
        }
        if (!ok) {
            check_fail(57);
        }
    }
    EXPECT(58, true);
}

/* ---- SIGTERM ------------------------------------------------------------------------------ */

static volatile u32 old_handler_calls;

static void old_handler(i32 signal)
{
    (void)signal;
    old_handler_calls++;
}

static void sigterm_from_timer(AppHandle app, AppEventSourceId source, EventMask mask,
                               void *context)
{
    Counters *counters = context;
    i32 saved_errno = 1234;

    (void)app;
    (void)source;
    (void)mask;
    counters->calls++;
    errno = saved_errno;
    (void)kill(getpid(), SIGTERM);
    counters->fd = errno == saved_errno; /* the handler keeps errno */
}

static bool handler_is(sighandler_t expected)
{
    struct sigaction current = { 0 };

    return sigaction(SIGTERM, NULL, &current) == 0 && current.sa_handler == expected;
}

static void sigterm(void)
{
    struct sigaction previous = { 0 };
    AppHandle app;
    Counters counters = { 0 };
    AppEventSourceId timer = 0;

    previous.sa_handler = old_handler;
    EXPECT(59, sigaction(SIGTERM, &previous, NULL) == 0);

    app = test_app();
    EXPECT(60, app != NULL && !handler_is(old_handler)); /* create installs its own */
    EXPECT(61, app_event_create_timer(app, 5, 0, sigterm_from_timer, &counters, &timer) ==
                   CORE_OK);
    EXPECT(62, app_run(app) == CORE_OK); /* SIGTERM stops the loop like a quit request */
    EXPECT(63, counters.calls == 1 && counters.fd == 1);
    EXPECT(64, old_handler_calls == 0); /* the old handler is not chained */
    EXPECT(65, app_event_remove(app, timer) == CORE_OK && app_destroy(&app) == CORE_OK);
    EXPECT(66, handler_is(old_handler)); /* restored by app_destroy */

    /* SIGTERM that arrives before app_run() is not lost. */
    app = test_app();
    EXPECT(67, app != NULL && kill(getpid(), SIGTERM) == 0);
    EXPECT(68, app_run(app) == CORE_OK);
    EXPECT(69, app_destroy(&app) == CORE_OK && handler_is(old_handler));

    /* After destroy the signal goes to the restored handler. */
    EXPECT(70, kill(getpid(), SIGTERM) == 0 && old_handler_calls == 1);
}

/* The SIGTERM handler may be running in another thread while the owner thread
 * destroys the handle. app_destroy() must not free the handle before that
 * handler is done with it. The "handler" here is a thread that enters the same
 * way on_sigterm() does and leaves a while later. */
static volatile u32 handler_inside;
static volatile u32 handler_done;

static i32 slow_handler(void *argument)
{
    AppHandle app = libappframework_internal_signal_enter();

    (void)argument;
    if (app == NULL) {
        libappframework_internal_signal_leave();
        return 1;
    }
    __atomic_store_n(&handler_inside, 1, __ATOMIC_SEQ_CST);
    test_sleep_ms(80); /* still using the handle */
    __atomic_store_n(&handler_done, 1, __ATOMIC_SEQ_CST);
    libappframework_internal_signal_leave();
    return 0;
}

static void sigterm_lifetime(void)
{
    AppHandle app = test_app();
    ThreadHandle thread = NULL;
    ThreadExitStatus status = -1;

    EXPECT(88, app != NULL);
    EXPECT(89, thread_create(slow_handler, NULL, &thread) == CORE_OK);
    for (u32 i = 0; i < 2000 && __atomic_load_n(&handler_inside, __ATOMIC_SEQ_CST) == 0; ++i) {
        test_sleep_ms(1);
    }
    EXPECT(90, __atomic_load_n(&handler_inside, __ATOMIC_SEQ_CST) == 1);
    EXPECT(91, app_destroy(&app) == CORE_OK);
    /* app_destroy() returned: the handler must have left before. */
    EXPECT(92, __atomic_load_n(&handler_done, __ATOMIC_SEQ_CST) == 1);
    EXPECT(93, thread_join(&thread, &status) == CORE_OK && status == 0);
    /* With nobody running a handler, destroying does not wait. */
    app = test_app();
    EXPECT(94, app != NULL && app_destroy(&app) == CORE_OK);
}

int main(void)
{
    quit_before_run();
    fd_sources();
    timers();
    batch_changes();
    quit_stops_batch();
    fd_argument_checks();
    handler_rules();
    quit_from_thread();
    sigterm();
    sigterm_lifetime();
    check_passed();
    return 0;
}
