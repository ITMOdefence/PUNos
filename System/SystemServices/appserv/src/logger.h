#ifndef APPSERV_LOGGER_H
#define APPSERV_LOGGER_H
#include <appserv/appserv.h>
#include <special/ipc.h>
#include <sys/eventloop.h>

#ifndef APP_LOG_ENDPOINT
#define APP_LOG_ENDPOINT "/run/punos/logserv.sock"
#endif

typedef struct AppLogger {
    IpcConnectionHandle connection;
    EventSourceId source;
    EventLoopHandle loop;
    bool connected;
    u64 outstanding;
    ErrorCode error;
    i32 pid;
    i32 tid;
} AppLogger;

ErrorCode app_logger_open(AppLogger *logger, EventLoopHandle loop);
ErrorCode app_logger_event(AppLogger *logger);
void app_logger_record(void *context, const char *event, const NativeAppSpec *app,
                        AppInstanceId id, const ProcessExitStatus *status,
                        ErrorCode error);
void app_logger_close(AppLogger *logger);
#endif
