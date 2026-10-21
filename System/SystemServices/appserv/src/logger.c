#include "logger.h"
#include "wire.h"
#include <punos/syscall/syscall.h>

static ErrorCode flush(AppLogger *logger)
{
    bool pending;
    ErrorCode error = ipc_connection_flush(logger->connection, &pending);
    if (error != CORE_OK) return error;
    return eventloop_update_fd(logger->loop, logger->source,
                               EVENT_READABLE | (pending ? EVENT_WRITABLE : 0));
}

ErrorCode app_logger_open(AppLogger *logger, EventLoopHandle loop)
{
    String endpoint = { (char *)APP_LOG_ENDPOINT, sizeof(APP_LOG_ENDPOINT) - 1 };
    i32 fd;
    ErrorCode error;
    *logger = (AppLogger){ .loop = loop, .pid = getpid(), .tid = gettid() };
    error = ipc_connection_begin_connect(&endpoint, &logger->connection);
    if (error != CORE_OK) return error;
    error = ipc_connection_get_fd(logger->connection, &fd);
    if (error == CORE_OK)
        error = eventloop_add_fd(loop, fd, EVENT_READABLE | EVENT_WRITABLE, &logger->source);
    return error;
}

ErrorCode app_logger_event(AppLogger *logger)
{
    ErrorCode error;
    if (!logger->connected) {
        IpcConnectionState state;
        error = ipc_connection_finish_connect(logger->connection, &state);
        if (error != CORE_OK) return error;
        if (state == IPC_CONNECTION_STATE_CONNECTING) return CORE_OK;
        if (state != IPC_CONNECTION_STATE_OPEN) return CORE_IO_ERROR;
        logger->connected = true;
    }
    /* Drain frames already buffered by ipclib, even when the socket is no
     * longer readable. Only our bounded outstanding queue produces replies. */
    for (;;) {
        IpcMessage message = { 0 };
        IpcReceiveStatus status;
        error = ipc_connection_receive(logger->connection, &status, &message);
        if (error != CORE_OK) return error;
        if (status == IPC_RECEIVE_STATUS_PEER_CLOSED) return CORE_IO_ERROR;
        if (status == IPC_RECEIVE_STATUS_NO_MESSAGE) break;
        if (message.type != 2 || message.payload.size != 4 ||
            logger->outstanding == 0 || wire_read(message.payload.data, 4) != CORE_OK)
            error = CORE_IO_ERROR;
        else --logger->outstanding;
        (void)ipc_message_destroy(&message);
        if (error != CORE_OK) return error;
    }
    return flush(logger);
}

static void append_text(u8 *buffer, usize *length, const char *text)
{
    while (*text != '\0') buffer[(*length)++] = (u8)*text++;
}

static void append_number(u8 *buffer, usize *length, u64 number)
{
    u8 digits[20];
    usize count = 0;
    do { digits[count++] = (u8)('0' + number % 10); number /= 10; } while (number != 0);
    while (count != 0) buffer[(*length)++] = digits[--count];
}

void app_logger_record(void *context, const char *event, const NativeAppSpec *app,
                        AppInstanceId id, const ProcessExitStatus *status,
                        ErrorCode error)
{
    AppLogger *logger = context;
    u8 buffer[512] = { 0 };
    usize length = 24; /* 17-byte header + seven-byte component. */
    IpcMessage message;
    if (!logger->connected || logger->error != CORE_OK) {
        logger->error = CORE_IO_ERROR;
        return;
    }
    wire_write(buffer, 2, 1);
    wire_write(buffer + 2, 4, (u32)logger->pid);
    wire_write(buffer + 6, 4, (u32)logger->tid);
    buffer[10] = error == CORE_OK ? 2 : 4; /* LogSeverity INFO / ERROR. */
    wire_write(buffer + 11, 2, 7);
    usize component = 17;
    append_text(buffer, &component, "appserv");
    append_text(buffer, &length, event);
    if (app != NULL) {
        append_text(buffer, &length, " application_id=");
        for (usize i = 0; i < app->application_id.length; ++i)
            buffer[length++] = (u8)app->application_id.data[i];
    }
    append_text(buffer, &length, " instance_id=");
    append_number(buffer, &length, id);
    append_text(buffer, &length, " error=");
    append_number(buffer, &length, error);
    if (status != NULL) {
        append_text(buffer, &length, " exit_kind=");
        append_number(buffer, &length, status->kind);
        append_text(buffer, &length, " exit_value=");
        if (status->value < 0) {
            buffer[length++] = '-';
            append_number(buffer, &length, (u64)(-(i64)status->value));
        } else append_number(buffer, &length, (u64)status->value);
    }
    wire_write(buffer + 13, 4, length - 24);
    message = (IpcMessage){ 1, { buffer, length } };
    logger->error = ipc_connection_send(logger->connection, &message);
    if (logger->error == CORE_OK) {
        ++logger->outstanding;
        logger->error = flush(logger);
    }
}

void app_logger_close(AppLogger *logger)
{
    if (logger->source != 0) (void)eventloop_remove_source(logger->loop, logger->source);
    if (logger->connection != NULL) (void)ipc_connection_close(&logger->connection);
    logger->source = 0;
}
