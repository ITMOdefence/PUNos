/*
 * An application built on appframework, started by the real appserv in the host
 * integration test. It logs, announces that it is ready by creating a marker
 * file, and runs until SIGTERM.
 *
 * argv[1]  endpoint of logserv
 * argv[2]  marker file created when the SIGTERM handler is in place
 */

#include <appframework/app.h>
#include <punos/syscall/constants.h>
#include <punos/syscall/syscall.h>

#include "internal.h"

int main(int argc, char **argv)
{
    AppConfig config = { .application_id = "fwchild", .log_component = "fwchild" };
    AppHandle app = NULL;
    ErrorCode run_result;
    i32 marker;

    if (argc != 3) {
        return 90;
    }
    libappframework_internal_logserv_endpoint = argv[1];
    if (app_create(&config, &app) != CORE_OK) {
        return 91;
    }
    if (app_log(app, APP_LOG_LEVEL_INFO, "child ready") != CORE_OK) {
        return 92;
    }
    marker = open(argv[2], O_WRONLY | O_CREAT, 0600);
    if (marker < 0) {
        return 93;
    }
    (void)write(marker, "ready", 5);
    (void)close(marker);

    run_result = app_run(app);
    /* app_log() is refused in Stopping; the library logs the SIGTERM itself. */
    if (app_destroy(&app) != CORE_OK) {
        return 94;
    }
    return run_result == CORE_OK ? 0 : 95;
}
