#include "logger.h"

#include <punos/syscall/syscall.h>

#include "wire.h"

/* Больше неподтверждённых записей брокер не ставит: журнал не повод расти в
 * памяти. */
#define MSG_LOG_MAX_OUTSTANDING ((u64)256)

#define MSG_LOG_COMPONENT "msgserv"
#define MSG_LOG_COMPONENT_LENGTH (sizeof(MSG_LOG_COMPONENT) - 1)
#define MSG_LOG_HEADER_SIZE ((usize)17)

static ErrorCode flush(MsgLogger *logger)
{
    bool pending = false;
    ErrorCode error = ipc_connection_flush(logger->connection, &pending);

    if (error != CORE_OK) {
        return error;
    }
    return eventloop_update_fd(logger->loop, logger->source,
                               EVENT_READABLE | (pending ? EVENT_WRITABLE : 0));
}

ErrorCode msg_logger_open(MsgLogger *logger, EventLoopHandle loop)
{
    String endpoint = { (char *)MSG_LOG_ENDPOINT, sizeof(MSG_LOG_ENDPOINT) - 1 };
    i32 fd = -1;
    ErrorCode error;

    *logger = (MsgLogger){ .loop = loop, .pid = getpid(), .tid = gettid() };
    error = ipc_connection_begin_connect(&endpoint, &logger->connection);
    if (error != CORE_OK) {
        logger->connection = NULL;
        return error;
    }
    error = ipc_connection_get_fd(logger->connection, &fd);
    if (error == CORE_OK) {
        error = eventloop_add_fd(loop, fd, EVENT_READABLE | EVENT_WRITABLE, &logger->source);
    }
    return error;
}

ErrorCode msg_logger_event(MsgLogger *logger)
{
    ErrorCode error;

    if (logger->connection == NULL) {
        return CORE_OK;
    }
    if (!logger->connected) {
        IpcConnectionState state = IPC_CONNECTION_STATE_CONNECTING;

        error = ipc_connection_finish_connect(logger->connection, &state);
        if (error != CORE_OK) {
            return error;
        }
        if (state == IPC_CONNECTION_STATE_CONNECTING) {
            return CORE_OK;
        }
        if (state != IPC_CONNECTION_STATE_OPEN) {
            return CORE_IO_ERROR;
        }
        logger->connected = true;
    }
    /* Разбираем кадры, уже сложенные ipclib, даже если сокет больше не
     * читаем: ответы порождает только собственная ограниченная очередь. */
    for (;;) {
        IpcMessage message = { 0, { NULL, 0 } };
        IpcReceiveStatus status = IPC_RECEIVE_STATUS_NO_MESSAGE;

        error = ipc_connection_receive(logger->connection, &status, &message);
        if (error != CORE_OK) {
            return error;
        }
        if (status == IPC_RECEIVE_STATUS_PEER_CLOSED) {
            return CORE_IO_ERROR;
        }
        if (status == IPC_RECEIVE_STATUS_NO_MESSAGE) {
            break;
        }
        if (message.type != 2 || message.payload.size != 4 || logger->outstanding == 0) {
            error = CORE_IO_ERROR;
        } else {
            MsgReader reader = msg_reader(message.payload.data, message.payload.size);

            /* Отказ logserv тоже считается подтверждением: запись потеряна,
             * но соединение исправно и собственная очередь не растёт. */
            (void)msg_read_u32(&reader);
            --logger->outstanding;
        }
        (void)ipc_message_destroy(&message);
        if (error != CORE_OK) {
            return error;
        }
    }
    return flush(logger);
}

static void append_text(u8 *buffer, usize *length, const char *text)
{
    while (*text != '\0') {
        buffer[(*length)++] = (u8)*text++;
    }
}

static void append_number(u8 *buffer, usize *length, u64 number)
{
    u8 digits[20];
    usize count = 0;

    do {
        digits[count++] = (u8)('0' + number % 10);
        number /= 10;
    } while (number != 0);
    while (count != 0) {
        buffer[(*length)++] = digits[--count];
    }
}

void msg_logger_record(void *context, const char *event, const char *application_id,
                       MsgClientId client, ErrorCode error)
{
    MsgLogger *logger = context;
    /* Хватает на самое длинное сообщение: событие, application_id и два
     * числа. Усечения не бывает, потому что имена событий - литералы. */
    u8 buffer[512] = { 0 };
    usize length = MSG_LOG_HEADER_SIZE + MSG_LOG_COMPONENT_LENGTH;
    usize component = MSG_LOG_HEADER_SIZE;
    MsgWriter header = msg_writer(buffer, MSG_LOG_HEADER_SIZE);
    IpcMessage message;

    if (!logger->connected || logger->error != CORE_OK ||
        logger->outstanding >= MSG_LOG_MAX_OUTSTANDING) {
        return; /* журнал недоступен: брокер продолжает работать без него */
    }
    msg_write_u16(&header, 1); /* версия протокола logserv */
    msg_write_u32(&header, (u32)logger->pid);
    msg_write_u32(&header, (u32)logger->tid);
    msg_write_u8(&header, error == CORE_OK ? 2 : 3); /* LogSeverity INFO / WARNING */
    msg_write_u16(&header, (u16)MSG_LOG_COMPONENT_LENGTH);
    msg_write_u32(&header, 0); /* message_length, дописывается ниже */
    append_text(buffer, &component, MSG_LOG_COMPONENT);
    append_text(buffer, &length, event);
    if (application_id != NULL) {
        append_text(buffer, &length, " application_id=");
        append_text(buffer, &length, application_id);
    }
    append_text(buffer, &length, " client_id=");
    append_number(buffer, &length, client);
    append_text(buffer, &length, " error=");
    append_number(buffer, &length, (u64)error);
    {
        MsgWriter tail = msg_writer(buffer + 13, 4);

        msg_write_u32(&tail, (u32)(length - MSG_LOG_HEADER_SIZE - MSG_LOG_COMPONENT_LENGTH));
    }
    message = (IpcMessage){ 1, { buffer, length } };
    logger->error = ipc_connection_send(logger->connection, &message);
    if (logger->error == CORE_OK) {
        ++logger->outstanding;
        logger->error = flush(logger);
    }
}

void msg_logger_close(MsgLogger *logger)
{
    if (logger->source != 0) {
        (void)eventloop_remove_source(logger->loop, logger->source);
        logger->source = 0;
    }
    if (logger->connection != NULL) {
        (void)ipc_connection_close(&logger->connection);
    }
}
