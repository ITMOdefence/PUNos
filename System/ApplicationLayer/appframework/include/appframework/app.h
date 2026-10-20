#ifndef PUNOS_APPFRAMEWORK_APP_H
#define PUNOS_APPFRAMEWORK_APP_H

/*
 * appframework: infrastructure library of native PUNos applications.
 * Specification: DevDoc/ApplicationLayer/appframework.md (the contract).
 *
 * One AppHandle owns the main event loop of an application, a non-blocking
 * client of logserv, a non-blocking client of appserv and the SIGTERM handler.
 * Everything except app_request_quit() runs on the thread that called
 * app_create(). All operations return ErrorCode; CORE_OK is the only success.
 *
 *     AppHandle app = NULL;
 *     AppConfig config = { .application_id = "desktop", .log_component = "desktop" };
 *     app_create(&config, &app);
 *     app_run(app);
 *     app_destroy(&app);
 */

#include <sys/core.h>
#include <sys/eventloop.h>
#include <sys/time.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/* Longest application_id and display_name, in bytes (appserv IPC contract). */
#define APP_ID_MAX_LENGTH ((usize)64)
/* Longest app_log() message; equals LOG_MESSAGE_MAX_LENGTH of loglib. */
#define APP_LOG_MESSAGE_MAX_LENGTH ((usize)16384)
/* Longest log_component; equals LOG_COMPONENT_MAX_LENGTH of loglib. */
#define APP_LOG_COMPONENT_MAX_LENGTH ((usize)256)
/* Requests to appserv that may wait for an answer at the same time. */
#define APP_APPS_MAX_PENDING ((usize)256)
/* Log records that may wait to be sent or acknowledged at the same time. */
#define APP_LOG_MAX_PENDING ((usize)256)
/* Version of the appserv and logserv protocols that framework speaks. */
#define APP_PROTOCOL_VERSION ((u16)1)

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/* Context of one application. Opaque; created by app_create(). */
typedef struct AppHandle *AppHandle;

/* Strings are NUL-terminated UTF-8, copied by app_create(). */
typedef struct AppConfig {
    const char *application_id; /* 1..APP_ID_MAX_LENGTH bytes */
    const char *log_component;  /* 1..APP_LOG_COMPONENT_MAX_LENGTH bytes */
} AppConfig;

/* Non-zero while the source is registered. */
typedef u64 AppEventSourceId;

/* Non-zero; grows by one per request, starting at 1. Never reused. */
typedef u64 AppRequestId;

/* Non-zero identifier of an application instance given by appserv. Not a PID.
 * appserv.h declares the same alias; an identical typedef is valid in C11. */
typedef u64 AppInstanceId;

/*
 * Called on the owner thread. mask holds EVENT_* bits; a timer reports
 * EVENT_READABLE. The handler may remove its own source.
 */
typedef void (*AppEventHandler)(AppHandle app, AppEventSourceId source,
                                EventMask mask, void *context);

/* Equal to LogSeverity of loglib. */
typedef enum AppLogLevel {
    APP_LOG_LEVEL_TRACE = 0,
    APP_LOG_LEVEL_DEBUG = 1,
    APP_LOG_LEVEL_INFO = 2,
    APP_LOG_LEVEL_WARNING = 3,
    APP_LOG_LEVEL_ERROR = 4,
    APP_LOG_LEVEL_FATAL = 5
} AppLogLevel;

typedef enum AppAppsOperation {
    APP_APPS_OPERATION_LIST = 0,
    APP_APPS_OPERATION_GET_INFO = 1,
    APP_APPS_OPERATION_LAUNCH = 2,
    APP_APPS_OPERATION_STOP = 3,
    APP_APPS_OPERATION_GET_STATUS = 4
} AppAppsOperation;

/* Equal to the wire values of appserv. appserv never reports STARTING. */
typedef enum AppInstanceState {
    APP_INSTANCE_STATE_STARTING = 0,
    APP_INSTANCE_STATE_RUNNING = 1,
    APP_INSTANCE_STATE_TERMINATING = 2,
    APP_INSTANCE_STATE_EXITED = 3
} AppInstanceState;

/* Equal to ProcessTerminationKind of libprocess. */
typedef enum AppExitKind {
    APP_EXIT_KIND_EXITED = 0,
    APP_EXIT_KIND_SIGNALED = 1
} AppExitKind;

/* Both strings are NUL-terminated UTF-8, valid only during the callback. */
typedef struct AppInfo {
    const char *application_id;
    const char *display_name;
} AppInfo;

/* exit_kind and exit_value are defined only for APP_INSTANCE_STATE_EXITED;
 * otherwise they are 0. */
typedef struct AppInstanceStatus {
    AppInstanceState state;
    AppExitKind exit_kind;
    i32 exit_value;
} AppInstanceStatus;

/* Successful result of one appserv request, valid only during the callback.
 * The member that is defined depends on operation; STOP has no payload. */
typedef struct AppAppsResult {
    AppAppsOperation operation;
    union {
        struct {
            const AppInfo *items;
            usize count;
        } list;                 /* APP_APPS_OPERATION_LIST */
        AppInfo info;           /* APP_APPS_OPERATION_GET_INFO */
        AppInstanceId instance; /* APP_APPS_OPERATION_LAUNCH */
        AppInstanceStatus status; /* APP_APPS_OPERATION_GET_STATUS */
    };
} AppAppsResult;

/*
 * Called once for every request accepted by app_apps_*(), in request order,
 * and never before that call returned CORE_OK. result is NULL unless error is
 * CORE_OK. Callbacks run from app_run() (or from app_destroy(), which ends
 * pending requests with CORE_INVALID_STATE).
 */
typedef void (*AppAppsCallback)(AppHandle app, AppRequestId request,
                                ErrorCode error, const AppAppsResult *result,
                                void *context);

/* ------------------------------------------------------------------------ */
/* Operations                                                                */
/* ------------------------------------------------------------------------ */

/*
 * Validates config, creates the event loop and installs the SIGTERM handler.
 * At most one AppHandle exists at a time (CORE_INVALID_STATE otherwise). The
 * calling thread becomes the owner thread.
 */
ErrorCode app_create(const AppConfig *config, AppHandle *out_app);

/*
 * Runs the event loop until a quit is requested (CORE_OK) or the event loop
 * fails (that error, state Failed). Returns at once with CORE_OK if a quit was
 * requested before the call. Cannot be called twice.
 */
ErrorCode app_run(AppHandle app);

/*
 * Requests a clean exit from app_run(). Safe from any thread and from a signal
 * handler: it sets a flag, wakes the event loop and preserves errno.
 */
ErrorCode app_request_quit(AppHandle app);

/*
 * Ends pending requests (callbacks get CORE_INVALID_STATE), closes the IPC
 * clients, restores the previous SIGTERM handler, frees the context and writes
 * NULL to *app. Not allowed while app_run() is dispatching.
 */
ErrorCode app_destroy(AppHandle *app);

/* Registers a non-owned fd. mask is EVENT_READABLE and/or EVENT_WRITABLE. */
ErrorCode app_event_add_fd(AppHandle app, i32 fd, EventMask mask,
                           AppEventHandler handler, void *context,
                           AppEventSourceId *out_source);

/* period_ms == 0 makes a one-shot timer; delay_ms == 0 fires at once. */
ErrorCode app_event_create_timer(AppHandle app, DurationMs delay_ms,
                                 DurationMs period_ms, AppEventHandler handler,
                                 void *context, AppEventSourceId *out_source);

/* Removes a source. An external fd is not closed. */
ErrorCode app_event_remove(AppHandle app, AppEventSourceId source);

/*
 * Queues one record for logserv without blocking. message is NUL-terminated
 * UTF-8 of at most APP_LOG_MESSAGE_MAX_LENGTH bytes. CORE_OK means "queued",
 * not "stored".
 */
ErrorCode app_log(AppHandle app, AppLogLevel level, const char *message);

/*
 * Requests to appserv. CORE_OK means "queued"; the outcome, including the
 * ErrorCode of appserv, arrives in the callback. out_request may be NULL.
 */
ErrorCode app_apps_list(AppHandle app, AppAppsCallback callback, void *context,
                        AppRequestId *out_request);
ErrorCode app_apps_get_info(AppHandle app, const char *application_id,
                            AppAppsCallback callback, void *context,
                            AppRequestId *out_request);
ErrorCode app_apps_launch(AppHandle app, const char *application_id,
                          AppAppsCallback callback, void *context,
                          AppRequestId *out_request);
ErrorCode app_apps_stop(AppHandle app, AppInstanceId instance,
                        AppAppsCallback callback, void *context,
                        AppRequestId *out_request);
ErrorCode app_apps_get_status(AppHandle app, AppInstanceId instance,
                              AppAppsCallback callback, void *context,
                              AppRequestId *out_request);

#endif
