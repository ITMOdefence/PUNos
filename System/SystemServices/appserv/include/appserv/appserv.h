#ifndef PUNOS_APPSERV_H
#define PUNOS_APPSERV_H

#include <sys/core.h>
#include <sys/memory.h>
#include <sys/process.h>
#include <sys/string.h>

/* libcore currently exports u64 but not AppInstanceId. Keep the service's
 * alias here until the common platform type is added upstream. Never a PID. */
typedef u64 AppInstanceId;

#define APP_PROTOCOL_VERSION 1
#define APP_CATALOG_MAX_COUNT 64
#define APP_TEXT_MAX_LENGTH 64
#ifndef APP_ENDPOINT
#define APP_ENDPOINT "/run/punos/appserv.sock"
#endif
#define APP_MONITOR_PERIOD_MS 1000

typedef enum AppMessageType {
    APP_LIST = 1, APP_LIST_RESULT = 2,
    APP_GET_INFO = 3, APP_GET_INFO_RESULT = 4,
    APP_LAUNCH = 5, APP_LAUNCH_RESULT = 6,
    APP_STOP = 7, APP_STOP_RESULT = 8,
    APP_GET_STATUS = 9, APP_GET_STATUS_RESULT = 10
} AppMessageType;

/* Numeric wire values follow the specification's state table order. */
typedef enum AppState {
    APP_STARTING = 0, APP_RUNNING = 1, APP_TERMINATING = 2, APP_EXITED = 3
} AppState;

typedef struct NativeAppLaunchSpec {
    String application_id;
    String executable;
} NativeAppLaunchSpec;

typedef struct NativeAppSpec {
    String application_id;
    String display_name;
    String executable;
    const String *default_arguments;
    usize argument_count;
    const String *environment;
    usize environment_count;
    bool terminates_on_sigterm;
} NativeAppSpec;

/* All strings and arrays are borrowed static storage for the service lifetime. */
typedef struct AppCatalog {
    String registry_id;
    String expected_registry_id;
    const NativeAppLaunchSpec *launch_specs;
    const NativeAppSpec *applications;
    usize count;
} AppCatalog;

typedef struct AppInstance AppInstance;
typedef void (*AppLogFn)(void *context, const char *event,
                         const NativeAppSpec *app, AppInstanceId id,
                         const ProcessExitStatus *status, ErrorCode error);

typedef struct AppService {
    const AppCatalog *catalog;
    AppInstance *instances;
    AppInstanceId next_id;
    bool ready;
    AppLogFn log;
    void *log_context;
} AppService;

typedef struct AppReply {
    u32 type;
    MemoryBlock payload;
    usize size; /* Wire length; payload.size is the owning allocation length. */
} AppReply;

ErrorCode app_catalog_validate(const AppCatalog *catalog);
ErrorCode app_service_init(AppService *service, const AppCatalog *catalog,
                           AppLogFn log, void *log_context);
/* Out reply owns memory, even for protocol errors. Transport failures are the
 * return value; protocol errors are LE u32 ErrorCode in the reply payload. */
ErrorCode app_service_request(AppService *service, u32 type, Bytes payload,
                              AppReply *out_reply);
ErrorCode app_service_poll(AppService *service);
/* Disable launches before shutdown begins. Shutdown sends TERM to all active
 * children before waiting for any, and preserves failed handles for retry. */
ErrorCode app_service_shutdown(AppService *service);

#endif
