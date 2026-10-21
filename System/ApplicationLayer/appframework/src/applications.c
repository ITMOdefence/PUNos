#include "internal.h"

#include <sys/string.h>

#define APPSERV_PROTOCOL_VERSION APP_PROTOCOL_VERSION
#define APPSERV_LIST_MAX 64
#define ERROR_CODE_SIZE 4
#define STATUS_RESULT_SIZE 10
#define LAUNCH_RESULT_SIZE 12
#define LAST_ERROR_CODE CORE_OVERFLOW

const char *libappframework_internal_appserv_endpoint = "/run/punos/appserv.sock";

static IpcMessageType request_type(AppAppsOperation operation)
{
    return (IpcMessageType)(2 * (u32)operation + 1);
}

/* ---- wire format ------------------------------------------------------------ */

static void put_u16(u8 *out, u16 value)
{
    out[0] = (u8)(value & 0xff);
    out[1] = (u8)(value >> 8);
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

ErrorCode libappframework_internal_apps_encode(AppAppsOperation operation, const char *id,
                                               usize id_length, AppInstanceId instance,
                                               MemoryBlock *out_payload,
                                               IpcMessageType *out_type)
{
    MemoryBlock block = { NULL, 0 };
    ErrorCode result;
    usize size;

    switch (operation) {
    case APP_APPS_OPERATION_LIST:
        size = 0;
        break;
    case APP_APPS_OPERATION_GET_INFO:
    case APP_APPS_OPERATION_LAUNCH:
        size = 4 + id_length;
        break;
    case APP_APPS_OPERATION_STOP:
    case APP_APPS_OPERATION_GET_STATUS:
        size = 2 + 8;
        break;
    default:
        return CORE_INVALID_ARGUMENT;
    }
    result = memory_allocate(size, &block);
    if (result != CORE_OK) {
        return result;
    }
    if (operation == APP_APPS_OPERATION_GET_INFO || operation == APP_APPS_OPERATION_LAUNCH) {
        put_u16(block.data, APPSERV_PROTOCOL_VERSION);
        put_u16(block.data + 2, (u16)id_length);
        for (usize i = 0; i < id_length; ++i) {
            block.data[4 + i] = (u8)id[i];
        }
    } else if (operation != APP_APPS_OPERATION_LIST) {
        put_u16(block.data, APPSERV_PROTOCOL_VERSION);
        for (u32 i = 0; i < 8; ++i) {
            block.data[2 + i] = (u8)((instance >> (8 * i)) & 0xff);
        }
    }
    *out_payload = block;
    *out_type = request_type(operation);
    return CORE_OK;
}

/* A length-prefixed text field (u16 length, bytes): at most 64 bytes, UTF-8. */
typedef struct Field {
    const u8 *data;
    usize length;
} Field;

static bool read_field(const u8 *data, usize size, usize *offset, bool allow_empty, Field *out)
{
    usize length;

    if (size - *offset < 2) {
        return false;
    }
    length = get_u16(data + *offset);
    *offset += 2;
    if (length > APP_ID_MAX_LENGTH || length > size - *offset || (length == 0 && !allow_empty)) {
        return false;
    }
    if (!libappframework_internal_utf8_valid(data + *offset, length)) {
        return false;
    }
    out->data = data + *offset;
    out->length = length;
    *offset += length;
    return true;
}

/* id and name of one AppInfo; the display name may be empty. */
static bool read_info(const u8 *data, usize size, usize *offset, Field *id, Field *name)
{
    return read_field(data, size, offset, false, id) &&
           read_field(data, size, offset, true, name);
}

static char *copy_text(char *destination, const Field *field)
{
    for (usize i = 0; i < field->length; ++i) {
        destination[i] = (char)field->data[i];
    }
    destination[field->length] = '\0';
    return destination;
}

/* The copies of count AppInfo entries start at `first`; the strings follow the
 * array in the same block. */
static ErrorCode decode_infos(const u8 *data, usize size, usize offset, usize count,
                              AppDecoded *out, AppInfo **out_items)
{
    usize text_bytes = 0;
    usize scan = offset;
    AppInfo *items;
    char *text;
    ErrorCode result;

    for (usize i = 0; i < count; ++i) {
        Field id;
        Field name;

        if (!read_info(data, size, &scan, &id, &name)) {
            return CORE_IO_ERROR;
        }
        text_bytes += id.length + 1 + name.length + 1;
    }
    if (scan != size) {
        return CORE_IO_ERROR; /* trailing bytes */
    }
    result = memory_allocate(count * sizeof(AppInfo) + text_bytes, &out->storage);
    if (result != CORE_OK) {
        return result;
    }
    items = (AppInfo *)out->storage.data;
    text = (char *)out->storage.data + count * sizeof(AppInfo);
    scan = offset;
    for (usize i = 0; i < count; ++i) {
        Field id;
        Field name;

        (void)read_info(data, size, &scan, &id, &name);
        items[i].application_id = copy_text(text, &id);
        text += id.length + 1;
        items[i].display_name = copy_text(text, &name);
        text += name.length + 1;
    }
    *out_items = items;
    return CORE_OK;
}

static ErrorCode decode_success(AppAppsOperation operation, const u8 *data, usize size,
                                AppDecoded *out)
{
    AppInfo *items = NULL;
    ErrorCode result;

    out->result.operation = operation;
    switch (operation) {
    case APP_APPS_OPERATION_LIST: {
        usize count;

        if (size < ERROR_CODE_SIZE + 2) {
            return CORE_IO_ERROR;
        }
        count = get_u16(data + ERROR_CODE_SIZE);
        if (count > APPSERV_LIST_MAX) {
            return CORE_IO_ERROR;
        }
        result = decode_infos(data, size, ERROR_CODE_SIZE + 2, count, out, &items);
        if (result == CORE_OK) {
            out->result.list.items = items;
            out->result.list.count = count;
        }
        return result;
    }
    case APP_APPS_OPERATION_GET_INFO:
        result = decode_infos(data, size, ERROR_CODE_SIZE, 1, out, &items);
        if (result == CORE_OK) {
            out->result.info = items[0];
        }
        return result;
    case APP_APPS_OPERATION_LAUNCH:
        if (size != LAUNCH_RESULT_SIZE) {
            return CORE_IO_ERROR;
        }
        out->result.instance = get_u64(data + ERROR_CODE_SIZE);
        return out->result.instance == 0 ? CORE_IO_ERROR : CORE_OK;
    case APP_APPS_OPERATION_STOP:
        return size == ERROR_CODE_SIZE ? CORE_OK : CORE_IO_ERROR;
    case APP_APPS_OPERATION_GET_STATUS: {
        u8 state;
        u8 kind;

        if (size != STATUS_RESULT_SIZE) {
            return CORE_IO_ERROR;
        }
        state = data[4];
        kind = data[5];
        if (state != APP_INSTANCE_STATE_RUNNING && state != APP_INSTANCE_STATE_TERMINATING &&
            state != APP_INSTANCE_STATE_EXITED) {
            return CORE_IO_ERROR; /* appserv never reports Starting */
        }
        out->result.status.state = (AppInstanceState)state;
        out->result.status.exit_kind = APP_EXIT_KIND_EXITED;
        out->result.status.exit_value = 0;
        if (state == APP_INSTANCE_STATE_EXITED) {
            if (kind > APP_EXIT_KIND_SIGNALED) {
                return CORE_IO_ERROR;
            }
            out->result.status.exit_kind = (AppExitKind)kind;
            out->result.status.exit_value = (i32)get_u32(data + 6);
        }
        return CORE_OK;
    }
    }
    return CORE_INVALID_ARGUMENT;
}

ErrorCode libappframework_internal_apps_decode(AppAppsOperation operation, const u8 *data,
                                               usize size, AppDecoded *out)
{
    u32 code;
    ErrorCode result;

    out->service_error = CORE_OK;
    out->storage.data = NULL;
    out->storage.size = 0;
    out->result.operation = operation;
    if (size < ERROR_CODE_SIZE) {
        return CORE_IO_ERROR;
    }
    code = get_u32(data);
    if (code > (u32)LAST_ERROR_CODE) {
        return CORE_IO_ERROR;
    }
    if (code != CORE_OK) {
        out->service_error = (ErrorCode)code; /* bytes after the code are ignored */
        return CORE_OK;
    }
    result = decode_success(operation, data, size, out);
    if (result != CORE_OK) {
        libappframework_internal_apps_decoded_release(out);
    }
    return result;
}

void libappframework_internal_apps_decoded_release(AppDecoded *decoded)
{
    (void)memory_free(&decoded->storage);
}

/* ---- the queue ------------------------------------------------------------------- */

static AppRequest *request_at(AppAppsClient *client, usize index)
{
    return &client->requests[(client->head + index) % APP_APPS_MAX_PENDING];
}

/* The slots of requests whose callbacks are still due stay reserved. */
static bool queue_full(const AppAppsClient *client)
{
    return client->count + client->ending >= APP_APPS_MAX_PENDING;
}

static ErrorCode queue_push(AppAppsClient *client, const AppRequest *request)
{
    if (queue_full(client)) {
        return CORE_WOULD_BLOCK;
    }
    if (client->requests == NULL) {
        ErrorCode result = memory_allocate(APP_APPS_MAX_PENDING * sizeof(AppRequest),
                                           &client->block);

        if (result != CORE_OK) {
            return result;
        }
        client->requests = (AppRequest *)client->block.data;
    }
    *request_at(client, client->count) = *request;
    client->count++;
    return CORE_OK;
}

static AppRequest queue_pop(AppAppsClient *client)
{
    AppRequest request = *request_at(client, 0);

    client->head = (client->head + 1) % APP_APPS_MAX_PENDING;
    client->count--;
    return request;
}

/* ---- the connection ------------------------------------------------------------------ */

static void close_connection(AppHandle app)
{
    AppAppsClient *client = &app->appserv;

    if (client->source != 0) {
        (void)libappframework_internal_unregister(app, client->source);
        client->source = 0;
    }
    if (client->connection != NULL) {
        (void)ipc_connection_close(&client->connection);
    }
    client->open = false;
    client->write_interest = false;
    client->failed = false;
}

/* Ends the first `count` requests of the queue, in order, each with one
 * callback. The whole batch leaves the queue before the first callback: a
 * callback may start new requests (and a new connection, which opens at once),
 * and those must never meet the old ones. The batch keeps its ring slots
 * (`ending`) until each of its callbacks is due, so new requests cannot
 * overwrite it and no copy is needed (thread stacks are small). */
static void finish_requests(AppHandle app, usize count, ErrorCode error)
{
    AppAppsClient *client = &app->appserv;
    usize first = client->head;
    usize taken = count < client->count ? count : client->count;

    client->head = (client->head + taken) % APP_APPS_MAX_PENDING;
    client->count -= taken;
    client->ending += taken;
    for (usize i = 0; i < taken; ++i) {
        AppRequest request = client->requests[(first + i) % APP_APPS_MAX_PENDING];

        client->ending--;
        (void)memory_free(&request.payload);
        request.callback(app, request.id, error, NULL, request.context);
    }
}

/* Closes the connection, then fails every request that was in the queue. */
static void fail_connection(AppHandle app, ErrorCode error, const char *diagnostic,
                            AppLogLevel level)
{
    usize count = app->appserv.count;

    close_connection(app);
    if (diagnostic != NULL) {
        (void)libappframework_internal_log(app, level, diagnostic);
    }
    finish_requests(app, count, error);
}

static ErrorCode set_interest(AppHandle app, bool write)
{
    AppAppsClient *client = &app->appserv;
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
    AppAppsClient *client = &app->appserv;
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

static ErrorCode send_request(AppHandle app, AppRequest *request)
{
    IpcMessage message = { request_type(request->operation),
                           { request->payload.data, request->payload.size } };
    ErrorCode result = ipc_connection_send(app->appserv.connection, &message);

    if (result == CORE_OVERFLOW) {
        return CORE_WOULD_BLOCK;
    }
    if (result == CORE_OK) {
        request->sent = true;
        (void)memory_free(&request->payload); /* ipclib copied it */
    }
    return result;
}

static ErrorCode become_open(AppHandle app)
{
    AppAppsClient *client = &app->appserv;
    ErrorCode result;

    client->open = true;
    for (usize i = 0; i < client->count; ++i) {
        result = send_request(app, request_at(client, i));
        if (result != CORE_OK) {
            return result;
        }
    }
    result = set_interest(app, false);
    return result != CORE_OK ? result : flush(app);
}

static void complete_head(AppHandle app, const AppDecoded *decoded)
{
    AppRequest request = queue_pop(&app->appserv);

    if (decoded->service_error != CORE_OK) {
        request.callback(app, request.id, decoded->service_error, NULL, request.context);
    } else {
        request.callback(app, request.id, CORE_OK, &decoded->result, request.context);
    }
}

/* Handles every complete reply. Returns CORE_IO_ERROR for a violation. */
static ErrorCode read_replies(AppHandle app, bool *out_violation)
{
    AppAppsClient *client = &app->appserv;

    for (;;) {
        IpcReceiveStatus status = IPC_RECEIVE_STATUS_NO_MESSAGE;
        IpcMessage message = { 0, { NULL, 0 } };
        AppDecoded decoded;
        ErrorCode result = ipc_connection_receive(client->connection, &status, &message);

        if (result != CORE_OK || status == IPC_RECEIVE_STATUS_PEER_CLOSED) {
            return CORE_IO_ERROR;
        }
        if (status == IPC_RECEIVE_STATUS_NO_MESSAGE) {
            return CORE_OK;
        }
        if (client->count == 0 ||
            message.type != request_type(request_at(client, 0)->operation) + 1) {
            (void)ipc_message_destroy(&message);
            *out_violation = true; /* an answer nobody asked for, or of the wrong kind */
            return CORE_IO_ERROR;
        }
        result = libappframework_internal_apps_decode(request_at(client, 0)->operation,
                                                      message.payload.data,
                                                      message.payload.size, &decoded);
        (void)ipc_message_destroy(&message);
        if (result == CORE_OUT_OF_MEMORY) {
            /* No copy of the answer: this request ends with the error, the
             * connection is fine. */
            AppRequest request = queue_pop(client);

            request.callback(app, request.id, CORE_OUT_OF_MEMORY, NULL, request.context);
            continue;
        }
        if (result != CORE_OK) {
            *out_violation = true;
            return result;
        }
        complete_head(app, &decoded);
        libappframework_internal_apps_decoded_release(&decoded);
        if (client->connection == NULL) {
            return CORE_OK; /* a callback destroyed the connection state */
        }
    }
}

/* A write failed inside a public call. The call only recorded it; the requests
 * are ended here, from the event loop (callbacks run only from app_run()). */
void libappframework_internal_apps_deferred(AppHandle app)
{
    if (app->appserv.failed) {
        fail_connection(app, CORE_IO_ERROR, "appserv connection failed", APP_LOG_LEVEL_WARNING);
    }
}

static void on_event(AppHandle app, AppEventSourceId source, EventMask mask, void *context)
{
    AppAppsClient *client = &app->appserv;
    bool violation = false;
    ErrorCode result = CORE_OK;

    (void)source;
    (void)context;
    if (client->connection == NULL) {
        return;
    }
    if (client->failed) {
        libappframework_internal_apps_deferred(app);
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
            result = read_replies(app, &violation);
        }
        if (result == CORE_OK && client->connection != NULL && (mask & EVENT_WRITABLE) != 0) {
            result = flush(app);
        }
    }
    if (result == CORE_OK) {
        return;
    }
    if (violation) {
        fail_connection(app, CORE_IO_ERROR, "appserv protocol violation", APP_LOG_LEVEL_ERROR);
    } else {
        fail_connection(app, CORE_IO_ERROR, "appserv connection failed", APP_LOG_LEVEL_WARNING);
    }
}

static ErrorCode connect_to_appserv(AppHandle app)
{
    AppAppsClient *client = &app->appserv;
    IpcConnectionState state = IPC_CONNECTION_STATE_CONNECTING;
    String endpoint = { 0 };
    i32 fd = -1;
    ErrorCode result = string_from_c_str(libappframework_internal_appserv_endpoint, &endpoint);

    if (result != CORE_OK) {
        return result;
    }
    result = ipc_connection_begin_connect(&endpoint, &client->connection);
    (void)string_destroy(&endpoint);
    if (result != CORE_OK) {
        client->connection = NULL;
        (void)libappframework_internal_log(app, APP_LOG_LEVEL_WARNING, "appserv unavailable");
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
        close_connection(app);
        (void)libappframework_internal_log(app, APP_LOG_LEVEL_WARNING, "appserv unavailable");
        return result;
    }
    return CORE_OK;
}

/* ---- requests ------------------------------------------------------------------------------ */

static ErrorCode submit(AppHandle app, AppAppsOperation operation, const char *id,
                        usize id_length, AppInstanceId instance, AppAppsCallback callback,
                        void *context, AppRequestId *out_request)
{
    AppAppsClient *client = &app->appserv;
    AppRequest request = { 0 };
    IpcMessageType type;
    ErrorCode result;

    if (queue_full(client)) {
        return CORE_WOULD_BLOCK;
    }
    result = libappframework_internal_apps_encode(operation, id, id_length, instance,
                                                  &request.payload, &type);
    if (result != CORE_OK) {
        return result;
    }
    request.operation = operation;
    request.callback = callback;
    request.context = context;
    request.id = client->last_request + 1;
    if (client->connection == NULL) {
        /* The connection is made first, so a refusal leaves nothing queued. */
        result = connect_to_appserv(app);
        if (result != CORE_OK) {
            (void)memory_free(&request.payload);
            return result;
        }
    }
    result = queue_push(client, &request);
    if (result != CORE_OK) {
        (void)memory_free(&request.payload);
        return result;
    }
    if (client->open && !client->failed) {
        AppRequest *queued = request_at(client, client->count - 1);

        result = send_request(app, queued);
        if (result == CORE_OK) {
            result = flush(app);
            if (result != CORE_OK) {
                /* A socket error. The new request was never accepted, so it
                 * gets no callback. The earlier ones are not ended here: that
                 * would run their callbacks inside this call. The event loop
                 * does it, in order. */
                client->count--;
                client->failed = true;
                (void)eventloop_wake(app->loop);
                return result;
            }
        } else {
            client->count--; /* refused by ipclib: take it back out */
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

static ErrorCode submit_by_id(AppHandle app, AppAppsOperation operation, const char *id,
                              AppAppsCallback callback, void *context,
                              AppRequestId *out_request)
{
    usize length;
    ErrorCode result = libappframework_internal_enter(app, APP_OPERATION_APPS);

    if (result != CORE_OK) {
        return result;
    }
    if (callback == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = libappframework_internal_text_check(id, 1, APP_ID_MAX_LENGTH, &length);
    if (result != CORE_OK) {
        return result;
    }
    return submit(app, operation, id, length, 0, callback, context, out_request);
}

static ErrorCode submit_by_instance(AppHandle app, AppAppsOperation operation,
                                    AppInstanceId instance, AppAppsCallback callback,
                                    void *context, AppRequestId *out_request)
{
    ErrorCode result = libappframework_internal_enter(app, APP_OPERATION_APPS);

    if (result != CORE_OK) {
        return result;
    }
    if (callback == NULL || instance == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    return submit(app, operation, NULL, 0, instance, callback, context, out_request);
}

ErrorCode app_apps_list(AppHandle app, AppAppsCallback callback, void *context,
                        AppRequestId *out_request)
{
    ErrorCode result = libappframework_internal_enter(app, APP_OPERATION_APPS);

    if (result != CORE_OK) {
        return result;
    }
    if (callback == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    return submit(app, APP_APPS_OPERATION_LIST, NULL, 0, 0, callback, context, out_request);
}

ErrorCode app_apps_get_info(AppHandle app, const char *application_id,
                            AppAppsCallback callback, void *context,
                            AppRequestId *out_request)
{
    return submit_by_id(app, APP_APPS_OPERATION_GET_INFO, application_id, callback, context,
                        out_request);
}

ErrorCode app_apps_launch(AppHandle app, const char *application_id,
                          AppAppsCallback callback, void *context,
                          AppRequestId *out_request)
{
    return submit_by_id(app, APP_APPS_OPERATION_LAUNCH, application_id, callback, context,
                        out_request);
}

ErrorCode app_apps_stop(AppHandle app, AppInstanceId instance, AppAppsCallback callback,
                        void *context, AppRequestId *out_request)
{
    return submit_by_instance(app, APP_APPS_OPERATION_STOP, instance, callback, context,
                              out_request);
}

ErrorCode app_apps_get_status(AppHandle app, AppInstanceId instance,
                              AppAppsCallback callback, void *context,
                              AppRequestId *out_request)
{
    return submit_by_instance(app, APP_APPS_OPERATION_GET_STATUS, instance, callback, context,
                              out_request);
}

/* app_destroy(): every pending request gets its callback with
 * CORE_INVALID_STATE, then the connection and the queue go. */
void libappframework_internal_apps_shutdown(AppHandle app)
{
    AppAppsClient *client = &app->appserv;
    usize count = client->count;

    close_connection(app);
    finish_requests(app, count, CORE_INVALID_STATE);
    (void)memory_free(&client->block);
    client->requests = NULL;
    client->head = 0;
    client->count = 0;
}
