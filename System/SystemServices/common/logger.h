#ifndef PUNOS_DEVICE_LOGGER_H
#define PUNOS_DEVICE_LOGGER_H
#include <special/ipc.h>
#include <sys/eventloop.h>

typedef struct ServiceLogger {
    EventLoopHandle loop;
    IpcConnectionHandle connection;
    EventSourceId source;
    const char *component;
    bool connected;
    ErrorCode error;
    usize outstanding;
} ServiceLogger;

ErrorCode service_logger_open(ServiceLogger *logger, EventLoopHandle loop,
                              const char *component);
ErrorCode service_logger_event(ServiceLogger *logger);
ErrorCode service_logger_record(ServiceLogger *logger, const char *event,
                                ErrorCode reason);
void service_logger_drain(ServiceLogger *logger);
void service_logger_close(ServiceLogger *logger);
#endif
