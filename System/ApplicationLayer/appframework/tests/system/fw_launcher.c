/*
 * An application built on appframework that drives the real appserv: it lists
 * the catalogue, asks for errors it must get, launches fw_child, waits for it
 * to run, stops it with SIGTERM and checks how it ended. In "wait" mode it only
 * waits for SIGTERM.
 *
 * argv[1]  endpoint of appserv
 * argv[2]  endpoint of logserv
 * argv[3]  mode: "full" or "wait"
 * argv[4]  marker file of fw_child (full mode)
 *
 * The exit status is 0 on success, otherwise the number of the failed step.
 */

#include <appframework/app.h>
#include <punos/syscall/constants.h>
#include <punos/syscall/syscall.h>

#include "internal.h"

#define POLL_PERIOD_MS 50
#define MAX_POLLS 400

typedef enum Step {
    STEP_LIST = 1,
    STEP_INFO,
    STEP_INFO_UNKNOWN,
    STEP_LAUNCH_UNKNOWN,
    STEP_STATUS_UNKNOWN,
    STEP_LAUNCH,
    STEP_WAIT_READY,
    STEP_WAIT_RUNNING,
    STEP_STOP,
    STEP_WAIT_EXITED,
    STEP_STOP_EXITED,
    STEP_DONE
} Step;

typedef struct Launcher {
    AppHandle app;
    const char *marker;
    Step step;
    AppInstanceId instance;
    u32 polls;
    bool poll_in_flight; /* one status request at a time */
    AppEventSourceId timer;
    i32 failure;
} Launcher;

static bool equal(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static void fail(Launcher *launcher, i32 code)
{
    if (launcher->failure == 0) {
        launcher->failure = code;
        (void)app_log(launcher->app, APP_LOG_LEVEL_ERROR, "launcher failed");
    }
    (void)app_request_quit(launcher->app);
}

static void advance(Launcher *launcher);

static void on_reply(AppHandle app, AppRequestId request, ErrorCode error,
                     const AppAppsResult *result, void *context)
{
    Launcher *launcher = context;
    bool ok = false;

    (void)app;
    (void)request;
    launcher->poll_in_flight = false;
    switch (launcher->step) {
    case STEP_LIST:
        ok = error == CORE_OK && result->list.count == 1 &&
             equal(result->list.items[0].application_id, "fwchild") &&
             equal(result->list.items[0].display_name, "Child");
        break;
    case STEP_INFO:
        ok = error == CORE_OK && equal(result->info.application_id, "fwchild") &&
             equal(result->info.display_name, "Child");
        break;
    case STEP_INFO_UNKNOWN:
    case STEP_LAUNCH_UNKNOWN:
    case STEP_STATUS_UNKNOWN:
        ok = error == CORE_NOT_FOUND && result == NULL;
        break;
    case STEP_LAUNCH:
        ok = error == CORE_OK && result->instance != 0;
        if (ok) {
            launcher->instance = result->instance;
        }
        break;
    case STEP_WAIT_RUNNING:
        if (error == CORE_OK && result->status.state == APP_INSTANCE_STATE_RUNNING) {
            ok = true;
        } else if (error == CORE_OK && ++launcher->polls < MAX_POLLS) {
            return; /* not yet; the timer asks again */
        }
        break;
    case STEP_STOP:
        ok = error == CORE_OK;
        break;
    case STEP_WAIT_EXITED:
        if (error == CORE_OK && result->status.state == APP_INSTANCE_STATE_EXITED) {
            ok = result->status.exit_kind == APP_EXIT_KIND_EXITED &&
                 result->status.exit_value == 0;
        } else if (error == CORE_OK && ++launcher->polls < MAX_POLLS) {
            return;
        }
        break;
    case STEP_STOP_EXITED:
        ok = error == CORE_INVALID_STATE;
        break;
    default:
        break;
    }
    if (!ok) {
        fail(launcher, (i32)launcher->step);
        return;
    }
    launcher->step = (Step)(launcher->step + 1);
    launcher->polls = 0;
    advance(launcher);
}

/* Fires while waiting: for the marker file, then for the status polls. */
static void on_timer(AppHandle app, AppEventSourceId source, EventMask mask, void *context)
{
    Launcher *launcher = context;

    (void)source;
    (void)mask;
    if (launcher->step == STEP_WAIT_READY) {
        i32 fd = open(launcher->marker, O_RDONLY, 0);

        if (fd >= 0) {
            (void)close(fd);
            launcher->step = STEP_WAIT_RUNNING;
        } else if (++launcher->polls >= MAX_POLLS) {
            fail(launcher, (i32)STEP_WAIT_READY);
        }
        return;
    }
    if ((launcher->step == STEP_WAIT_RUNNING || launcher->step == STEP_WAIT_EXITED) &&
        !launcher->poll_in_flight) {
        launcher->poll_in_flight = true;
        if (app_apps_get_status(app, launcher->instance, on_reply, launcher, NULL) != CORE_OK) {
            fail(launcher, (i32)launcher->step);
        }
    }
}

static void advance(Launcher *launcher)
{
    AppHandle app = launcher->app;
    ErrorCode result = CORE_OK;

    switch (launcher->step) {
    case STEP_LIST:
        result = app_apps_list(app, on_reply, launcher, NULL);
        break;
    case STEP_INFO:
        result = app_apps_get_info(app, "fwchild", on_reply, launcher, NULL);
        break;
    case STEP_INFO_UNKNOWN:
        result = app_apps_get_info(app, "nosuch", on_reply, launcher, NULL);
        break;
    case STEP_LAUNCH_UNKNOWN:
        result = app_apps_launch(app, "nosuch", on_reply, launcher, NULL);
        break;
    case STEP_STATUS_UNKNOWN:
        result = app_apps_get_status(app, 999999, on_reply, launcher, NULL);
        break;
    case STEP_LAUNCH:
        result = app_apps_launch(app, "fwchild", on_reply, launcher, NULL);
        break;
    case STEP_WAIT_READY:
        launcher->polls = 0; /* on_timer() looks for the marker */
        return;
    case STEP_WAIT_RUNNING:
    case STEP_WAIT_EXITED:
        return;             /* on_timer() asks for the status */
    case STEP_STOP:
        result = app_apps_stop(app, launcher->instance, on_reply, launcher, NULL);
        break;
    case STEP_STOP_EXITED:
        result = app_apps_stop(app, launcher->instance, on_reply, launcher, NULL);
        break;
    case STEP_DONE:
        (void)app_log(app, APP_LOG_LEVEL_INFO, "launcher finished");
        (void)app_request_quit(app);
        return;
    }
    if (result != CORE_OK) {
        fail(launcher, (i32)launcher->step);
    }
}

int main(int argc, char **argv)
{
    AppConfig config = { .application_id = "fwlauncher", .log_component = "fwlauncher" };
    Launcher launcher = { 0 };
    AppHandle app = NULL;
    ErrorCode run_result;
    bool full;

    if (argc < 4) {
        return 90;
    }
    full = equal(argv[3], "full");
    if (full && argc != 5) {
        return 90;
    }
    libappframework_internal_appserv_endpoint = argv[1];
    libappframework_internal_logserv_endpoint = argv[2];
    if (app_create(&config, &app) != CORE_OK) {
        return 91;
    }
    launcher.app = app;
    launcher.marker = full ? argv[4] : NULL;
    if (full) {
        launcher.step = STEP_LIST;
        if (app_event_create_timer(app, POLL_PERIOD_MS, POLL_PERIOD_MS, on_timer, &launcher,
                                   &launcher.timer) != CORE_OK) {
            return 92;
        }
        advance(&launcher);
        /* After STEP_LAUNCH the next step waits for the marker file. */
    } else {
        (void)app_log(app, APP_LOG_LEVEL_INFO, "launcher waiting");
    }
    run_result = app_run(app);
    if (app_destroy(&app) != CORE_OK) {
        return 94;
    }
    if (launcher.failure != 0) {
        return launcher.failure;
    }
    return run_result == CORE_OK ? 0 : 95;
}
