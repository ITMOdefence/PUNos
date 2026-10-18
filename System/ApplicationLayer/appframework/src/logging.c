#include "internal.h"

#include <sys/string.h>

/* log.submit, version 1 (appframework.md, "Протокол клиента logserv"). */
#define LOG_SUBMIT_TYPE 1
#define LOG_SUBMIT_RESULT_TYPE 2
#define LOG_SUBMIT_RESULT_SIZE 4
#define LOG_SUBMIT_HEADER_SIZE 17
#define LOG_LEVEL_LAST APP_LOG_LEVEL_FATAL
#define QUEUE_INITIAL_CAPACITY 8

const char *libappframework_internal_logserv_endpoint = "/run/punos/logserv.sock";

/* ---- encoding ---------------------------------------------------------- */

static void put_u16(u8 *out, u16 value)
{
    out[0] = (u8)(value & 0xff);
    out[1] = (u8)(value >> 8);
}

static void put_u32(u8 *out, u32 value)
{
    for (u32 i = 0; i < 4; ++i) {
        out[i] = (u8)((value >> (8 * i)) & 0xff);
    }
}

ErrorCode libappframework_internal_log_encode(AppHandle app, AppLogLevel level,
                                              const char *message, usize message_length,
                                              i32 process_id, i32 thread_id,
                                              MemoryBlock *out_payload)
{
    MemoryBlock block;
    ErrorCode result;
    u8 *out;

    result = memory_allocate(LOG_SUBMIT_HEADER_SIZE + app->log_component_length +
                                 message_length,
                             &block);
    if (result != CORE_OK) {
        return result;
    }
    out = block.data;
    put_u16(out, APP_PROTOCOL_VERSION);
    put_u32(out + 2, (u32)process_id);
    put_u32(out + 6, (u32)thread_id);
    out[10] = (u8)level;
    put_u16(out + 11, (u16)app->log_component_length);
    put_u32(out + 13, (u32)message_length);
    for (usize i = 0; i < app->log_component_length; ++i) {
        out[LOG_SUBMIT_HEADER_SIZE + i] = (u8)app->log_component[i];
    }
    for (usize i = 0; i < message_length; ++i) {
        out[LOG_SUBMIT_HEADER_SIZE + app->log_component_length + i] = (u8)message[i];
    }
    *out_payload = block;
    return CORE_OK;
}

/* ---- connection state ---------------------------------------------------- */

static usize pending_records(const AppLogClient *client)
{
    return client->queued + client->unacknowledged;
}

static void free_queue(AppLogClient *client)
{
    for (usize i = 0; i < client->queued; ++i) {
        (void)memory_free(&client->queue[i]);
    }
    client->queued = 0;
    (void)memory_free(&client->queue_block);
    client->queue = NULL;
    client->queue_capacity = 0;
}

/* Closes the connection and forgets every record that logserv has not
 * confirmed: they are not sent again (appframework.md, logserv protocol). */
static void drop_connection(AppHandle app)
{
    AppLogClient *client = &app->logserv;

    if (client->source != 0) {
        (void)libappframework_internal_unregister(app, client->source);
        client->source = 0;
    }
    if (client->connection != NULL) {
        (void)ipc_connection_close(&client->connection);
    }
    client->open = false;
    client->write_interest = false;
    client->unacknowledged = 0;
    free_queue(client);
}

static ErrorCode set_interest(AppHandle app, bool write)
{
    AppLogClient *client = &app->logserv;
    EventMask mask = client->open ? EVENT_READABLE : EVENT_WRITABLE;
    ErrorCode result;

    if (client->open && write) {
        mask |= EVENT_WRITABLE;
    }
    result = libappframework_internal_update_fd(app, client->source, mask);
    if (result == CORE_OK) {
        client->write_interest = (mask & EVENT_WRITABLE) != 0;
    }
    return result;
}

/* Sends what the socket accepts and asks for WRITABLE only while bytes remain. */
static ErrorCode flush(AppHandle app)
{
    AppLogClient *client = &app->logserv;
    bool pending = false;
    ErrorCode result = ipc_connection_flush(client->connection, &pending);

    if (result != CORE_OK) {
        return result;
    }
    if (pending != client->write_interest) {
        return set_interest(app, pending);
    }
    return CORE_OK;
}

static ErrorCode send_payload(AppHandle app, MemoryBlock *payload)
{
    AppLogClient *client = &app->logserv;
    IpcMessage message = { LOG_SUBMIT_TYPE, { payload->data, payload->size } };
    ErrorCode result = ipc_connection_send(client->connection, &message);

    if (result == CORE_OVERFLOW) {
        return CORE_WOULD_BLOCK;
    }
    if (result == CORE_OK) {
        client->unacknowledged++;
    }
    return result;
}

/* Connecting -> Open: the records that waited go out in order. */
static ErrorCode become_open(AppHandle app)
{
    AppLogClient *client = &app->logserv;
    ErrorCode result;

    client->open = true;
    for (usize i = 0; i < client->queued; ++i) {
        result = send_payload(app, &client->queue[i]);
        if (result != CORE_OK) {
            return result;
        }
        (void)memory_free(&client->queue[i]);
    }
    client->queued = 0;
    free_queue(client);
    result = set_interest(app, false);
    if (result != CORE_OK) {
        return result;
    }
    return flush(app);
}

/* Reads every complete reply. Anything but one 4-byte log.submit_result per
 * outstanding record is a protocol violation. */
static ErrorCode read_replies(AppHandle app)
{
    AppLogClient *client = &app->logserv;

    for (;;) {
        IpcReceiveStatus status = IPC_RECEIVE_STATUS_NO_MESSAGE;
        IpcMessage message = { 0, { NULL, 0 } };
        bool valid;
        ErrorCode result = ipc_connection_receive(client->connection, &status, &message);

        if (result != CORE_OK) {
            return result;
        }
        if (status == IPC_RECEIVE_STATUS_NO_MESSAGE) {
            return CORE_OK;
        }
        if (status == IPC_RECEIVE_STATUS_PEER_CLOSED) {
            return CORE_IO_ERROR;
        }
        valid = message.type == LOG_SUBMIT_RESULT_TYPE &&
                message.payload.size == LOG_SUBMIT_RESULT_SIZE && client->unacknowledged > 0;
        (void)ipc_message_destroy(&message);
        if (!valid) {
            return CORE_IO_ERROR;
        }
        /* The error code in the reply (a full logserv queue, for instance)
         * means the record is lost; there is nothing to do with it. */
        client->unacknowledged--;
    }
}

static void on_event(AppHandle app, AppEventSourceId source, EventMask mask, void *context)
{
    AppLogClient *client = &app->logserv;
    ErrorCode result = CORE_OK;

    (void)source;
    (void)context;
    if (client->connection == NULL) {
        return;
    }
    if (!client->open) {
        IpcConnectionState state = IPC_CONNECTION_STATE_CONNECTING;

        result = ipc_connection_finish_connect(client->connection, &state);
        if (result == CORE_OK && state == IPC_CONNECTION_STATE_OPEN) {
            result = become_open(app);
        } else if (result == CORE_OK && state != IPC_CONNECTION_STATE_CONNECTING) {
            result = CORE_IO_ERROR;
        }
    } else {
        if ((mask & (EVENT_READABLE | EVENT_ERROR | EVENT_HANGUP)) != 0) {
            result = read_replies(app);
        }
        if (result == CORE_OK && (mask & EVENT_WRITABLE) != 0) {
            result = flush(app);
        }
    }
    if (result != CORE_OK) {
        drop_connection(app);
    }
}

static ErrorCode connect_to_logserv(AppHandle app)
{
    AppLogClient *client = &app->logserv;
    IpcConnectionState state = IPC_CONNECTION_STATE_CONNECTING;
    String endpoint = { 0 };
    i32 fd = -1;
    ErrorCode result = string_from_c_str(libappframework_internal_logserv_endpoint, &endpoint);

    if (result != CORE_OK) {
        return result;
    }
    result = ipc_connection_begin_connect(&endpoint, &client->connection);
    (void)string_destroy(&endpoint);
    if (result != CORE_OK) {
        client->connection = NULL;
        return result;
    }
    result = ipc_connection_get_fd(client->connection, &fd);
    if (result == CORE_OK) {
        result = libappframework_internal_register_fd(app, fd, EVENT_WRITABLE, on_event, NULL,
                                                      true, &client->source);
    }
    if (result == CORE_OK) {
        result = ipc_connection_finish_connect(client->connection, &state);
    }
    if (result == CORE_OK && state == IPC_CONNECTION_STATE_OPEN) {
        result = become_open(app);
    }
    if (result != CORE_OK) {
        drop_connection(app);
    }
    return result;
}

static ErrorCode enqueue(AppHandle app, MemoryBlock *payload)
{
    AppLogClient *client = &app->logserv;
    ErrorCode result;

    if (client->queued == client->queue_capacity) {
        usize capacity = client->queue_capacity == 0 ? QUEUE_INITIAL_CAPACITY
                                                      : client->queue_capacity * 2;
        usize bytes;

        result = core_size_multiply(capacity, sizeof(MemoryBlock), &bytes);
        if (result == CORE_OK) {
            result = memory_reallocate(&client->queue_block, bytes);
        }
        if (result != CORE_OK) {
            return result;
        }
        client->queue = (MemoryBlock *)client->queue_block.data;
        client->queue_capacity = capacity;
    }
    client->queue[client->queued++] = *payload;
    return CORE_OK;
}

/* ---- submitting ------------------------------------------------------------ */

static ErrorCode submit(AppHandle app, AppLogLevel level, const char *message,
                        usize message_length)
{
    AppLogClient *client = &app->logserv;
    MemoryBlock payload;
    ErrorCode result;

    if (pending_records(client) >= APP_LOG_MAX_PENDING) {
        return CORE_WOULD_BLOCK;
    }
    result = libappframework_internal_log_encode(app, level, message, message_length,
                                                 getpid(), gettid(), &payload);
    if (result != CORE_OK) {
        return result;
    }
    if (client->connection == NULL) {
        result = connect_to_logserv(app);
        if (result != CORE_OK) {
            (void)memory_free(&payload);
            return result;
        }
    }
    if (client->open) {
        result = send_payload(app, &payload);
        (void)memory_free(&payload); /* ipclib copied it */
        if (result != CORE_OK) {
            return result; /* this record is refused; the connection is fine */
        }
        result = flush(app);
        if (result != CORE_OK) {
            drop_connection(app); /* a socket error: everything unconfirmed is lost */
        }
        return result;
    }
    result = enqueue(app, &payload);
    if (result != CORE_OK) {
        (void)memory_free(&payload);
    }
    return result;
}

ErrorCode app_log(AppHandle app, AppLogLevel level, const char *message)
{
    usize length;
    ErrorCode result = libappframework_internal_enter(app, APP_OPERATION_LOG);

    if (result != CORE_OK) {
        return result;
    }
    if ((u32)level > (u32)LOG_LEVEL_LAST) {
        return CORE_INVALID_ARGUMENT;
    }
    result = libappframework_internal_text_check(message, 0, APP_LOG_MESSAGE_MAX_LENGTH,
                                                 &length);
    if (result != CORE_OK) {
        return result;
    }
    return submit(app, level, message, length);
}

ErrorCode libappframework_internal_log(AppHandle app, AppLogLevel level, const char *message)
{
    usize length = 0;

    while (message[length] != '\0') {
        ++length; /* the library's own texts are short literals */
    }
    return submit(app, level, message, length);
}

/* One non-blocking attempt: finish the connection if it can be, hand over what
 * is queued, then let go. Delivery is not guaranteed. */
void libappframework_internal_logging_shutdown(AppHandle app)
{
    AppLogClient *client = &app->logserv;

    if (client->connection != NULL && !client->open) {
        IpcConnectionState state = IPC_CONNECTION_STATE_CONNECTING;

        if (ipc_connection_finish_connect(client->connection, &state) == CORE_OK &&
            state == IPC_CONNECTION_STATE_OPEN) {
            (void)become_open(app);
        }
    }
    if (client->connection != NULL && client->open) {
        bool pending = false;

        (void)ipc_connection_flush(client->connection, &pending);
    }
    drop_connection(app);
}
