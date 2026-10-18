/*
 * Compile-time checks of the public ABI of appframework. The numeric values and
 * layouts are part of the contract (DevDoc/ApplicationLayer/appframework.md);
 * the checks also pin the values that must stay equal to those of loglib,
 * libprocess and appserv.
 */

#include <appframework/app.h>
#include <special/log.h>
#include <sys/process.h>

#define OFFSET(type, member) __builtin_offsetof(type, member)
/* Enumerations of different types are compared through int. */
#define SAME(left, right) ((int)(left) == (int)(right))

/* Constants. */
_Static_assert(APP_ID_MAX_LENGTH == 64, "application_id limit");
_Static_assert(APP_LOG_MESSAGE_MAX_LENGTH == LOG_MESSAGE_MAX_LENGTH,
               "message limit equals loglib");
_Static_assert(APP_LOG_COMPONENT_MAX_LENGTH == LOG_COMPONENT_MAX_LENGTH,
               "component limit equals loglib");
_Static_assert(APP_APPS_MAX_PENDING == 256, "appserv queue limit");
_Static_assert(APP_LOG_MAX_PENDING == 256, "logserv queue limit");
_Static_assert(APP_PROTOCOL_VERSION == 1, "protocol version");

/* Scalar types. */
_Static_assert(sizeof(AppHandle) == sizeof(void *), "AppHandle is a pointer");
_Static_assert(sizeof(AppEventSourceId) == 8, "AppEventSourceId is u64");
_Static_assert(sizeof(AppRequestId) == 8, "AppRequestId is u64");
_Static_assert(sizeof(AppInstanceId) == 8, "AppInstanceId is u64");
_Static_assert((AppEventSourceId)-1 > 0, "AppEventSourceId is unsigned");
_Static_assert((AppRequestId)-1 > 0, "AppRequestId is unsigned");
_Static_assert((AppInstanceId)-1 > 0, "AppInstanceId is unsigned");

/* AppLogLevel equals LogSeverity. */
_Static_assert(APP_LOG_LEVEL_TRACE == 0 && SAME(APP_LOG_LEVEL_TRACE, LOG_SEVERITY_TRACE), "Trace");
_Static_assert(APP_LOG_LEVEL_DEBUG == 1 && SAME(APP_LOG_LEVEL_DEBUG, LOG_SEVERITY_DEBUG), "Debug");
_Static_assert(APP_LOG_LEVEL_INFO == 2 && SAME(APP_LOG_LEVEL_INFO, LOG_SEVERITY_INFO), "Info");
_Static_assert(APP_LOG_LEVEL_WARNING == 3 && SAME(APP_LOG_LEVEL_WARNING, LOG_SEVERITY_WARNING), "Warning");
_Static_assert(APP_LOG_LEVEL_ERROR == 4 && SAME(APP_LOG_LEVEL_ERROR, LOG_SEVERITY_ERROR), "Error");
_Static_assert(APP_LOG_LEVEL_FATAL == 5 && SAME(APP_LOG_LEVEL_FATAL, LOG_SEVERITY_FATAL), "Fatal");

/* AppAppsOperation. */
_Static_assert(APP_APPS_OPERATION_LIST == 0, "List");
_Static_assert(APP_APPS_OPERATION_GET_INFO == 1, "GetInfo");
_Static_assert(APP_APPS_OPERATION_LAUNCH == 2, "Launch");
_Static_assert(APP_APPS_OPERATION_STOP == 3, "Stop");
_Static_assert(APP_APPS_OPERATION_GET_STATUS == 4, "GetStatus");

/* AppInstanceState follows the wire values of appserv. */
_Static_assert(APP_INSTANCE_STATE_STARTING == 0, "Starting");
_Static_assert(APP_INSTANCE_STATE_RUNNING == 1, "Running");
_Static_assert(APP_INSTANCE_STATE_TERMINATING == 2, "Terminating");
_Static_assert(APP_INSTANCE_STATE_EXITED == 3, "Exited");

/* AppExitKind equals ProcessTerminationKind. */
_Static_assert(APP_EXIT_KIND_EXITED == 0 &&
                   SAME(APP_EXIT_KIND_EXITED, PROCESS_TERMINATION_KIND_EXITED), "Exited kind");
_Static_assert(APP_EXIT_KIND_SIGNALED == 1 &&
                   SAME(APP_EXIT_KIND_SIGNALED, PROCESS_TERMINATION_KIND_SIGNALED), "Signaled kind");

/* Structures. */
_Static_assert(sizeof(AppConfig) == 16, "AppConfig size");
_Static_assert(OFFSET(AppConfig, application_id) == 0, "AppConfig.application_id");
_Static_assert(OFFSET(AppConfig, log_component) == 8, "AppConfig.log_component");

_Static_assert(sizeof(AppInfo) == 16, "AppInfo size");
_Static_assert(OFFSET(AppInfo, application_id) == 0, "AppInfo.application_id");
_Static_assert(OFFSET(AppInfo, display_name) == 8, "AppInfo.display_name");

_Static_assert(sizeof(AppInstanceStatus) == 12, "AppInstanceStatus size");
_Static_assert(OFFSET(AppInstanceStatus, state) == 0, "AppInstanceStatus.state");
_Static_assert(OFFSET(AppInstanceStatus, exit_kind) == 4, "AppInstanceStatus.exit_kind");
_Static_assert(OFFSET(AppInstanceStatus, exit_value) == 8, "AppInstanceStatus.exit_value");

_Static_assert(sizeof(AppAppsResult) == 24, "AppAppsResult size");
_Static_assert(OFFSET(AppAppsResult, operation) == 0, "AppAppsResult.operation");
_Static_assert(OFFSET(AppAppsResult, list.items) == 8, "AppAppsResult.list.items");
_Static_assert(OFFSET(AppAppsResult, list.count) == 16, "AppAppsResult.list.count");
_Static_assert(OFFSET(AppAppsResult, info) == 8, "AppAppsResult.info");
_Static_assert(OFFSET(AppAppsResult, instance) == 8, "AppAppsResult.instance");
_Static_assert(OFFSET(AppAppsResult, status) == 8, "AppAppsResult.status");

/* Callback types accept exactly the documented parameters. */
static void event_handler(AppHandle app, AppEventSourceId source, EventMask mask,
                          void *context)
{
    (void)app;
    (void)source;
    (void)mask;
    (void)context;
}

static void apps_callback(AppHandle app, AppRequestId request, ErrorCode error,
                          const AppAppsResult *result, void *context)
{
    (void)app;
    (void)request;
    (void)error;
    (void)result;
    (void)context;
}

/* The operations have the documented signatures: assigning them to pointers of
 * the exact type fails to compile on any difference. */
ErrorCode (*const abi_app_create)(const AppConfig *, AppHandle *) = app_create;
ErrorCode (*const abi_app_run)(AppHandle) = app_run;
ErrorCode (*const abi_app_request_quit)(AppHandle) = app_request_quit;
ErrorCode (*const abi_app_destroy)(AppHandle *) = app_destroy;
ErrorCode (*const abi_app_event_add_fd)(AppHandle, i32, EventMask, AppEventHandler,
                                        void *, AppEventSourceId *) = app_event_add_fd;
ErrorCode (*const abi_app_event_create_timer)(AppHandle, DurationMs, DurationMs,
                                              AppEventHandler, void *,
                                              AppEventSourceId *) = app_event_create_timer;
ErrorCode (*const abi_app_event_remove)(AppHandle, AppEventSourceId) = app_event_remove;
ErrorCode (*const abi_app_log)(AppHandle, AppLogLevel, const char *) = app_log;
ErrorCode (*const abi_app_apps_list)(AppHandle, AppAppsCallback, void *,
                                     AppRequestId *) = app_apps_list;
ErrorCode (*const abi_app_apps_get_info)(AppHandle, const char *, AppAppsCallback, void *,
                                         AppRequestId *) = app_apps_get_info;
ErrorCode (*const abi_app_apps_launch)(AppHandle, const char *, AppAppsCallback, void *,
                                       AppRequestId *) = app_apps_launch;
ErrorCode (*const abi_app_apps_stop)(AppHandle, AppInstanceId, AppAppsCallback, void *,
                                     AppRequestId *) = app_apps_stop;
ErrorCode (*const abi_app_apps_get_status)(AppHandle, AppInstanceId, AppAppsCallback,
                                           void *, AppRequestId *) = app_apps_get_status;

AppEventHandler const abi_event_handler = event_handler;
AppAppsCallback const abi_apps_callback = apps_callback;
