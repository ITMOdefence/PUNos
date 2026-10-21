#include "logger.h"
#include "wire.h"
#include <punos/syscall/syscall.h>

#ifndef LOG_ENDPOINT
#define LOG_ENDPOINT "/run/punos/logserv.sock"
#endif

static usize append(u8 *out, usize at, const char *value)
{
    while (*value != '\0') out[at++] = (u8)*value++;
    return at;
}

static usize append_number(u8 *out, usize at, u32 value)
{
    u8 digits[10];
    usize count = 0;
    do { digits[count++] = (u8)('0' + value % 10); value /= 10; } while (value);
    while (count) out[at++] = digits[--count];
    return at;
}

static ErrorCode flush(ServiceLogger *logger)
{
    bool pending;
    ErrorCode error = ipc_connection_flush(logger->connection, &pending);
    if (error != CORE_OK) return error;
    return eventloop_update_fd(logger->loop, logger->source,
                               EVENT_READABLE | (pending ? EVENT_WRITABLE : 0));
}

ErrorCode service_logger_open(ServiceLogger *logger, EventLoopHandle loop,
                              const char *component)
{
    String endpoint = { (char *)LOG_ENDPOINT, sizeof(LOG_ENDPOINT) - 1 };
    i32 fd;
    *logger = (ServiceLogger){ .loop = loop, .component = component };
    ErrorCode error = ipc_connection_begin_connect(&endpoint, &logger->connection);
    if (error != CORE_OK) return error;
    error = ipc_connection_get_fd(logger->connection, &fd);
    if (error == CORE_OK)
        error = eventloop_add_fd(loop, fd, EVENT_READABLE | EVENT_WRITABLE, &logger->source);
    return error;
}

ErrorCode service_logger_event(ServiceLogger *logger)
{
    ErrorCode error;
    if (!logger->connected) {
        IpcConnectionState state;
        error = ipc_connection_finish_connect(logger->connection, &state);
        if (error != CORE_OK) return logger->error = error;
        if (state == IPC_CONNECTION_STATE_CONNECTING) return CORE_OK;
        if (state != IPC_CONNECTION_STATE_OPEN) return logger->error = CORE_IO_ERROR;
        logger->connected = true;
    }
    for (;;) {
        IpcReceiveStatus status;
        IpcMessage reply = { 0 };
        error = ipc_connection_receive(logger->connection, &status, &reply);
        if (error != CORE_OK) return logger->error = error;
        if (status == IPC_RECEIVE_STATUS_PEER_CLOSED) return logger->error = CORE_IO_ERROR;
        if (status == IPC_RECEIVE_STATUS_NO_MESSAGE) break;
        if (reply.type != 2 || reply.payload.size != 4 || logger->outstanding == 0 ||
            wire_read(reply.payload.data, 4) != CORE_OK) error = CORE_IO_ERROR;
        else --logger->outstanding;
        (void)ipc_message_destroy(&reply);
        if (error != CORE_OK) return logger->error = error;
    }
    return logger->error = flush(logger);
}

ErrorCode service_logger_record(ServiceLogger *logger, const char *event,
                                ErrorCode reason)
{
    if (!logger->connected || logger->error != CORE_OK) return CORE_IO_ERROR;
    u8 bytes[512] = { 0 };
    usize component_size = 0, event_size = 0;
    while (logger->component[component_size]) ++component_size;
    while (event[event_size]) ++event_size;
    if (component_size > 256 || event_size > 400 ||
        17 + component_size + event_size + 17 > sizeof(bytes)) return CORE_OVERFLOW;
    wire_write(bytes, 2, 1);
    wire_write(bytes + 2, 4, (u32)getpid());
    wire_write(bytes + 6, 4, (u32)gettid());
    bytes[10] = reason == CORE_OK ? 2 : 4;
    wire_write(bytes + 11, 2, component_size);
    usize at = 17;
    at = append(bytes, at, logger->component);
    usize message_start = at;
    at = append(bytes, at, event);
    if (reason != CORE_OK) {
        at = append(bytes, at, " error=");
        at = append_number(bytes, at, (u32)reason);
    }
    if (at > sizeof(bytes)) return CORE_OVERFLOW;
    wire_write(bytes + 13, 4, at - message_start);
    IpcMessage message = { 1, { bytes, at } };
    ErrorCode error = ipc_connection_send(logger->connection, &message);
    if (error == CORE_OK) {
        ++logger->outstanding;
        error = flush(logger);
    }
    return logger->error = error;
}

void service_logger_drain(ServiceLogger *logger)
{
    for (usize tries = 0; logger->connected && logger->outstanding &&
             logger->error == CORE_OK && tries < 10; ++tries) {
        Event events[4];
        usize count;
        if (eventloop_wait(logger->loop, events, 4, 100, &count) != CORE_OK) break;
        for (usize i = 0; i < count; ++i)
            if (events[i].source_id == logger->source)
                (void)service_logger_event(logger);
    }
}

void service_logger_close(ServiceLogger *logger)
{
    if (logger->source) (void)eventloop_remove_source(logger->loop, logger->source);
    if (logger->connection) (void)ipc_connection_close(&logger->connection);
    logger->source = 0;
}
