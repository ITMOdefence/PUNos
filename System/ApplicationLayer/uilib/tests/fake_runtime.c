#include "fake_runtime.h"

#define FAKE_CONNECTIONS 2
#define FAKE_SENT_MAX 64
#define FAKE_PAYLOAD_MAX 2048

typedef struct FakeSent {
    IpcMessageType type;
    usize size;
    bool descriptor;
    u8 payload[FAKE_PAYLOAD_MAX];
} FakeSent;

struct IpcConnectionHandle {
    bool open;
    FakeSent sent[FAKE_SENT_MAX];
    usize sent_count;
    IpcMessage incoming;
    u8 incoming_payload[FAKE_PAYLOAD_MAX];
    bool has_incoming;
};

typedef struct FakeSource {
    AppEventSourceId id;
    AppEventHandler handler;
    void *context;
    bool active;
} FakeSource;

static struct IpcConnectionHandle connections[FAKE_CONNECTIONS];
static usize connection_count;
static FakeSource sources[8];
static usize source_count;
static AppEventSourceId next_source = 1;

static void copy_bytes(u8 *to, const u8 *from, usize size)
{
    for (usize index = 0; index < size; ++index) to[index] = from[index];
}

ErrorCode app_event_add_fd(AppHandle app, i32 fd, EventMask mask,
                           AppEventHandler handler, void *context,
                           AppEventSourceId *out_source)
{
    (void)app; (void)fd; (void)mask;
    if (handler == NULL || out_source == NULL || source_count >= 8)
        return CORE_INVALID_ARGUMENT;
    sources[source_count] = (FakeSource){ ++next_source, handler, context, true };
    *out_source = sources[source_count++].id;
    return CORE_OK;
}

ErrorCode app_event_create_timer(AppHandle app, DurationMs delay, DurationMs period,
                                 AppEventHandler handler, void *context,
                                 AppEventSourceId *out_source)
{
    (void)delay; (void)period;
    return app_event_add_fd(app, 0, EVENT_READABLE, handler, context, out_source);
}

ErrorCode app_event_update_fd(AppHandle app, AppEventSourceId source, EventMask mask)
{ (void)app; (void)source; (void)mask; return CORE_OK; }

ErrorCode app_event_remove(AppHandle app, AppEventSourceId source)
{
    (void)app;
    for (usize index = 0; index < source_count; ++index) {
        if (sources[index].id == source && sources[index].active) {
            sources[index].active = false;
            return CORE_OK;
        }
    }
    return CORE_NOT_FOUND;
}

ErrorCode ipc_connection_begin_connect(const String *endpoint,
                                       IpcConnectionHandle *out_connection)
{
    (void)endpoint;
    if (out_connection == NULL || connection_count >= FAKE_CONNECTIONS)
        return CORE_INVALID_ARGUMENT;
    *out_connection = &connections[connection_count++];
    return CORE_OK;
}

ErrorCode ipc_connection_get_fd(IpcConnectionHandle connection, i32 *out_fd)
{ if (!connection || !out_fd) return CORE_INVALID_ARGUMENT; *out_fd = (i32)connection_count; return CORE_OK; }

ErrorCode ipc_connection_finish_connect(IpcConnectionHandle connection,
                                        IpcConnectionState *out_state)
{
    if (!connection || !out_state) return CORE_INVALID_ARGUMENT;
    connection->open = true; *out_state = IPC_CONNECTION_STATE_OPEN; return CORE_OK;
}

static ErrorCode send_fake(IpcConnectionHandle connection, const IpcMessage *message,
                           bool descriptor)
{
    FakeSent *sent;
    if (!connection || !connection->open || !message ||
        connection->sent_count >= FAKE_SENT_MAX ||
        message->payload.size > FAKE_PAYLOAD_MAX) return CORE_INVALID_ARGUMENT;
    sent = &connection->sent[connection->sent_count++];
    sent->type = message->type; sent->size = message->payload.size;
    sent->descriptor = descriptor;
    copy_bytes(sent->payload, message->payload.data, message->payload.size);
    return CORE_OK;
}

ErrorCode ipc_connection_send(IpcConnectionHandle connection,
                              const IpcMessage *message)
{ return send_fake(connection, message, false); }

ErrorCode ipc_connection_send_with_descriptor(IpcConnectionHandle connection,
                                              const IpcMessage *message, i32 descriptor)
{ return descriptor < 0 ? CORE_INVALID_ARGUMENT : send_fake(connection, message, true); }

ErrorCode ipc_connection_flush(IpcConnectionHandle connection, bool *out_pending)
{ if (!connection || !out_pending) return CORE_INVALID_ARGUMENT; *out_pending = false; return CORE_OK; }

ErrorCode ipc_connection_receive_with_descriptor(IpcConnectionHandle connection,
    IpcReceiveStatus *out_status, IpcMessage *out_message, i32 *out_descriptor)
{
    if (!connection || !out_status || !out_message || !out_descriptor)
        return CORE_INVALID_ARGUMENT;
    *out_descriptor = IPC_NO_DESCRIPTOR;
    if (!connection->has_incoming) { *out_status = IPC_RECEIVE_STATUS_NO_MESSAGE; return CORE_OK; }
    *out_message = connection->incoming;
    connection->has_incoming = false;
    *out_status = IPC_RECEIVE_STATUS_MESSAGE;
    return CORE_OK;
}

ErrorCode ipc_message_destroy(IpcMessage *message)
{ if (!message) return CORE_INVALID_ARGUMENT; *message = (IpcMessage){ 0 }; return CORE_OK; }

ErrorCode ipc_connection_close(IpcConnectionHandle *connection)
{ if (!connection || !*connection) return CORE_INVALID_ARGUMENT; (*connection)->open=false; *connection=NULL; return CORE_OK; }

void fake_connect_clients(void)
{
    for (usize index = 0; index < source_count; ++index)
        if (sources[index].active && sources[index].id != 2)
            sources[index].handler(NULL, sources[index].id, EVENT_WRITABLE,
                                   sources[index].context);
}

static struct IpcConnectionHandle *selected(bool compositor)
{ return &connections[compositor ? 0 : 1]; }

usize fake_sent_count(bool compositor, IpcMessageType type)
{
    struct IpcConnectionHandle *connection = selected(compositor); usize count=0;
    for (usize index=0; index<connection->sent_count; ++index)
        if (connection->sent[index].type==type) ++count;
    return count;
}

u64 fake_last_request(bool compositor, IpcMessageType type)
{
    struct IpcConnectionHandle *connection=selected(compositor);
    for (usize index=connection->sent_count; index>0; --index) {
        FakeSent *sent=&connection->sent[index-1]; u64 value=0;
        if (sent->type!=type || sent->size<10) continue;
        for (usize byte=0; byte<8; ++byte) value|=(u64)sent->payload[2+byte]<<(byte*8);
        return value;
    }
    return 0;
}

bool fake_last_had_descriptor(bool compositor, IpcMessageType type)
{
    struct IpcConnectionHandle *connection=selected(compositor);
    for (usize index=connection->sent_count; index>0; --index)
        if (connection->sent[index-1].type==type) return connection->sent[index-1].descriptor;
    return false;
}

void fake_deliver(bool compositor, const IpcMessage *message)
{
    struct IpcConnectionHandle *connection=selected(compositor);
    connection->incoming.type=message->type;
    connection->incoming.payload=(Bytes){connection->incoming_payload,message->payload.size};
    copy_bytes(connection->incoming_payload,message->payload.data,message->payload.size);
    connection->has_incoming=true;
    for (usize index=0; index<source_count; ++index) {
        if (sources[index].active && sources[index].context == (void *)(compositor ? sources[1].context : sources[2].context)) {
            sources[index].handler(NULL,sources[index].id,EVENT_READABLE,sources[index].context);
            return;
        }
    }
}
