#include <msgserv.h>
#include <sys/eventloop.h>
#include <punos/syscall/syscall.h>

#include "wire.h"

#define MSGSERV_ENDPOINT "/run/punos/msgserv.sock"
#define PROBE_TIMEOUT_MS ((DurationMs)20000)

typedef struct ProbeConnection {
    IpcConnectionHandle connection;
    EventSourceId source;
} ProbeConnection;

static bool wait_events(EventLoopHandle loop)
{
    Event events[4];
    usize count = 0;

    return eventloop_wait(loop, events, 4, 50, &count) == CORE_OK;
}

static bool connect_to_msgserv(EventLoopHandle loop, Deadline deadline,
                               ProbeConnection *out)
{
    String endpoint = { (char *)MSGSERV_ENDPOINT, sizeof(MSGSERV_ENDPOINT) - 1 };

    for (;;) {
        bool expired = false;
        IpcConnectionHandle connection = NULL;
        EventSourceId source = 0;
        i32 fd = -1;
        bool connected = false;

        if (time_deadline_expired(deadline, &expired) != CORE_OK || expired) {
            return false;
        }
        if (ipc_connection_begin_connect(&endpoint, &connection) != CORE_OK) {
            if (!wait_events(loop)) {
                return false;
            }
            continue;
        }
        if (ipc_connection_get_fd(connection, &fd) != CORE_OK ||
            eventloop_add_fd(loop, fd, EVENT_READABLE | EVENT_WRITABLE, &source) != CORE_OK) {
            (void)ipc_connection_close(&connection);
            return false;
        }
        while (!connected) {
            IpcConnectionState state = IPC_CONNECTION_STATE_CONNECTING;

            if (time_deadline_expired(deadline, &expired) != CORE_OK || expired) {
                break;
            }
            if (!wait_events(loop)) {
                break;
            }
            if (ipc_connection_finish_connect(connection, &state) != CORE_OK) {
                break;
            }
            if (state == IPC_CONNECTION_STATE_OPEN) {
                connected = true;
            } else if (state == IPC_CONNECTION_STATE_PEER_CLOSED) {
                break;
            }
        }
        if (connected) {
            out->connection = connection;
            out->source = source;
            return true;
        }
        (void)eventloop_remove_source(loop, source);
        (void)ipc_connection_close(&connection);
    }
}

static bool send_message(EventLoopHandle loop, ProbeConnection *client,
                         IpcMessageType type, const u8 *payload, usize size,
                         Deadline deadline)
{
    IpcMessage message = { type, { (u8 *)payload, size } };

    if (ipc_connection_send(client->connection, &message) != CORE_OK) {
        return false;
    }
    for (;;) {
        bool pending = false;
        bool expired = false;

        if (ipc_connection_flush(client->connection, &pending) != CORE_OK) {
            return false;
        }
        if (!pending) {
            return true;
        }
        if (time_deadline_expired(deadline, &expired) != CORE_OK || expired ||
            !wait_events(loop)) {
            return false;
        }
    }
}

static bool receive_message(EventLoopHandle loop, ProbeConnection *client,
                            IpcMessageType expected, IpcMessage *out,
                            Deadline deadline)
{
    for (;;) {
        IpcReceiveStatus status = IPC_RECEIVE_STATUS_NO_MESSAGE;
        bool expired = false;

        if (ipc_connection_receive(client->connection, &status, out) != CORE_OK ||
            status == IPC_RECEIVE_STATUS_PEER_CLOSED) {
            return false;
        }
        if (status == IPC_RECEIVE_STATUS_MESSAGE) {
            if (out->type == expected) {
                return true;
            }
            (void)ipc_message_destroy(out);
            return false;
        }
        if (time_deadline_expired(deadline, &expired) != CORE_OK || expired ||
            !wait_events(loop)) {
            return false;
        }
    }
}

static bool bind_client(EventLoopHandle loop, ProbeConnection *client,
                        const char *application, const char *secret,
                        u64 *out_client_id, Deadline deadline)
{
    u8 payload[192];
    usize application_length = 0;
    usize secret_length = 0;
    MsgWriter writer = msg_writer(payload, sizeof(payload));
    IpcMessage reply = { 0, { NULL, 0 } };
    MsgReader reader;
    MsgField id;
    bool success;

    while (application[application_length] != '\0') {
        ++application_length;
    }
    while (secret[secret_length] != '\0') {
        ++secret_length;
    }
    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    msg_write_text(&writer, (const u8 *)application, application_length);
    msg_write_text(&writer, (const u8 *)secret, secret_length);
    if (writer.bad || !send_message(loop, client, MSG_TYPE_BIND, payload, writer.offset,
                                    deadline) ||
        !receive_message(loop, client, MSG_TYPE_BIND_RESULT, &reply, deadline)) {
        return false;
    }
    reader = msg_reader(reply.payload.data, reply.payload.size);
    success = msg_read_u32(&reader) == (u32)CORE_OK;
    id = msg_read_text(&reader, 1, MSG_APPLICATION_ID_MAX_LENGTH);
    *out_client_id = msg_read_u64(&reader);
    success = success && msg_bytes_equal(id.data, id.length,
                                         (const u8 *)application, application_length) &&
              *out_client_id != 0 && msg_reader_done(&reader);
    (void)ipc_message_destroy(&reply);
    return success;
}

static bool subscribe(EventLoopHandle loop, ProbeConnection *client, Deadline deadline)
{
    static const u8 topic[] = "qemu.smoke";
    u8 payload[32];
    MsgWriter writer = msg_writer(payload, sizeof(payload));
    IpcMessage reply = { 0, { NULL, 0 } };
    MsgReader reader;
    bool success;

    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    msg_write_text(&writer, topic, sizeof(topic) - 1);
    if (writer.bad || !send_message(loop, client, MSG_TYPE_SUBSCRIBE,
                                    payload, writer.offset, deadline) ||
        !receive_message(loop, client, MSG_TYPE_SUBSCRIBE_RESULT, &reply, deadline)) {
        return false;
    }
    reader = msg_reader(reply.payload.data, reply.payload.size);
    success = msg_read_u32(&reader) == (u32)CORE_OK && msg_reader_done(&reader);
    (void)ipc_message_destroy(&reply);
    return success;
}

static bool publish(EventLoopHandle loop, ProbeConnection *client, Deadline deadline)
{
    static const u8 topic[] = "qemu.smoke";
    static const u8 body[] = "qemu-msgserv-ok";
    u8 payload[64];
    MsgWriter writer = msg_writer(payload, sizeof(payload));
    IpcMessage reply = { 0, { NULL, 0 } };
    MsgReader reader;
    bool success;

    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    msg_write_text(&writer, topic, sizeof(topic) - 1);
    msg_write_blob(&writer, body, sizeof(body) - 1);
    if (writer.bad || !send_message(loop, client, MSG_TYPE_PUBLISH,
                                    payload, writer.offset, deadline) ||
        !receive_message(loop, client, MSG_TYPE_PUBLISH_RESULT, &reply, deadline)) {
        return false;
    }
    reader = msg_reader(reply.payload.data, reply.payload.size);
    success = msg_read_u32(&reader) == (u32)CORE_OK &&
              msg_read_u16(&reader) == 1 && msg_read_u16(&reader) == 0 &&
              msg_reader_done(&reader);
    (void)ipc_message_destroy(&reply);
    return success;
}

static bool receive_event(EventLoopHandle loop, ProbeConnection *client,
                          u64 publisher_id, Deadline deadline)
{
    static const u8 application[] = "qemu.publisher";
    static const u8 topic[] = "qemu.smoke";
    static const u8 body[] = "qemu-msgserv-ok";
    IpcMessage event = { 0, { NULL, 0 } };
    MsgReader reader;
    MsgField sender;
    MsgField event_topic;
    MsgField payload;
    bool success;

    if (!receive_message(loop, client, MSG_TYPE_EVENT, &event, deadline)) {
        return false;
    }
    reader = msg_reader(event.payload.data, event.payload.size);
    success = msg_read_u64(&reader) == publisher_id;
    sender = msg_read_text(&reader, 1, MSG_APPLICATION_ID_MAX_LENGTH);
    event_topic = msg_read_text(&reader, 1, MSG_TOPIC_MAX_LENGTH);
    success = success && msg_bytes_equal(sender.data, sender.length,
                                         application, sizeof(application) - 1) &&
              msg_bytes_equal(event_topic.data, event_topic.length,
                              topic, sizeof(topic) - 1) &&
              msg_read_u64(&reader) == 1;
    payload = msg_read_blob(&reader, MSG_PAYLOAD_MAX_SIZE);
    success = success && msg_bytes_equal(payload.data, payload.length,
                                         body, sizeof(body) - 1) &&
              msg_reader_done(&reader);
    (void)ipc_message_destroy(&event);
    return success;
}

static void close_client(EventLoopHandle loop, ProbeConnection *client)
{
    if (client->source != 0) {
        (void)eventloop_remove_source(loop, client->source);
    }
    if (client->connection != NULL) {
        (void)ipc_connection_close(&client->connection);
    }
}

int main(int argc, char **argv, char **envp)
{
    static const char publisher_secret[] =
        "32929491f9eaa2a9432302a5c10c12a3ab6ddafe6af6fcd912d3987c02e20af2";
    static const char subscriber_secret[] =
        "bc73d729842f8955ff336437b6d5dd8578f39a6545464c0db801a95f63e2c985";
    EventLoopHandle loop = NULL;
    ProbeConnection publisher = { NULL, 0 };
    ProbeConnection subscriber = { NULL, 0 };
    Deadline deadline = 0;
    u64 publisher_id = 0;
    bool success = false;

    (void)argc;
    (void)argv;
    (void)envp;
    if (eventloop_create(&loop) != CORE_OK ||
        time_deadline_after(PROBE_TIMEOUT_MS, &deadline) != CORE_OK) {
        return 1;
    }
    /* appserv was already confirmed ready by services-bootcheck; keep waiting
     * here until initserv has started msgserv and its endpoint accepts clients. */
    if (!connect_to_msgserv(loop, deadline, &subscriber) ||
        !connect_to_msgserv(loop, deadline, &publisher) ||
        !bind_client(loop, &subscriber, "qemu.subscriber", subscriber_secret,
                     &(u64){ 0 }, deadline) ||
        !bind_client(loop, &publisher, "qemu.publisher", publisher_secret,
                     &publisher_id, deadline) ||
        !subscribe(loop, &subscriber, deadline) ||
        !publish(loop, &publisher, deadline) ||
        !receive_event(loop, &subscriber, publisher_id, deadline)) {
        goto cleanup;
    }
    success = true;

cleanup:
    close_client(loop, &publisher);
    close_client(loop, &subscriber);
    if (loop != NULL) {
        (void)eventloop_close(&loop);
    }
    if (success) {
        static const char message[] = "PUNos services: msgserv publish/receive passed\n";
        (void)write(STDERR_FILENO, message, sizeof(message) - 1);
        return 0;
    }
    static const char error[] = "PUNos services: msgserv publish/receive failed\n";
    (void)write(STDERR_FILENO, error, sizeof(error) - 1);
    return 1;
}
