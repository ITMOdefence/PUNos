#include "internal.h"

#include <sys/string.h>

/*
 * The client of msgserv (appframework.md, "Протокол клиента msgserv").
 *
 * Two things make this module different from the logserv and appserv clients:
 *
 *   - it has to prove who it is before anything else may be sent, so a fresh
 *     connection sends bind on its own and the application's requests wait;
 *   - frames arrive unsolicited at any moment, including between a request of
 *     this application and the answer to it. The reply FIFO and the handlers
 *     of unsolicited frames are therefore two separate mechanisms, and the
 *     read loop decides by message type which one a frame belongs to.
 *
 * The connection is re-established with a growing backoff, because msgserv
 * starts after appserv: an application launched right after boot does not
 * find the broker on its first attempt. Subscriptions are remembered and
 * re-sent after a reconnect; the requests that were in flight are not.
 */

/* Frame types of msgserv; the broker's include/msgserv.h declares the same. */
#define MSG_TYPE_BIND 1
#define MSG_TYPE_BIND_RESULT 2
#define MSG_TYPE_SUBSCRIBE 3
#define MSG_TYPE_SUBSCRIBE_RESULT 4
#define MSG_TYPE_UNSUBSCRIBE 5
#define MSG_TYPE_UNSUBSCRIBE_RESULT 6
#define MSG_TYPE_PUBLISH 7
#define MSG_TYPE_PUBLISH_RESULT 8
#define MSG_TYPE_REQUEST 9
#define MSG_TYPE_REQUEST_RESULT 10
#define MSG_TYPE_RESPOND 11
#define MSG_TYPE_RESPOND_RESULT 12
#define MSG_TYPE_EVENT 20
#define MSG_TYPE_DELIVER_REQUEST 21
#define MSG_TYPE_DELIVER_RESPONSE 22

#define MSG_TARGET_CLIENT 0
#define MSG_TARGET_APPLICATION 1

#define ERROR_CODE_SIZE 4
#define LAST_ERROR_CODE CORE_OVERFLOW

const char *libappframework_internal_msgserv_endpoint = "/run/punos/msgserv.sock";

/* ---- wire format --------------------------------------------------------- */

static void put_u8(u8 *out, u8 value)
{
    out[0] = value;
}

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

static void put_u64(u8 *out, u64 value)
{
    for (u32 i = 0; i < 8; ++i) {
        out[i] = (u8)((value >> (8 * i)) & 0xff);
    }
}

static u16 get_u16(const u8 *in)
{
    return (u16)(in[0] | (in[1] << 8));
}

static u32 get_u32(const u8 *in)
{
    return (u32)in[0] | ((u32)in[1] << 8) | ((u32)in[2] << 16) | ((u32)in[3] << 24);
}

static u64 get_u64(const u8 *in)
{
    return (u64)get_u32(in) | ((u64)get_u32(in + 4) << 32);
}

static void copy_in(u8 *out, const u8 *in, usize size)
{
    for (usize i = 0; i < size; ++i) {
        out[i] = in[i];
    }
}

static IpcMessageType request_type(AppMsgOperation operation)
{
    switch (operation) {
    case APP_MSG_OPERATION_SUBSCRIBE:
        return MSG_TYPE_SUBSCRIBE;
    case APP_MSG_OPERATION_UNSUBSCRIBE:
        return MSG_TYPE_UNSUBSCRIBE;
    case APP_MSG_OPERATION_PUBLISH:
        return MSG_TYPE_PUBLISH;
    case APP_MSG_OPERATION_REQUEST:
        return MSG_TYPE_REQUEST;
    case APP_MSG_OPERATION_RESPOND:
        return MSG_TYPE_RESPOND;
    }
    return 0;
}

ErrorCode libappframework_internal_msg_encode(AppMsgOperation operation, const char *text,
                                              usize text_length, const char *application_id,
                                              usize application_id_length,
                                              AppMsgClientId target,
                                              AppMsgCorrelationId correlation, u32 status,
                                              Bytes payload, MemoryBlock *out_payload,
                                              IpcMessageType *out_type)
{
    MemoryBlock block = { NULL, 0 };
    usize size = 2; /* protocol_version */
    usize offset;
    ErrorCode result;

    switch (operation) {
    case APP_MSG_OPERATION_SUBSCRIBE:
    case APP_MSG_OPERATION_UNSUBSCRIBE:
        size += 2 + text_length;
        break;
    case APP_MSG_OPERATION_PUBLISH:
        size += 2 + text_length + 4 + payload.size;
        break;
    case APP_MSG_OPERATION_REQUEST:
        size += 1;
        size += application_id == NULL ? 8 : 2 + application_id_length;
        size += 2 + text_length + 4 + payload.size;
        break;
    case APP_MSG_OPERATION_RESPOND:
        size += 8 + 4 + 4 + payload.size;
        break;
    default:
        return CORE_INVALID_ARGUMENT;
    }
    result = memory_allocate(size, &block);
    if (result != CORE_OK) {
        return result;
    }
    put_u16(block.data, APP_PROTOCOL_VERSION);
    offset = 2;
    if (operation == APP_MSG_OPERATION_RESPOND) {
        put_u64(block.data + offset, correlation);
        offset += 8;
        put_u32(block.data + offset, status);
        offset += 4;
    } else {
        if (operation == APP_MSG_OPERATION_REQUEST) {
            put_u8(block.data + offset,
                   application_id == NULL ? MSG_TARGET_CLIENT : MSG_TARGET_APPLICATION);
            offset += 1;
            if (application_id == NULL) {
                put_u64(block.data + offset, target);
                offset += 8;
            } else {
                put_u16(block.data + offset, (u16)application_id_length);
                offset += 2;
                copy_in(block.data + offset, (const u8 *)application_id,
                        application_id_length);
                offset += application_id_length;
            }
        }
        put_u16(block.data + offset, (u16)text_length);
        offset += 2;
        copy_in(block.data + offset, (const u8 *)text, text_length);
        offset += text_length;
    }
    if (operation == APP_MSG_OPERATION_PUBLISH || operation == APP_MSG_OPERATION_REQUEST ||
        operation == APP_MSG_OPERATION_RESPOND) {
        put_u32(block.data + offset, (u32)payload.size);
        offset += 4;
        copy_in(block.data + offset, payload.data, payload.size);
        offset += payload.size;
    }
    *out_payload = block;
    *out_type = request_type(operation);
    return CORE_OK;
}

/* ---- environment --------------------------------------------------------- */

const char *libappframework_internal_env_lookup(char **envp, const char *name,
                                                usize *out_length)
{
    usize name_length = 0;

    if (envp == NULL || name == NULL) {
        return NULL;
    }
    while (name[name_length] != '\0') {
        name_length++;
    }
    for (usize index = 0; envp[index] != NULL; ++index) {
        const char *entry = envp[index];
        usize i = 0;

        while (i < name_length && entry[i] == name[i]) {
            i++;
        }
        if (i != name_length || entry[i] != '=') {
            continue;
        }
        if (out_length != NULL) {
            usize length = 0;

            while (entry[name_length + 1 + length] != '\0') {
                length++;
            }
            *out_length = length;
        }
        return entry + name_length + 1;
    }
    return NULL;
}

/* ---- reading a frame ----------------------------------------------------- */

/* A length-prefixed text field inside a frame the broker sent. */
typedef struct Field {
    const u8 *data;
    usize length;
} Field;

typedef struct Cursor {
    const u8 *data;
    usize size;
    usize offset;
    bool bad;
} Cursor;

static Cursor cursor_of(const u8 *data, usize size)
{
    Cursor cursor = { data, size, 0, false };

    return cursor;
}

static u64 take_fixed(Cursor *cursor, usize width)
{
    u64 value = 0;

    if (cursor->bad || cursor->size - cursor->offset < width) {
        cursor->bad = true;
        return 0;
    }
    value = width == 2 ? get_u16(cursor->data + cursor->offset)
                       : (width == 4 ? get_u32(cursor->data + cursor->offset)
                                     : get_u64(cursor->data + cursor->offset));
    cursor->offset += width;
    return value;
}

static Field take_text(Cursor *cursor, usize max_length)
{
    Field field = { NULL, 0 };
    usize length = (usize)take_fixed(cursor, 2);

    if (cursor->bad || length == 0 || length > max_length ||
        length > cursor->size - cursor->offset ||
        !libappframework_internal_utf8_valid(cursor->data + cursor->offset, length)) {
        cursor->bad = true;
        return field;
    }
    field.data = cursor->data + cursor->offset;
    field.length = length;
    cursor->offset += length;
    return field;
}

static Field take_blob(Cursor *cursor)
{
    Field field = { NULL, 0 };
    usize length = (usize)take_fixed(cursor, 4);

    if (cursor->bad || length > APP_MSG_PAYLOAD_MAX_SIZE ||
        length > cursor->size - cursor->offset) {
        cursor->bad = true;
        return field;
    }
    field.data = cursor->data + cursor->offset;
    field.length = length;
    cursor->offset += length;
    return field;
}

static bool cursor_done(const Cursor *cursor)
{
    return !cursor->bad && cursor->offset == cursor->size;
}

/* ---- the queue ----------------------------------------------------------- */

static AppMsgRequest *request_at(AppMsgClient *client, usize index)
{
    return &client->requests[(client->head + index) % APP_MSG_MAX_PENDING];
}

static bool queue_full(const AppMsgClient *client)
{
    return client->count + client->ending >= APP_MSG_MAX_PENDING;
}

static ErrorCode queue_push(AppMsgClient *client, const AppMsgRequest *request)
{
    if (queue_full(client)) {
        return CORE_WOULD_BLOCK;
    }
    if (client->requests == NULL) {
        ErrorCode result = memory_allocate(APP_MSG_MAX_PENDING * sizeof(AppMsgRequest),
                                           &client->block);

        if (result != CORE_OK) {
            return result;
        }
        client->requests = (AppMsgRequest *)client->block.data;
    }
    *request_at(client, client->count) = *request;
    client->count++;
    return CORE_OK;
}

static AppMsgRequest queue_pop(AppMsgClient *client)
{
    AppMsgRequest request = *request_at(client, 0);

    client->head = (client->head + 1) % APP_MSG_MAX_PENDING;
    client->count--;
    return request;
}

/* Ends the first count requests, in order, each with one callback. Same rule
 * as the appserv client: the whole batch leaves the queue before the first
 * callback runs, and keeps its ring slots until its callbacks are due. */
static void finish_requests(AppHandle app, usize count, ErrorCode error)
{
    AppMsgClient *client = &app->msgserv;
    usize first = client->head;
    usize taken = count < client->count ? count : client->count;

    client->head = (client->head + taken) % APP_MSG_MAX_PENDING;
    client->count -= taken;
    client->ending += taken;
    for (usize i = 0; i < taken; ++i) {
        AppMsgRequest request = client->requests[(first + i) % APP_MSG_MAX_PENDING];

        client->ending--;
        (void)memory_free(&request.payload);
        if (request.callback != NULL) {
            request.callback(app, request.id, error, NULL, request.context);
        }
    }
}

/* ---- subscriptions ------------------------------------------------------- */

static AppMsgTopic *topic_find(AppMsgClient *client, const char *topic, usize length)
{
    for (usize i = 0; i < APP_MSG_SUBSCRIPTIONS_MAX; ++i) {
        AppMsgTopic *entry = &client->topics[i];
        bool equal = entry->used && entry->length == length;

        for (usize byte = 0; equal && byte < length; ++byte) {
            equal = entry->topic[byte] == (u8)topic[byte];
        }
        if (equal) {
            return entry;
        }
    }
    return NULL;
}

static void topic_remember(AppMsgClient *client, const char *topic, usize length)
{
    if (topic_find(client, topic, length) != NULL) {
        return;
    }
    for (usize i = 0; i < APP_MSG_SUBSCRIPTIONS_MAX; ++i) {
        AppMsgTopic *entry = &client->topics[i];

        if (!entry->used) {
            copy_in(entry->topic, (const u8 *)topic, length);
            entry->length = length;
            entry->used = true;
            return;
        }
    }
}

static void topic_forget(AppMsgClient *client, const char *topic, usize length)
{
    AppMsgTopic *entry = topic_find(client, topic, length);

    if (entry != NULL) {
        entry->used = false;
    }
}

/* ---- the connection ------------------------------------------------------ */

static ErrorCode set_interest(AppHandle app, bool write)
{
    AppMsgClient *client = &app->msgserv;
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

static ErrorCode flush(AppHandle app)
{
    AppMsgClient *client = &app->msgserv;
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

static void close_connection(AppHandle app)
{
    AppMsgClient *client = &app->msgserv;

    if (client->source != 0) {
        (void)libappframework_internal_unregister(app, client->source);
        client->source = 0;
    }
    if (client->connection != NULL) {
        (void)ipc_connection_close(&client->connection);
    }
    client->open = false;
    client->bound = false;
    client->write_interest = false;
    client->failed = false;
    client->client_id = 0;
}

static ErrorCode connect_to_msgserv(AppHandle app);

static void on_retry(AppHandle app, AppEventSourceId source, EventMask mask, void *context)
{
    AppMsgClient *client = &app->msgserv;

    (void)mask;
    (void)context;
    (void)libappframework_internal_unregister(app, source);
    client->retry = 0;
    (void)connect_to_msgserv(app);
}

/* Arms the reconnect timer and doubles the delay up to the cap. A refusal that
 * cannot change (a wrong secret, an unknown application_id) is not retried:
 * the environment of a running process does not change. */
static void schedule_retry(AppHandle app)
{
    AppMsgClient *client = &app->msgserv;
    AppEventSourceId source = 0;

    if (client->retry != 0 || client->permanent || !client->started) {
        return;
    }
    /* An internal one-shot timer: the application neither sees nor removes it. */
    if (libappframework_internal_register_timer(app, client->backoff_ms, 0, on_retry, NULL,
                                               &source) != CORE_OK) {
        return; /* без таймера остаётся попытка в следующем app_msg_*() */
    }
    client->retry = source;
    client->backoff_ms = client->backoff_ms >= APP_MSG_BACKOFF_MAX_MS
                             ? APP_MSG_BACKOFF_MAX_MS
                             : client->backoff_ms * 2;
}

/* Closes the connection, ends every queued request and reports the link. */
static void fail_connection(AppHandle app, ErrorCode error, const char *diagnostic)
{
    AppMsgClient *client = &app->msgserv;
    usize count = client->count;
    AppMsgLinkHandler link = client->handlers.link;
    void *context = client->handlers.context;

    close_connection(app);
    if (diagnostic != NULL) {
        (void)libappframework_internal_log(app, APP_LOG_LEVEL_WARNING, diagnostic);
    }
    finish_requests(app, count, error);
    if (link != NULL) {
        link(app, error, 0, context);
    }
    schedule_retry(app);
}

static ErrorCode send_bind(AppHandle app)
{
    AppMsgClient *client = &app->msgserv;
    MemoryBlock block = { NULL, 0 };
    IpcMessage message;
    ErrorCode result = memory_allocate(2 + 2 + app->application_id_length + 2 +
                                           client->secret_length,
                                       &block);
    usize offset = 0;

    if (result != CORE_OK) {
        return result;
    }
    put_u16(block.data, APP_PROTOCOL_VERSION);
    offset = 2;
    put_u16(block.data + offset, (u16)app->application_id_length);
    offset += 2;
    copy_in(block.data + offset, (const u8 *)app->application_id,
            app->application_id_length);
    offset += app->application_id_length;
    put_u16(block.data + offset, (u16)client->secret_length);
    offset += 2;
    copy_in(block.data + offset, (const u8 *)client->secret_block.data,
            client->secret_length);
    offset += client->secret_length;
    message = (IpcMessage){ MSG_TYPE_BIND, { block.data, offset } };
    result = ipc_connection_send(client->connection, &message);
    (void)memory_free(&block);
    return result == CORE_OVERFLOW ? CORE_WOULD_BLOCK : result;
}

static ErrorCode send_request(AppHandle app, AppMsgRequest *request)
{
    IpcMessage message = { request_type(request->operation),
                           { request->payload.data, request->payload.size } };
    ErrorCode result = ipc_connection_send(app->msgserv.connection, &message);

    if (result == CORE_OVERFLOW) {
        return CORE_WOULD_BLOCK;
    }
    if (result == CORE_OK) {
        request->sent = true;
        (void)memory_free(&request->payload); /* ipclib copied it */
    }
    return result;
}

/* Re-sends the remembered subscriptions. They get no callbacks: the
 * application already learned their outcome the first time. */
static ErrorCode resubscribe(AppHandle app)
{
    AppMsgClient *client = &app->msgserv;

    for (usize i = 0; i < APP_MSG_SUBSCRIPTIONS_MAX; ++i) {
        AppMsgTopic *entry = &client->topics[i];
        AppMsgRequest request = { 0 };
        IpcMessageType type = 0;
        ErrorCode result;

        if (!entry->used) {
            continue;
        }
        result = libappframework_internal_msg_encode(APP_MSG_OPERATION_SUBSCRIBE,
                                                     (const char *)entry->topic,
                                                     entry->length, NULL, 0, 0, 0, 0,
                                                     (Bytes){ NULL, 0 }, &request.payload,
                                                     &type);
        if (result != CORE_OK) {
            return result;
        }
        request.operation = APP_MSG_OPERATION_SUBSCRIBE;
        request.id = ++client->last_request;
        result = queue_push(client, &request);
        if (result != CORE_OK) {
            (void)memory_free(&request.payload);
            return result;
        }
        result = send_request(app, request_at(client, client->count - 1));
        if (result != CORE_OK) {
            return result;
        }
    }
    return CORE_OK;
}

/* bind succeeded: the identity is confirmed, so everything that waited goes. */
static ErrorCode become_bound(AppHandle app, AppMsgClientId client_id)
{
    AppMsgClient *client = &app->msgserv;
    ErrorCode result;

    client->bound = true;
    client->client_id = client_id;
    client->backoff_ms = APP_MSG_BACKOFF_FIRST_MS;
    result = resubscribe(app);
    for (usize i = 0; result == CORE_OK && i < client->count; ++i) {
        AppMsgRequest *request = request_at(client, i);

        if (!request->sent) {
            result = send_request(app, request);
        }
    }
    if (result == CORE_OK) {
        result = flush(app);
    }
    if (client->handlers.link != NULL) {
        client->handlers.link(app, CORE_OK, client_id, client->handlers.context);
    }
    return result;
}

static IpcMessageType reply_type(AppMsgOperation operation)
{
    return (IpcMessageType)(request_type(operation) + 1);
}

/* ---- unsolicited frames -------------------------------------------------- */

/*
 * A NUL-terminated copy of a text field. The handlers get C strings, and the
 * frame holds none, so the copy goes on the stack of the dispatcher: the
 * fields are bounded by APP_MSG_TOPIC_MAX_LENGTH and APP_ID_MAX_LENGTH.
 */
static void terminate(char *destination, const Field *field)
{
    copy_in((u8 *)destination, field->data, field->length);
    destination[field->length] = '\0';
}

static bool dispatch_event(AppHandle app, const u8 *data, usize size)
{
    AppMsgClient *client = &app->msgserv;
    Cursor cursor = cursor_of(data, size);
    AppMsgEvent event = { { 0, NULL }, NULL, 0, { NULL, 0 } };
    char sender[APP_ID_MAX_LENGTH + 1];
    char topic[APP_MSG_TOPIC_MAX_LENGTH + 1];
    Field sender_field;
    Field topic_field;
    Field payload;

    event.sender.client = take_fixed(&cursor, 8);
    sender_field = take_text(&cursor, APP_ID_MAX_LENGTH);
    topic_field = take_text(&cursor, APP_MSG_TOPIC_MAX_LENGTH);
    event.sequence = take_fixed(&cursor, 8);
    payload = take_blob(&cursor);
    if (!cursor_done(&cursor) || event.sender.client == 0) {
        return false;
    }
    if (client->handlers.event == NULL) {
        return true; /* no handler: the frame is dropped, not an error */
    }
    terminate(sender, &sender_field);
    terminate(topic, &topic_field);
    event.sender.application_id = sender;
    event.topic = topic;
    event.payload = (Bytes){ (u8 *)payload.data, payload.length };
    client->handlers.event(app, &event, client->handlers.context);
    return true;
}

static bool dispatch_incoming(AppHandle app, const u8 *data, usize size)
{
    AppMsgClient *client = &app->msgserv;
    Cursor cursor = cursor_of(data, size);
    AppMsgIncoming incoming = { 0, { 0, NULL }, NULL, { NULL, 0 } };
    char sender[APP_ID_MAX_LENGTH + 1];
    char method[APP_MSG_METHOD_MAX_LENGTH + 1];
    Field sender_field;
    Field method_field;
    Field payload;

    incoming.correlation = take_fixed(&cursor, 8);
    incoming.sender.client = take_fixed(&cursor, 8);
    sender_field = take_text(&cursor, APP_ID_MAX_LENGTH);
    method_field = take_text(&cursor, APP_MSG_METHOD_MAX_LENGTH);
    payload = take_blob(&cursor);
    if (!cursor_done(&cursor) || incoming.correlation == 0 || incoming.sender.client == 0) {
        return false;
    }
    if (client->handlers.request == NULL) {
        return true;
    }
    terminate(sender, &sender_field);
    terminate(method, &method_field);
    incoming.sender.application_id = sender;
    incoming.method = method;
    incoming.payload = (Bytes){ (u8 *)payload.data, payload.length };
    client->handlers.request(app, &incoming, client->handlers.context);
    return true;
}

static bool dispatch_answer(AppHandle app, const u8 *data, usize size)
{
    AppMsgClient *client = &app->msgserv;
    Cursor cursor = cursor_of(data, size);
    AppMsgAnswer answer = { 0, CORE_OK, 0, { NULL, 0 } };
    u32 error;
    Field payload;

    answer.correlation = take_fixed(&cursor, 8);
    error = (u32)take_fixed(&cursor, 4);
    answer.status = (u32)take_fixed(&cursor, 4);
    payload = take_blob(&cursor);
    if (!cursor_done(&cursor) || answer.correlation == 0 || error > (u32)LAST_ERROR_CODE) {
        return false;
    }
    answer.error = (ErrorCode)error;
    if (client->handlers.response == NULL) {
        return true;
    }
    answer.payload = (Bytes){ (u8 *)payload.data, payload.length };
    client->handlers.response(app, &answer, client->handlers.context);
    return true;
}

/* ---- replies ------------------------------------------------------------- */

/* Decodes a reply to the request at the head of the FIFO. */
static bool complete_head(AppHandle app, const u8 *data, usize size)
{
    AppMsgClient *client = &app->msgserv;
    AppMsgOperation operation = request_at(client, 0)->operation;
    AppMsgResult result = { operation, 0, 0, 0 };
    Cursor cursor = cursor_of(data, size);
    u32 code;
    AppMsgRequest request;

    code = (u32)take_fixed(&cursor, 4);
    if (cursor.bad || code > (u32)LAST_ERROR_CODE) {
        return false;
    }
    if (code != CORE_OK) {
        /* bytes after the code are ignored, as in the appserv client */
        request = queue_pop(client);
        (void)memory_free(&request.payload);
        if (request.callback != NULL) {
            request.callback(app, request.id, (ErrorCode)code, NULL, request.context);
        }
        return true;
    }
    if (operation == APP_MSG_OPERATION_PUBLISH) {
        result.delivered_count = (u16)take_fixed(&cursor, 2);
        result.dropped_count = (u16)take_fixed(&cursor, 2);
    } else if (operation == APP_MSG_OPERATION_REQUEST) {
        result.correlation = take_fixed(&cursor, 8);
        if (result.correlation == 0) {
            return false;
        }
    }
    if (!cursor_done(&cursor)) {
        return false;
    }
    request = queue_pop(client);
    (void)memory_free(&request.payload);
    if (request.callback != NULL) {
        request.callback(app, request.id, CORE_OK, &result, request.context);
    }
    return true;
}

/*
 * Reads every complete frame. The decision is made by message type alone:
 * 20..22 are unsolicited and go to the handlers, anything else must be the
 * reply the head of the FIFO is waiting for. A reply nobody asked for, or of
 * the wrong kind, is a protocol violation.
 */
static ErrorCode read_frames(AppHandle app, bool *out_violation)
{
    AppMsgClient *client = &app->msgserv;

    for (;;) {
        IpcReceiveStatus status = IPC_RECEIVE_STATUS_NO_MESSAGE;
        IpcMessage message = { 0, { NULL, 0 } };
        IpcMessageType type;
        bool handled;
        ErrorCode result = ipc_connection_receive(client->connection, &status, &message);

        if (result != CORE_OK || status == IPC_RECEIVE_STATUS_PEER_CLOSED) {
            return CORE_IO_ERROR;
        }
        if (status == IPC_RECEIVE_STATUS_NO_MESSAGE) {
            return CORE_OK;
        }
        type = message.type;
        if (type == MSG_TYPE_EVENT || type == MSG_TYPE_DELIVER_REQUEST ||
            type == MSG_TYPE_DELIVER_RESPONSE) {
            handled = type == MSG_TYPE_EVENT
                          ? dispatch_event(app, message.payload.data, message.payload.size)
                          : (type == MSG_TYPE_DELIVER_REQUEST
                                 ? dispatch_incoming(app, message.payload.data,
                                                     message.payload.size)
                                 : dispatch_answer(app, message.payload.data,
                                                   message.payload.size));
            (void)ipc_message_destroy(&message);
            if (!handled) {
                *out_violation = true;
                return CORE_IO_ERROR;
            }
            if (client->connection == NULL) {
                return CORE_OK; /* a handler closed the connection state */
            }
            continue;
        }
        if (!client->bound) {
            bool ok = type == MSG_TYPE_BIND_RESULT && message.payload.size >= ERROR_CODE_SIZE;
            u32 code = ok ? get_u32(message.payload.data) : 0;

            if (!ok || code > (u32)LAST_ERROR_CODE) {
                (void)ipc_message_destroy(&message);
                *out_violation = true;
                return CORE_IO_ERROR;
            }
            if (code != CORE_OK) {
                AppMsgClientId identifier = 0;

                (void)identifier;
                (void)ipc_message_destroy(&message);
                /* A wrong secret or an unknown application_id will not change
                 * while this process lives: reconnecting would only spin. */
                client->permanent = code == (u32)CORE_PERMISSION_DENIED ||
                                    code == (u32)CORE_NOT_FOUND;
                fail_connection(app, (ErrorCode)code, "msgserv refused bind");
                return CORE_OK;
            }
            {
                Cursor cursor = cursor_of(message.payload.data, message.payload.size);
                Field confirmed;
                AppMsgClientId identifier;

                (void)take_fixed(&cursor, 4);
                confirmed = take_text(&cursor, APP_ID_MAX_LENGTH);
                identifier = take_fixed(&cursor, 8);
                if (!cursor_done(&cursor) || identifier == 0 ||
                    confirmed.length != app->application_id_length) {
                    (void)ipc_message_destroy(&message);
                    *out_violation = true;
                    return CORE_IO_ERROR;
                }
                (void)ipc_message_destroy(&message);
                result = become_bound(app, identifier);
                if (result != CORE_OK) {
                    return result;
                }
            }
            continue;
        }
        if (client->count == 0 || type != reply_type(request_at(client, 0)->operation)) {
            (void)ipc_message_destroy(&message);
            *out_violation = true; /* an answer nobody asked for, or of the wrong kind */
            return CORE_IO_ERROR;
        }
        handled = complete_head(app, message.payload.data, message.payload.size);
        (void)ipc_message_destroy(&message);
        if (!handled) {
            *out_violation = true;
            return CORE_IO_ERROR;
        }
        if (client->connection == NULL) {
            return CORE_OK;
        }
    }
}

void libappframework_internal_msg_deferred(AppHandle app)
{
    if (app->msgserv.failed) {
        fail_connection(app, CORE_IO_ERROR, "msgserv connection failed");
    }
}

static void on_event(AppHandle app, AppEventSourceId source, EventMask mask, void *context)
{
    AppMsgClient *client = &app->msgserv;
    bool violation = false;
    ErrorCode result = CORE_OK;

    (void)source;
    (void)context;
    if (client->connection == NULL) {
        return;
    }
    if (client->failed) {
        libappframework_internal_msg_deferred(app);
        return;
    }
    if (!client->open) {
        IpcConnectionState state = IPC_CONNECTION_STATE_CONNECTING;

        result = ipc_connection_finish_connect(client->connection, &state);
        if (result == CORE_OK && state == IPC_CONNECTION_STATE_OPEN) {
            client->open = true;
            result = send_bind(app);
            if (result == CORE_OK) {
                result = set_interest(app, false);
            }
            if (result == CORE_OK) {
                result = flush(app);
            }
        } else if (result == CORE_OK && state != IPC_CONNECTION_STATE_CONNECTING) {
            result = CORE_IO_ERROR;
        }
    } else {
        if ((mask & (EVENT_READABLE | EVENT_ERROR | EVENT_HANGUP)) != 0) {
            result = read_frames(app, &violation);
        }
        if (result == CORE_OK && client->connection != NULL && (mask & EVENT_WRITABLE) != 0) {
            result = flush(app);
        }
    }
    if (result == CORE_OK) {
        return;
    }
    fail_connection(app, CORE_IO_ERROR,
                    violation ? "msgserv protocol violation" : "msgserv connection failed");
}

static ErrorCode connect_to_msgserv(AppHandle app)
{
    AppMsgClient *client = &app->msgserv;
    IpcConnectionState state = IPC_CONNECTION_STATE_CONNECTING;
    String endpoint = { 0 };
    i32 fd = -1;
    ErrorCode result;

    if (client->permanent) {
        return CORE_PERMISSION_DENIED;
    }
    result = string_from_c_str(libappframework_internal_msgserv_endpoint, &endpoint);
    if (result != CORE_OK) {
        return result;
    }
    result = ipc_connection_begin_connect(&endpoint, &client->connection);
    (void)string_destroy(&endpoint);
    if (result != CORE_OK) {
        client->connection = NULL;
        (void)libappframework_internal_log(app, APP_LOG_LEVEL_WARNING, "msgserv unavailable");
        schedule_retry(app);
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
        client->open = true;
        result = send_bind(app);
        if (result == CORE_OK) {
            result = set_interest(app, false);
        }
        if (result == CORE_OK) {
            result = flush(app);
        }
    }
    if (result != CORE_OK) {
        close_connection(app);
        (void)libappframework_internal_log(app, APP_LOG_LEVEL_WARNING, "msgserv unavailable");
        schedule_retry(app);
        return result;
    }
    return CORE_OK;
}

/* ---- public operations --------------------------------------------------- */

ErrorCode app_msg_start(AppHandle app, char **envp, const AppMsgHandlers *handlers)
{
    AppMsgClient *client;
    const char *secret;
    usize secret_length = 0;
    ErrorCode result = libappframework_internal_enter(app, APP_OPERATION_MSG);

    if (result != CORE_OK) {
        return result;
    }
    if (envp == NULL || handlers == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    client = &app->msgserv;
    if (client->started) {
        return CORE_ALREADY_EXISTS;
    }
    secret = libappframework_internal_env_lookup(envp, APP_MSG_SECRET_VARIABLE,
                                                 &secret_length);
    if (secret == NULL || secret_length == 0 || secret_length > APP_MSG_SECRET_MAX_LENGTH ||
        !libappframework_internal_utf8_valid((const u8 *)secret, secret_length)) {
        /* An application without a secret cannot take part in the exchange. */
        return CORE_UNSUPPORTED;
    }
    result = memory_allocate(secret_length, &client->secret_block);
    if (result != CORE_OK) {
        return result;
    }
    copy_in(client->secret_block.data, (const u8 *)secret, secret_length);
    client->secret_length = secret_length;
    client->handlers = *handlers;
    client->backoff_ms = APP_MSG_BACKOFF_FIRST_MS;
    client->started = true;
    result = connect_to_msgserv(app);
    /* A broker that is not up yet is not an error: the retry timer is armed. */
    return result == CORE_OK || client->retry != 0 ? CORE_OK : result;
}

static ErrorCode submit(AppHandle app, AppMsgOperation operation, const char *text,
                        usize text_length, const char *application_id,
                        usize application_id_length, AppMsgClientId target,
                        AppMsgCorrelationId correlation, u32 status, Bytes payload,
                        AppMsgCallback callback, void *context, AppRequestId *out_request)
{
    AppMsgClient *client = &app->msgserv;
    AppMsgRequest request = { 0 };
    IpcMessageType type = 0;
    ErrorCode result;

    if (!client->started) {
        return CORE_INVALID_STATE;
    }
    if (queue_full(client)) {
        return CORE_WOULD_BLOCK;
    }
    result = libappframework_internal_msg_encode(operation, text, text_length, application_id,
                                                 application_id_length, target, correlation,
                                                 status, payload, &request.payload, &type);
    if (result != CORE_OK) {
        return result;
    }
    request.operation = operation;
    request.callback = callback;
    request.context = context;
    request.id = client->last_request + 1;
    if (client->connection == NULL && client->retry == 0) {
        result = connect_to_msgserv(app);
        if (result != CORE_OK && client->retry == 0) {
            (void)memory_free(&request.payload);
            return result;
        }
    }
    result = queue_push(client, &request);
    if (result != CORE_OK) {
        (void)memory_free(&request.payload);
        return result;
    }
    /* Before bind the request only waits: the broker refuses anything else. */
    if (client->bound && !client->failed) {
        AppMsgRequest *queued = request_at(client, client->count - 1);

        result = send_request(app, queued);
        if (result == CORE_OK) {
            result = flush(app);
            if (result != CORE_OK) {
                /* A socket error. The new request was never accepted, so it
                 * gets no callback; the earlier ones end in the event loop. */
                client->count--;
                client->failed = true;
                (void)eventloop_wake(app->loop);
                return result;
            }
        } else {
            client->count--;
            (void)memory_free(&queued->payload);
            return result;
        }
    }
    client->last_request = request.id;
    if (out_request != NULL) {
        *out_request = request.id;
    }
    return CORE_OK;
}

static ErrorCode submit_topic(AppHandle app, AppMsgOperation operation, const char *topic,
                              Bytes payload, AppMsgCallback callback, void *context,
                              AppRequestId *out_request)
{
    usize length = 0;
    ErrorCode result = libappframework_internal_enter(app, APP_OPERATION_MSG);

    if (result != CORE_OK) {
        return result;
    }
    result = libappframework_internal_text_check(topic, 1, APP_MSG_TOPIC_MAX_LENGTH, &length);
    if (result != CORE_OK) {
        return result;
    }
    if (payload.size > APP_MSG_PAYLOAD_MAX_SIZE ||
        (payload.data == NULL && payload.size != 0)) {
        return CORE_INVALID_ARGUMENT;
    }
    result = submit(app, operation, topic, length, NULL, 0, 0, 0, 0, payload, callback,
                    context, out_request);
    if (result == CORE_OK && operation == APP_MSG_OPERATION_SUBSCRIBE) {
        topic_remember(&app->msgserv, topic, length);
    }
    if (result == CORE_OK && operation == APP_MSG_OPERATION_UNSUBSCRIBE) {
        topic_forget(&app->msgserv, topic, length);
    }
    return result;
}

ErrorCode app_msg_subscribe(AppHandle app, const char *topic, AppMsgCallback callback,
                            void *context, AppRequestId *out_request)
{
    return submit_topic(app, APP_MSG_OPERATION_SUBSCRIBE, topic, (Bytes){ NULL, 0 }, callback,
                        context, out_request);
}

ErrorCode app_msg_unsubscribe(AppHandle app, const char *topic, AppMsgCallback callback,
                              void *context, AppRequestId *out_request)
{
    return submit_topic(app, APP_MSG_OPERATION_UNSUBSCRIBE, topic, (Bytes){ NULL, 0 },
                        callback, context, out_request);
}

ErrorCode app_msg_publish(AppHandle app, const char *topic, Bytes payload,
                          AppMsgCallback callback, void *context, AppRequestId *out_request)
{
    return submit_topic(app, APP_MSG_OPERATION_PUBLISH, topic, payload, callback, context,
                        out_request);
}

static ErrorCode submit_call(AppHandle app, const char *application_id, AppMsgClientId target,
                             const char *method, Bytes payload, AppMsgCallback callback,
                             void *context, AppRequestId *out_request)
{
    usize method_length = 0;
    usize id_length = 0;
    ErrorCode result = libappframework_internal_enter(app, APP_OPERATION_MSG);

    if (result != CORE_OK) {
        return result;
    }
    result = libappframework_internal_text_check(method, 1, APP_MSG_METHOD_MAX_LENGTH,
                                                 &method_length);
    if (result != CORE_OK) {
        return result;
    }
    if (application_id != NULL) {
        result = libappframework_internal_text_check(application_id, 1, APP_ID_MAX_LENGTH,
                                                     &id_length);
        if (result != CORE_OK) {
            return result;
        }
    } else if (target == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    if (payload.size > APP_MSG_PAYLOAD_MAX_SIZE ||
        (payload.data == NULL && payload.size != 0)) {
        return CORE_INVALID_ARGUMENT;
    }
    return submit(app, APP_MSG_OPERATION_REQUEST, method, method_length, application_id,
                  id_length, target, 0, 0, payload, callback, context, out_request);
}

ErrorCode app_msg_request(AppHandle app, AppMsgClientId target, const char *method,
                          Bytes payload, AppMsgCallback callback, void *context,
                          AppRequestId *out_request)
{
    return submit_call(app, NULL, target, method, payload, callback, context, out_request);
}

ErrorCode app_msg_request_application(AppHandle app, const char *application_id,
                                      const char *method, Bytes payload,
                                      AppMsgCallback callback, void *context,
                                      AppRequestId *out_request)
{
    if (application_id == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    return submit_call(app, application_id, 0, method, payload, callback, context,
                       out_request);
}

ErrorCode app_msg_respond(AppHandle app, AppMsgCorrelationId correlation, u32 status,
                          Bytes payload, AppMsgCallback callback, void *context,
                          AppRequestId *out_request)
{
    ErrorCode result = libappframework_internal_enter(app, APP_OPERATION_MSG);

    if (result != CORE_OK) {
        return result;
    }
    if (correlation == 0 || payload.size > APP_MSG_PAYLOAD_MAX_SIZE ||
        (payload.data == NULL && payload.size != 0)) {
        return CORE_INVALID_ARGUMENT;
    }
    return submit(app, APP_MSG_OPERATION_RESPOND, NULL, 0, NULL, 0, 0, correlation, status,
                  payload, callback, context, out_request);
}

void libappframework_internal_msg_shutdown(AppHandle app)
{
    AppMsgClient *client = &app->msgserv;
    usize count = client->count;

    client->started = false;
    client->permanent = true; /* no reconnect while the handle is going away */
    if (client->retry != 0) {
        (void)libappframework_internal_unregister(app, client->retry);
        client->retry = 0;
    }
    close_connection(app);
    finish_requests(app, count, CORE_INVALID_STATE);
    (void)memory_free(&client->block);
    (void)memory_free(&client->secret_block);
    client->requests = NULL;
    client->head = 0;
    client->count = 0;
    client->secret_length = 0;
}
