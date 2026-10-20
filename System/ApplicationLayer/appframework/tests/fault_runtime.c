/*
 * Fault injection into app_create(), the event source table, app_run() and
 * the SIGTERM handler installation (libmem, libeventloop and sigaction are
 * wrapped by tests/fault_fake.c).
 *
 * Check groups:
 *   1-12   every allocation of app_create() failing in turn: error returned,
 *          nothing leaked, the slot free, the SIGTERM handler untouched
 *   13-22  growth of the source table failing: the new source is not left in
 *          the event loop, earlier sources still work
 *   23-32  eventloop_wait(): interrupted once, failing for good (Failed state)
 *   33-42  sigaction() failing: the error is mapped, errno is not needed after
 */

#define CHECK_TEST_NAME "appframework-fault"
#include <check.h>

#include "fault.h"
#include "support.h"

static void old_handler(i32 signal)
{
    (void)signal;
}

static bool handler_is(sighandler_t expected)
{
    struct sigaction current = { 0 };

    return sigaction(SIGTERM, NULL, &current) == 0 && current.sa_handler == expected;
}

static void quit_handler(AppHandle app, AppEventSourceId source, EventMask mask, void *context)
{
    (void)source;
    (void)mask;
    (void)context;
    (void)app_request_quit(app);
}

static void ignore_handler(AppHandle app, AppEventSourceId source, EventMask mask, void *context)
{
    (void)app;
    (void)source;
    (void)mask;
    (void)context;
}

static void create_failures(void)
{
    struct sigaction previous = { 0 };
    AppConfig config = test_config();
    u32 failed = 0;
    u32 first_success = 0;

    previous.sa_handler = old_handler;
    EXPECT(1, sigaction(SIGTERM, &previous, NULL) == 0);

    /* Fail the 1st, 2nd, ... allocation; stop once a create succeeds. */
    for (u32 n = 1; n <= 16 && first_success == 0; ++n) {
        AppHandle app = NULL;
        ErrorCode result;
        u32 allocations_before = fault_plan.allocations;
        u32 frees_before = fault_plan.frees;

        fault_plan.fail_allocation_at = n;
        result = app_create(&config, &app);
        fault_plan.fail_allocation_at = 0;
        if (result == CORE_OK) {
            first_success = n;
            if (app_destroy(&app) != CORE_OK) {
                check_fail(2);
            }
            continue;
        }
        failed++;
        if (result != CORE_OUT_OF_MEMORY || app != NULL) {
            check_fail(3);
        }
        /* Everything that was allocated has been freed again. */
        if (fault_plan.allocations - allocations_before !=
            fault_plan.frees - frees_before) {
            check_fail(4);
        }
        if (!handler_is(old_handler)) {
            check_fail(5);
        }
    }
    EXPECT(6, failed >= 2);      /* the handle block and the event loop's memory */
    EXPECT(7, first_success > failed);
    {
        AppHandle app = test_app();

        EXPECT(8, app != NULL); /* the slot was released by every failure */
        EXPECT(9, app_destroy(&app) == CORE_OK);
    }
    EXPECT(10, handler_is(old_handler));
    EXPECT(11, fault_plan.allocations == fault_plan.frees);
    EXPECT(12, true);
}

static void table_growth_failure(void)
{
    AppHandle app = test_app();
    AppEventSourceId ids[10] = { 0 };
    AppEventSourceId extra = 0;
    ErrorCode result;

    EXPECT(13, app != NULL);
    /* The table holds 8 entries at first; the 9th add has to grow it. */
    for (u32 i = 0; i < 8; ++i) {
        if (app_event_create_timer(app, 1000, 0, ignore_handler, NULL, &ids[i]) != CORE_OK) {
            check_fail(14);
        }
    }
    fault_plan.fail_allocation_at = 1;
    result = app_event_create_timer(app, 1000, 0, ignore_handler, NULL, &extra);
    fault_plan.fail_allocation_at = 0;
    EXPECT(15, result == CORE_OUT_OF_MEMORY);
    EXPECT(16, extra == 0);
    /* The same call works afterwards, and the old entries are intact. */
    EXPECT(17, app_event_create_timer(app, 1000, 0, ignore_handler, NULL, &ids[8]) == CORE_OK);
    EXPECT(18, app_event_create_timer(app, 1000, 0, ignore_handler, NULL, &ids[9]) == CORE_OK);
    for (u32 i = 0; i < 10; ++i) {
        if (app_event_remove(app, ids[i]) != CORE_OK) {
            check_fail(19);
        }
    }
    EXPECT(20, app_event_remove(app, ids[0]) == CORE_NOT_FOUND);
    EXPECT(21, app_event_create_timer(app, 1000, 0, ignore_handler, NULL, &extra) == CORE_OK);
    EXPECT(22, app_event_remove(app, extra) == CORE_OK && app_destroy(&app) == CORE_OK);
}

/* A source whose table entry could not be made must not stay in the event
 * loop: a ready fd without a handler would wake every wait for ever. */
static void table_failure_leaves_no_source(void)
{
    AppHandle app = test_app();
    AppEventSourceId ids[8] = { 0 };
    AppEventSourceId timer = 0;
    AppEventSourceId extra = 0;
    i32 fd = test_eventfd(1); /* readable and never drained */
    u32 waits_before;

    EXPECT(43, app != NULL && fd >= 0);
    for (u32 i = 0; i < 8; ++i) {
        if (app_event_create_timer(app, 1000, 0, ignore_handler, NULL, &ids[i]) != CORE_OK) {
            check_fail(44);
        }
    }
    fault_plan.fail_allocation_at = 1;
    EXPECT(45, app_event_add_fd(app, fd, EVENT_READABLE, ignore_handler, NULL, &extra) ==
                   CORE_OUT_OF_MEMORY);
    fault_plan.fail_allocation_at = 0;
    EXPECT(46, app_event_create_timer(app, 40, 0, quit_handler, NULL, &timer) == CORE_OK);
    waits_before = fault_plan.waits;
    EXPECT(47, app_run(app) == CORE_OK);
    EXPECT(48, fault_plan.waits - waits_before < 10); /* a leaked fd would spin here */
    for (u32 i = 0; i < 8; ++i) {
        (void)app_event_remove(app, ids[i]);
    }
    (void)app_event_remove(app, timer);
    EXPECT(49, app_destroy(&app) == CORE_OK && close(fd) == 0);
}

static void wait_failures(void)
{
    AppHandle app = test_app();
    AppEventSourceId timer = 0;

    EXPECT(23, app != NULL);
    EXPECT(24, app_event_create_timer(app, 10, 0, quit_handler, NULL, &timer) == CORE_OK);
    fault_plan.wait_error = CORE_INTERRUPTED; /* a signal woke the first wait */
    EXPECT(25, app_run(app) == CORE_OK);      /* ... and the loop went on */
    EXPECT(26, fault_plan.wait_error == CORE_OK);
    EXPECT(27, app_destroy(&app) == CORE_OK);

    app = test_app();
    EXPECT(28, app != NULL);
    fault_plan.wait_error = CORE_IO_ERROR;
    EXPECT(29, app_run(app) == CORE_IO_ERROR);
    EXPECT(30, app_run(app) == CORE_INVALID_STATE); /* Failed: no second run */
    EXPECT(31, app_event_create_timer(app, 1000, 0, ignore_handler, NULL, &timer) ==
                   CORE_INVALID_STATE);
    EXPECT(32, app_destroy(&app) == CORE_OK && app == NULL);
}

static void sigaction_failures(void)
{
    AppConfig config = test_config();
    AppHandle app = NULL;
    struct sigaction previous = { 0 };

    previous.sa_handler = old_handler;
    EXPECT(33, sigaction(SIGTERM, &previous, NULL) == 0);

    fault_plan.sigaction_errno = EINVAL;
    EXPECT(34, app_create(&config, &app) == core_error_from_errno(EINVAL));
    EXPECT(35, app == NULL && fault_plan.sigaction_errno == 0);
    fault_plan.sigaction_errno = EPERM;
    EXPECT(36, app_create(&config, &app) == core_error_from_errno(EPERM));
    EXPECT(37, handler_is(old_handler));
    EXPECT(38, fault_plan.allocations == fault_plan.frees);
    /* The slot was given back, so the next create works. */
    app = test_app();
    EXPECT(39, app != NULL && !handler_is(old_handler));
    /* A failing restore reports the error but still destroys the handle. */
    fault_plan.sigaction_errno = EINVAL;
    EXPECT(40, app_destroy(&app) == core_error_from_errno(EINVAL));
    EXPECT(41, app == NULL);
    app = test_app();
    EXPECT(42, app != NULL && app_destroy(&app) == CORE_OK);
}

/* The SIGTERM handler interrupts code that is about to read errno; whatever
 * its own write() leaves behind must be put back. */
static void signal_keeps_errno(void)
{
    AppHandle app = test_app();
    i32 before = 0;

    EXPECT(50, app != NULL);
    fault_plan.wake_errno = EINVAL;
    errno = 1234;
    EXPECT(51, kill(getpid(), SIGTERM) == 0);
    before = errno;
    EXPECT(52, before == 1234);
    EXPECT(53, fault_plan.wake_errno == 0); /* the failing wake really ran */
    EXPECT(54, app_run(app) == CORE_OK);    /* the quit flag was set before the wake failed */
    EXPECT(55, app_destroy(&app) == CORE_OK);
}

int main(void)
{
    create_failures();
    table_growth_failure();
    table_failure_leaves_no_source();
    wait_failures();
    sigaction_failures();
    signal_keeps_errno();
    check_passed();
    return 0;
}
