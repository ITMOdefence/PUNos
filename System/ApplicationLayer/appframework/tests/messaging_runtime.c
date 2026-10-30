/*
 * Runtime test of the msgserv client (app_msg_*) against a fake msgserv and a
 * fake logserv (tests/fake_peer.h). The event loop is run by hand with
 * test_pump(). The checks follow the contract of the module:
 *
 *   the secret comes from envp           bind before anything else
 *   requests wait until bind succeeded   subscriptions survive a reconnect
 *   unsolicited frames go to handlers,   a refused bind is final when the
 *   never to a request's callback        secret cannot change
 *   answering from a timer               app_destroy() with pending requests
 */

#define CHECK_TEST_NAME "appframework-messaging"
#include <check.h>

#include "fake_peer.h"
#include "fault.h"
#include "internal.h"

#define MSGSERV_PATH "/tmp/appframework-test-msgserv.sock"
#define LOGSERV_PATH "/tmp/appframework-test-logserv3.sock"
#define SECRET "s3cr3t-of-test-app"

static FakePeer msgserv;
static FakePeer logserv;

static void fail_at(i32 line)
{
    char digits[12];
    usize count = 0;
    u32 value = (u32)line;

    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    (void)write(STDERR_FILENO, "line ", 5);
    while (count != 0) {
        --count;
        (void)write(STDERR_FILENO, &digits[count], 1);
    }
    (void)write(STDERR_FILENO, ": ", 2);
    check_fail(1 + line % 255);
}

#define CHECK(condition)                \
    do {                                \
        if (!(condition)) {             \
            fail_at(__LINE__);          \
        }                               \
    } while (0)

/* ---- environment ---------------------------------------------------------- */

static char *test_envp[] = { (char *)"HOME=/", (char *)"PUNOS_MSG_SECRET=" SECRET,
                             (char *)"TERM=dumb", NULL };
static char *envp_without_secret[] = { (char *)"HOME=/", (char *)"PUNOS_MSG_SECRETX=x",
                                       NULL };
static char *envp_empty_secret[] = { (char *)"PUNOS_MSG_SECRET=", NULL };

/* ---- recorded callbacks and handlers -------------------------------------- */

typedef struct Record {
    usize callbacks;
    AppRequestId last_request;
    ErrorCode last_error;
    bool last_had_result;
    AppMsgOperation last_operation;
    u16 delivered;
    u16 dropped;
    AppMsgCorrelationId correlation;

    usize events;
    AppMsgClientId event_sender;
    char event_topic[APP_MSG_TOPIC_MAX_LENGTH + 1];
    char event_application[APP_ID_MAX_LENGTH + 1];
    u64 event_sequence;
    usize event_payload_size;

    usize requests;
    AppMsgCorrelationId request_correlation;
    char request_method[APP_MSG_METHOD_MAX_LENGTH + 1];

    usize answers;
    ErrorCode answer_error;
    u32 answer_status;

    usize links;
    ErrorCode link_error;
    AppMsgClientId link_client;
} Record;

static Record record;

static void copy_c(char *destination, const char *source, usize limit)
{
    usize i = 0;

    while (source != NULL && source[i] != '\0' && i < limit) {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

static void on_callback(AppHandle app, AppRequestId request, ErrorCode error,
                        const AppMsgResult *result, void *context)
{
    (void)app;
    (void)context;
    record.callbacks++;
    record.last_request = request;
    record.last_error = error;
    record.last_had_result = result != NULL;
    if (result != NULL) {
        record.last_operation = result->operation;
        record.delivered = result->delivered_count;
        record.dropped = result->dropped_count;
        record.correlation = result->correlation;
    }
}

static void on_event(AppHandle app, const AppMsgEvent *event, void *context)
{
    (void)app;
    (void)context;
    record.events++;
    record.event_sender = event->sender.client;
    copy_c(record.event_application, event->sender.application_id, APP_ID_MAX_LENGTH);
    copy_c(record.event_topic, event->topic, APP_MSG_TOPIC_MAX_LENGTH);
    record.event_sequence = event->sequence;
    record.event_payload_size = event->payload.size;
}

static void on_incoming(AppHandle app, const AppMsgIncoming *incoming, void *context)
{
    (void)app;
    (void)context;
    record.requests++;
    record.request_correlation = incoming->correlation;
    copy_c(record.request_method, incoming->method, APP_MSG_METHOD_MAX_LENGTH);
}

static void on_answer(AppHandle app, const AppMsgAnswer *answer, void *context)
{
    (void)app;
    (void)context;
    record.answers++;
    record.answer_error = answer->error;
    record.answer_status = answer->status;
}

static void on_link(AppHandle app, ErrorCode error, AppMsgClientId client, void *context)
{
    (void)app;
    (void)context;
    record.links++;
    record.link_error = error;
    record.link_client = client;
}

static AppMsgHandlers all_handlers(void)
{
    AppMsgHandlers handlers = { on_event, on_incoming, on_answer, on_link, NULL };

    return handlers;
}

static void reset(void)
{
    Record empty = { 0 };

    record = empty;
}

/* ---- frames the fake broker sends ----------------------------------------- */

static void put16(u8 *out, u16 value)
{
    out[0] = (u8)(value & 0xff);
    out[1] = (u8)(value >> 8);
}

static void put64(u8 *out, u64 value)
{
    for (u32 i = 0; i < 8; ++i) {
        out[i] = (u8)((value >> (8 * i)) & 0xff);
    }
}

static usize put_text(u8 *out, const char *text)
{
    usize length = 0;

    while (text[length] != '\0') {
        length++;
    }
    put16(out, (u16)length);
    for (usize i = 0; i < length; ++i) {
        out[2 + i] = (u8)text[i];
    }
    return 2 + length;
}

/* bind_result with CORE_OK, the confirmed application_id and a client_id. */
static bool send_bind_result(AppMsgClientId client)
{
    u8 payload[64];
    usize offset = 4;

    put_le32(payload, (u32)CORE_OK);
    offset += put_text(payload + offset, "test-app");
    put64(payload + offset, client);
    offset += 8;
    return fake_peer_send(&msgserv, 2, payload, offset);
}

static bool send_event(AppMsgClientId sender, const char *application_id, const char *topic,
                       u64 sequence, usize payload_size)
{
    u8 payload[512];
    usize offset = 0;

    put64(payload, sender);
    offset = 8;
    offset += put_text(payload + offset, application_id);
    offset += put_text(payload + offset, topic);
    put64(payload + offset, sequence);
    offset += 8;
    put_le32(payload + offset, (u32)payload_size);
    offset += 4;
    for (usize i = 0; i < payload_size; ++i) {
        payload[offset + i] = (u8)i;
    }
    return fake_peer_send(&msgserv, 20, payload, offset + payload_size);
}

static bool send_deliver_request(AppMsgCorrelationId correlation, const char *method)
{
    u8 payload[256];
    usize offset = 0;

    put64(payload, correlation);
    offset = 8;
    put64(payload + offset, 42);
    offset += 8;
    offset += put_text(payload + offset, "other-app");
    offset += put_text(payload + offset, method);
    put_le32(payload + offset, 0);
    return fake_peer_send(&msgserv, 21, payload, offset + 4);
}

static bool send_deliver_response(AppMsgCorrelationId correlation, ErrorCode error, u32 status)
{
    u8 payload[32];

    put64(payload, correlation);
    put_le32(payload + 8, (u32)error);
    put_le32(payload + 12, status);
    put_le32(payload + 16, 0);
    return fake_peer_send(&msgserv, 22, payload, 20);
}

/* Receives one frame the client sent and reports its type and payload. */
static bool take_frame(AppHandle app, IpcMessage *out)
{
    for (usize attempt = 0; attempt < 200; ++attempt) {
        test_pump(app, 2);
        (void)fake_peer_accept(&msgserv);
        if (fake_peer_receive(&msgserv, out) == 1) {
            return true;
        }
    }
    return false;
}

/* Accepts the connection and answers its bind; leaves the client bound. */
static bool bind_client(AppHandle app, AppMsgClientId client)
{
    IpcMessage frame = { 0, { NULL, 0 } };
    bool ok;

    if (!take_frame(app, &frame)) {
        return false;
    }
    ok = frame.type == 1 && frame.payload.size >= 2 && get_le16(frame.payload.data) == 1;
    (void)ipc_message_destroy(&frame);
    if (!ok || !send_bind_result(client)) {
        return false;
    }
    for (usize attempt = 0; attempt < 50 && record.links == 0; ++attempt) {
        test_pump(app, 2);
    }
    return record.links == 1 && record.link_error == CORE_OK && record.link_client == client;
}

/* ---- scenarios ------------------------------------------------------------- */

/* The secret comes out of envp, and its absence is not a transport error. */
static void secret_from_environment(void)
{
    AppHandle app = test_app();
    usize length = 0;
    AppMsgHandlers handlers = all_handlers();

    CHECK(app != NULL);
    reset();
    CHECK(libappframework_internal_env_lookup(test_envp, "PUNOS_MSG_SECRET", &length) !=
          NULL);
    CHECK(length == sizeof(SECRET) - 1);
    /* A prefix of the name must not match, and neither must a longer name. */
    CHECK(libappframework_internal_env_lookup(test_envp, "PUNOS_MSG", NULL) == NULL);
    CHECK(libappframework_internal_env_lookup(test_envp, "PUNOS_MSG_SECRETX", NULL) == NULL);
    CHECK(libappframework_internal_env_lookup(test_envp, "HOME", &length) != NULL &&
          length == 1);

    CHECK(app_msg_start(app, envp_without_secret, &handlers) == CORE_UNSUPPORTED);
    CHECK(app_msg_start(app, envp_empty_secret, &handlers) == CORE_UNSUPPORTED);
    CHECK(app_msg_start(app, NULL, &handlers) == CORE_INVALID_ARGUMENT);
    CHECK(app_msg_start(app, test_envp, NULL) == CORE_INVALID_ARGUMENT);
    /* Without app_msg_start() the operations are refused, not queued. */
    CHECK(app_msg_subscribe(app, "ui.theme", on_callback, NULL, NULL) == CORE_INVALID_STATE);
    CHECK(app_destroy(&app) == CORE_OK);
}

/* bind goes first, with exactly the bytes of the protocol, and the request
 * made before it waits. */
static void bind_then_requests(void)
{
    AppHandle app = test_app();
    AppMsgHandlers handlers = all_handlers();
    IpcMessage frame = { 0, { NULL, 0 } };
    AppRequestId request = 0;
    usize offset;

    CHECK(app != NULL);
    reset();
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    /* A request before bind_result: accepted, but not on the wire yet. */
    CHECK(app_msg_subscribe(app, "ui.theme", on_callback, NULL, &request) == CORE_OK);
    CHECK(request == 1);

    CHECK(take_frame(app, &frame));
    CHECK(frame.type == 1); /* bind, not subscribe */
    CHECK(get_le16(frame.payload.data) == 1);
    offset = 2;
    CHECK(get_le16(frame.payload.data + offset) == 8); /* "test-app" */
    offset += 2 + 8;
    CHECK(get_le16(frame.payload.data + offset) == sizeof(SECRET) - 1);
    offset += 2;
    for (usize i = 0; i < sizeof(SECRET) - 1; ++i) {
        CHECK(frame.payload.data[offset + i] == (u8)SECRET[i]);
    }
    CHECK(offset + sizeof(SECRET) - 1 == frame.payload.size);
    (void)ipc_message_destroy(&frame);
    CHECK(record.callbacks == 0);

    CHECK(send_bind_result(7));
    for (usize attempt = 0; attempt < 50 && record.links == 0; ++attempt) {
        test_pump(app, 2);
    }
    CHECK(record.links == 1 && record.link_error == CORE_OK && record.link_client == 7);

    /* Only now the waiting subscribe reaches the broker. */
    CHECK(take_frame(app, &frame));
    CHECK(frame.type == 3);
    (void)ipc_message_destroy(&frame);
    CHECK(fake_peer_reply(&msgserv, 4, CORE_OK));
    for (usize attempt = 0; attempt < 50 && record.callbacks == 0; ++attempt) {
        test_pump(app, 2);
    }
    CHECK(record.callbacks == 1 && record.last_error == CORE_OK);
    CHECK(record.last_had_result && record.last_operation == APP_MSG_OPERATION_SUBSCRIBE);
    CHECK(app_destroy(&app) == CORE_OK);
    fake_peer_drop(&msgserv);
}

/*
 * The contract this module exists for: an event that arrives between a request
 * and its answer goes to the event handler, and the request still gets its own
 * callback with its own result.
 */
static void unsolicited_between_request_and_reply(void)
{
    AppHandle app = test_app();
    AppMsgHandlers handlers = all_handlers();
    IpcMessage frame = { 0, { NULL, 0 } };
    u8 reply[8];

    CHECK(app != NULL);
    reset();
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    CHECK(bind_client(app, 3));

    CHECK(app_msg_publish(app, "ui.theme", (Bytes){ NULL, 0 }, on_callback, NULL, NULL) ==
          CORE_OK);
    CHECK(take_frame(app, &frame));
    CHECK(frame.type == 7);
    (void)ipc_message_destroy(&frame);

    /* First an event, then a deliver_request, then a deliver_response, and
     * only after all three the answer to the publish. */
    CHECK(send_event(11, "other-app", "ui.theme", 5, 4));
    CHECK(send_deliver_request(99, "open_image"));
    CHECK(send_deliver_response(12, CORE_TIMEOUT, 0));
    put_le32(reply, (u32)CORE_OK);
    reply[4] = 2; /* delivered_count */
    reply[5] = 0;
    reply[6] = 1; /* dropped_count */
    reply[7] = 0;
    CHECK(fake_peer_send(&msgserv, 8, reply, sizeof(reply)));
    for (usize attempt = 0; attempt < 100 && record.callbacks == 0; ++attempt) {
        test_pump(app, 2);
    }

    CHECK(record.events == 1);
    CHECK(record.event_sender == 11 && record.event_sequence == 5);
    CHECK(record.event_payload_size == 4);
    CHECK(record.event_topic[0] == 'u' && record.event_topic[7] == 'e');
    CHECK(record.event_application[0] == 'o');
    CHECK(record.requests == 1 && record.request_correlation == 99);
    CHECK(record.request_method[0] == 'o' && record.request_method[9] == 'e');
    /* The transport error and the application status are separate fields. */
    CHECK(record.answers == 1 && record.answer_error == CORE_TIMEOUT &&
          record.answer_status == 0);
    /* And the request's own callback is not confused with any of them. */
    CHECK(record.callbacks == 1 && record.last_error == CORE_OK && record.last_had_result);
    CHECK(record.last_operation == APP_MSG_OPERATION_PUBLISH);
    CHECK(record.delivered == 2 && record.dropped == 1);

    CHECK(app_destroy(&app) == CORE_OK);
    fake_peer_drop(&msgserv);
}

/* An answer may be sent long after the request handler returned. */
static void answer_from_a_timer(void)
{
    AppHandle app = test_app();
    AppMsgHandlers handlers = all_handlers();
    IpcMessage frame = { 0, { NULL, 0 } };

    CHECK(app != NULL);
    reset();
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    CHECK(bind_client(app, 4));
    CHECK(send_deliver_request(123, "echo"));
    for (usize attempt = 0; attempt < 100 && record.requests == 0; ++attempt) {
        test_pump(app, 2);
    }
    CHECK(record.requests == 1 && record.request_correlation == 123);

    /* Not from the handler: the correlation outlives it. */
    CHECK(app_msg_respond(app, record.request_correlation, 55, (Bytes){ NULL, 0 },
                          on_callback, NULL, NULL) == CORE_OK);
    CHECK(take_frame(app, &frame));
    CHECK(frame.type == 11);
    CHECK(get_le16(frame.payload.data) == 1);
    CHECK(get_le32(frame.payload.data + 10) == 55); /* status after correlation_id */
    (void)ipc_message_destroy(&frame);
    CHECK(fake_peer_reply(&msgserv, 12, CORE_OK));
    for (usize attempt = 0; attempt < 50 && record.callbacks == 0; ++attempt) {
        test_pump(app, 2);
    }
    CHECK(record.callbacks == 1 && record.last_error == CORE_OK);
    CHECK(app_msg_respond(app, 0, 0, (Bytes){ NULL, 0 }, on_callback, NULL, NULL) ==
          CORE_INVALID_ARGUMENT);
    CHECK(app_destroy(&app) == CORE_OK);
    fake_peer_drop(&msgserv);
}

/* Two forms of addressing differ by one byte on the wire and nothing else. */
static void request_addressing(void)
{
    AppHandle app = test_app();
    AppMsgHandlers handlers = all_handlers();
    IpcMessage frame = { 0, { NULL, 0 } };
    u8 reply[12];

    CHECK(app != NULL);
    reset();
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    CHECK(bind_client(app, 5));

    CHECK(app_msg_request(app, 17, "open_image", (Bytes){ NULL, 0 }, on_callback, NULL,
                          NULL) == CORE_OK);
    CHECK(take_frame(app, &frame));
    CHECK(frame.type == 9);
    CHECK(frame.payload.data[2] == 0); /* target_kind: client */
    CHECK(get_le32(frame.payload.data + 3) == 17);
    (void)ipc_message_destroy(&frame);
    put_le32(reply, (u32)CORE_OK);
    put64(reply + 4, 777);
    CHECK(fake_peer_send(&msgserv, 10, reply, sizeof(reply)));
    for (usize attempt = 0; attempt < 50 && record.callbacks == 0; ++attempt) {
        test_pump(app, 2);
    }
    CHECK(record.callbacks == 1 && record.correlation == 777);
    CHECK(record.last_operation == APP_MSG_OPERATION_REQUEST);

    CHECK(app_msg_request_application(app, "settings", "get_value", (Bytes){ NULL, 0 },
                                      on_callback, NULL, NULL) == CORE_OK);
    CHECK(take_frame(app, &frame));
    CHECK(frame.type == 9);
    CHECK(frame.payload.data[2] == 1); /* target_kind: application */
    CHECK(get_le16(frame.payload.data + 3) == 8); /* "settings" */
    (void)ipc_message_destroy(&frame);

    /* Argument checks happen before anything is queued. */
    CHECK(app_msg_request(app, 0, "open_image", (Bytes){ NULL, 0 }, on_callback, NULL,
                          NULL) == CORE_INVALID_ARGUMENT);
    CHECK(app_msg_request_application(app, NULL, "x", (Bytes){ NULL, 0 }, on_callback, NULL,
                                      NULL) == CORE_INVALID_ARGUMENT);
    CHECK(app_msg_subscribe(app, NULL, on_callback, NULL, NULL) == CORE_INVALID_ARGUMENT);
    CHECK(app_destroy(&app) == CORE_OK);
    fake_peer_drop(&msgserv);
}

/* A refused bind whose reason cannot change is final: no reconnect loop. */
static void bind_refused_for_good(void)
{
    AppHandle app = test_app();
    AppMsgHandlers handlers = all_handlers();
    IpcMessage frame = { 0, { NULL, 0 } };

    CHECK(app != NULL);
    reset();
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    CHECK(take_frame(app, &frame));
    CHECK(frame.type == 1);
    (void)ipc_message_destroy(&frame);
    CHECK(fake_peer_reply(&msgserv, 2, CORE_PERMISSION_DENIED));
    for (usize attempt = 0; attempt < 100 && record.links == 0; ++attempt) {
        test_pump(app, 2);
    }
    CHECK(record.links == 1 && record.link_error == CORE_PERMISSION_DENIED);
    CHECK(record.link_client == 0);
    /* No retry is armed at all, and no new connection appears however long the
     * loop runs. */
    CHECK(app->msgserv.retry == 0 && app->msgserv.permanent);
    for (usize attempt = 0; attempt < 50; ++attempt) {
        test_pump(app, 2);
    }
    CHECK(!fake_peer_accept(&msgserv));
    /* And a request is refused instead of waiting for a link that will not be. */
    CHECK(app_msg_subscribe(app, "ui.theme", on_callback, NULL, NULL) ==
          CORE_PERMISSION_DENIED);
    CHECK(app_destroy(&app) == CORE_OK);
    fake_peer_drop(&msgserv);
}

/*
 * The broker went away: pending requests end with an error, the module
 * reconnects by itself and re-sends the subscriptions it remembers.
 */
static void reconnect_restores_subscriptions(void)
{
    AppHandle app = test_app();
    AppMsgHandlers handlers = all_handlers();
    IpcMessage frame = { 0, { NULL, 0 } };

    CHECK(app != NULL);
    reset();
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    CHECK(bind_client(app, 8));
    CHECK(app_msg_subscribe(app, "ui.theme", on_callback, NULL, NULL) == CORE_OK);
    CHECK(take_frame(app, &frame));
    CHECK(frame.type == 3);
    (void)ipc_message_destroy(&frame);
    CHECK(fake_peer_reply(&msgserv, 4, CORE_OK));
    for (usize attempt = 0; attempt < 50 && record.callbacks == 0; ++attempt) {
        test_pump(app, 2);
    }
    CHECK(record.callbacks == 1 && record.last_error == CORE_OK);

    /* A publish is in flight when the broker disappears. */
    CHECK(app_msg_publish(app, "ui.theme", (Bytes){ NULL, 0 }, on_callback, NULL, NULL) ==
          CORE_OK);
    CHECK(take_frame(app, &frame));
    (void)ipc_message_destroy(&frame);
    fake_peer_drop(&msgserv);
    for (usize attempt = 0; attempt < 100 && record.callbacks == 1; ++attempt) {
        test_pump(app, 2);
    }
    CHECK(record.callbacks == 2 && record.last_error == CORE_IO_ERROR);
    CHECK(record.links == 2 && record.link_error == CORE_IO_ERROR);

    /* The backoff timer brings the connection back and bind happens again. */
    for (usize attempt = 0; attempt < 400 && !fake_peer_accept(&msgserv); ++attempt) {
        test_pump(app, 20);
    }
    CHECK(take_frame(app, &frame));
    CHECK(frame.type == 1); /* bind, before anything else */
    (void)ipc_message_destroy(&frame);
    CHECK(send_bind_result(9));
    for (usize attempt = 0; attempt < 100 && record.links == 2; ++attempt) {
        test_pump(app, 2);
    }
    CHECK(record.links == 3 && record.link_error == CORE_OK && record.link_client == 9);
    /* The remembered subscription is re-sent without a new app_msg_subscribe. */
    CHECK(take_frame(app, &frame));
    CHECK(frame.type == 3);
    CHECK(get_le16(frame.payload.data + 2) == 8); /* "ui.theme" */
    (void)ipc_message_destroy(&frame);
    CHECK(app_destroy(&app) == CORE_OK);
    fake_peer_drop(&msgserv);
}

/* An unsubscribed topic is forgotten and does not come back on a reconnect. */
static void unsubscribe_is_remembered(void)
{
    AppHandle app = test_app();
    AppMsgHandlers handlers = all_handlers();
    IpcMessage frame = { 0, { NULL, 0 } };

    CHECK(app != NULL);
    reset();
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    CHECK(bind_client(app, 10));
    CHECK(app_msg_subscribe(app, "ui.theme", on_callback, NULL, NULL) == CORE_OK);
    CHECK(take_frame(app, &frame));
    (void)ipc_message_destroy(&frame);
    CHECK(app_msg_unsubscribe(app, "ui.theme", on_callback, NULL, NULL) == CORE_OK);
    CHECK(take_frame(app, &frame));
    CHECK(frame.type == 5);
    (void)ipc_message_destroy(&frame);

    fake_peer_drop(&msgserv);
    for (usize attempt = 0; attempt < 400 && !fake_peer_accept(&msgserv); ++attempt) {
        test_pump(app, 20);
    }
    CHECK(take_frame(app, &frame));
    CHECK(frame.type == 1);
    (void)ipc_message_destroy(&frame);
    CHECK(send_bind_result(11));
    for (usize attempt = 0; attempt < 50; ++attempt) {
        test_pump(app, 2);
    }
    /* Nothing follows bind: the subscription was dropped on purpose. */
    CHECK(fake_peer_receive(&msgserv, &frame) == 0);
    CHECK(app_destroy(&app) == CORE_OK);
    fake_peer_drop(&msgserv);
}

/* A frame that breaks the protocol costs the connection, not the process. */
static void protocol_violation(void)
{
    AppHandle app = test_app();
    AppMsgHandlers handlers = all_handlers();
    IpcMessage frame = { 0, { NULL, 0 } };
    u8 truncated[4] = { 0, 0, 0, 0 };

    CHECK(app != NULL);
    reset();
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    CHECK(bind_client(app, 12));
    CHECK(app_msg_publish(app, "ui.theme", (Bytes){ NULL, 0 }, on_callback, NULL, NULL) ==
          CORE_OK);
    CHECK(take_frame(app, &frame));
    (void)ipc_message_destroy(&frame);
    /* publish_result without delivered_count and dropped_count. */
    CHECK(fake_peer_send(&msgserv, 8, truncated, sizeof(truncated)));
    for (usize attempt = 0; attempt < 100 && record.callbacks == 0; ++attempt) {
        test_pump(app, 2);
    }
    CHECK(record.callbacks == 1 && record.last_error == CORE_IO_ERROR);
    CHECK(record.links == 2 && record.link_error == CORE_IO_ERROR);
    CHECK(app_destroy(&app) == CORE_OK);
    fake_peer_drop(&msgserv);
}

/* An answer of the wrong kind is a violation too, even if it decodes. */
static void wrong_reply_kind(void)
{
    AppHandle app = test_app();
    AppMsgHandlers handlers = all_handlers();
    IpcMessage frame = { 0, { NULL, 0 } };

    CHECK(app != NULL);
    reset();
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    CHECK(bind_client(app, 13));
    CHECK(app_msg_subscribe(app, "ui.theme", on_callback, NULL, NULL) == CORE_OK);
    CHECK(take_frame(app, &frame));
    (void)ipc_message_destroy(&frame);
    CHECK(fake_peer_reply(&msgserv, 8, CORE_OK)); /* publish_result for a subscribe */
    for (usize attempt = 0; attempt < 100 && record.callbacks == 0; ++attempt) {
        test_pump(app, 2);
    }
    CHECK(record.callbacks == 1 && record.last_error == CORE_IO_ERROR);
    CHECK(app_destroy(&app) == CORE_OK);
    fake_peer_drop(&msgserv);
}

/* Correlation 0 is never issued by msgserv: a request_result with it is a
 * protocol violation, and the callback sees the connection failure. */
static void zero_correlation_is_a_violation(void)
{
    AppHandle app = test_app();
    AppMsgHandlers handlers = all_handlers();
    IpcMessage frame = { 0, { NULL, 0 } };
    u8 payload[12] = { 0 };

    CHECK(app != NULL);
    reset();
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    CHECK(bind_client(app, 14));
    CHECK(app_msg_request(app, 7, "method", (Bytes){ NULL, 0 }, on_callback, NULL, NULL) ==
          CORE_OK);
    CHECK(take_frame(app, &frame));
    (void)ipc_message_destroy(&frame);
    /* request_result: CORE_OK, then correlation 0. */
    CHECK(fake_peer_send(&msgserv, 10, payload, sizeof(payload)));
    for (usize attempt = 0; attempt < 100 && record.callbacks == 0; ++attempt) {
        test_pump(app, 2);
    }
    CHECK(record.callbacks == 1 && record.last_error == CORE_IO_ERROR);
    CHECK(app_destroy(&app) == CORE_OK);
    fake_peer_drop(&msgserv);
}

/* A deliver_response with correlation 0 is not an answer to anything. */
static void answer_with_zero_correlation_is_refused(void)
{
    AppHandle app = test_app();
    AppMsgHandlers handlers = all_handlers();
    u8 payload[20] = { 0 };

    CHECK(app != NULL);
    reset();
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    CHECK(bind_client(app, 15));
    /* deliver_response: correlation 0, transport CORE_OK, status 0, empty payload. */
    CHECK(fake_peer_send(&msgserv, 22, payload, sizeof(payload)));
    for (usize attempt = 0; attempt < 50; ++attempt) {
        test_pump(app, 2);
    }
    CHECK(record.answers == 0);
    CHECK(app_destroy(&app) == CORE_OK);
    fake_peer_drop(&msgserv);
}

/* The error of the broker reaches the callback unchanged. */
static void service_error_passed_on(void)
{
    AppHandle app = test_app();
    AppMsgHandlers handlers = all_handlers();
    IpcMessage frame = { 0, { NULL, 0 } };

    CHECK(app != NULL);
    reset();
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    CHECK(bind_client(app, 14));
    CHECK(app_msg_subscribe(app, "ui.theme", on_callback, NULL, NULL) == CORE_OK);
    CHECK(take_frame(app, &frame));
    (void)ipc_message_destroy(&frame);
    CHECK(fake_peer_reply(&msgserv, 4, CORE_PERMISSION_DENIED));
    for (usize attempt = 0; attempt < 100 && record.callbacks == 0; ++attempt) {
        test_pump(app, 2);
    }
    CHECK(record.callbacks == 1 && record.last_error == CORE_PERMISSION_DENIED);
    CHECK(!record.last_had_result);
    /* The connection survives: a refusal by rights is not a transport failure. */
    CHECK(record.links == 1);
    CHECK(app_destroy(&app) == CORE_OK);
    fake_peer_drop(&msgserv);
}

/* app_destroy() ends what is left with CORE_INVALID_STATE, in request order. */
static void destroy_with_pending(void)
{
    AppHandle app = test_app();
    AppMsgHandlers handlers = all_handlers();
    IpcMessage frame = { 0, { NULL, 0 } };

    CHECK(app != NULL);
    reset();
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    CHECK(bind_client(app, 15));
    CHECK(app_msg_subscribe(app, "a.one", on_callback, NULL, NULL) == CORE_OK);
    CHECK(app_msg_subscribe(app, "a.two", on_callback, NULL, NULL) == CORE_OK);
    CHECK(take_frame(app, &frame));
    (void)ipc_message_destroy(&frame);
    CHECK(record.callbacks == 0);
    CHECK(app_destroy(&app) == CORE_OK);
    CHECK(record.callbacks == 2);
    CHECK(record.last_error == CORE_INVALID_STATE && !record.last_had_result);
    CHECK(record.last_request == 2);
    fake_peer_drop(&msgserv);
}

/* An unreachable broker is not an error of app_msg_start(): it retries. */
static void broker_not_running(void)
{
    AppHandle app;
    AppMsgHandlers handlers = all_handlers();

    fake_peer_stop(&msgserv);
    app = test_app();
    CHECK(app != NULL);
    reset();
    /* msgserv starts after appserv, so this is the normal case at boot. */
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    for (usize attempt = 0; attempt < 50; ++attempt) {
        test_pump(app, 2);
    }
    /* The broker appears later and the client finds it. */
    CHECK(fake_peer_start(&msgserv, MSGSERV_PATH));
    CHECK(bind_client(app, 16));
    CHECK(app_destroy(&app) == CORE_OK);
    fake_peer_drop(&msgserv);
}

/* A full subscription table is reported, not silently forgotten on reconnect. */
static void subscribe_overflow_is_reported(void)
{
    AppHandle app = test_app();
    AppMsgHandlers handlers = all_handlers();
    IpcMessage frame = { 0, { NULL, 0 } };
    char topic[8] = "t.00";

    CHECK(app != NULL);
    reset();
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    CHECK(bind_client(app, 10));
    for (usize i = 0; i < APP_MSG_SUBSCRIPTIONS_MAX; ++i) {
        topic[2] = (char)('0' + i / 10);
        topic[3] = (char)('0' + i % 10);
        CHECK(app_msg_subscribe(app, topic, on_callback, NULL, NULL) == CORE_OK);
        CHECK(take_frame(app, &frame));
        (void)ipc_message_destroy(&frame);
    }
    topic[2] = 'x';
    topic[3] = 'x';
    CHECK(app_msg_subscribe(app, topic, on_callback, NULL, NULL) == CORE_OVERFLOW);
    for (usize attempt = 0; attempt < 20; ++attempt) {
        test_pump(app, 2);
    }
    /* Rejected locally: no frame was sent for the overflowing subscribe. */
    CHECK(fake_peer_receive(&msgserv, &frame) == 0);
    CHECK(app_destroy(&app) == CORE_OK);
    fake_peer_drop(&msgserv);
}

/* The pending table is bounded: the 257th request is refused, not queued. */
static void pending_limit_is_enforced(void)
{
    AppHandle app = test_app();
    AppMsgHandlers handlers = all_handlers();
    AppRequestId request = 0;

    CHECK(app != NULL);
    reset();
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    for (usize i = 0; i < APP_MSG_MAX_PENDING; ++i) {
        CHECK(app_msg_request(app, 10, "method", (Bytes){ NULL, 0 }, on_callback, NULL,
                              &request) == CORE_OK);
    }
    CHECK(app_msg_request(app, 10, "method", (Bytes){ NULL, 0 }, on_callback, NULL,
                          &request) == CORE_WOULD_BLOCK);
    CHECK(app_destroy(&app) == CORE_OK);
    CHECK(record.callbacks == APP_MSG_MAX_PENDING);
    fake_peer_drop(&msgserv);
}

/* A full ipclib queue is WOULD_BLOCK for the caller, never a transport error. */
static void full_queue_is_would_block(void)
{
    AppHandle app = test_app();
    AppMsgHandlers handlers = all_handlers();
    AppRequestId request = 0;

    CHECK(app != NULL);
    reset();
    CHECK(app_msg_start(app, test_envp, &handlers) == CORE_OK);
    CHECK(bind_client(app, 10));
    fault_plan.send_error = CORE_OVERFLOW;
    CHECK(app_msg_publish(app, "ui.theme", (Bytes){ (u8 *)"x", 1 }, on_callback, NULL,
                          &request) == CORE_WOULD_BLOCK);
    CHECK(app_msg_publish(app, "ui.theme", (Bytes){ (u8 *)"x", 1 }, on_callback, NULL,
                          &request) == CORE_OK);
    CHECK(app_destroy(&app) == CORE_OK);
    fake_peer_drop(&msgserv);
}

int main(void)
{
    const char *saved_msg = libappframework_internal_msgserv_endpoint;
    const char *saved_log = libappframework_internal_logserv_endpoint;

    libappframework_internal_msgserv_endpoint = MSGSERV_PATH;
    libappframework_internal_logserv_endpoint = LOGSERV_PATH;
    CHECK(fake_peer_start(&msgserv, MSGSERV_PATH) && fake_peer_start(&logserv, LOGSERV_PATH));
    secret_from_environment();
    bind_then_requests();
    unsolicited_between_request_and_reply();
    answer_from_a_timer();
    request_addressing();
    bind_refused_for_good();
    reconnect_restores_subscriptions();
    unsubscribe_is_remembered();
    subscribe_overflow_is_reported();
    pending_limit_is_enforced();
    full_queue_is_would_block();
    protocol_violation();
    wrong_reply_kind();
    zero_correlation_is_a_violation();
    answer_with_zero_correlation_is_refused();
    service_error_passed_on();
    destroy_with_pending();
    broker_not_running();
    fake_peer_stop(&msgserv);
    fake_peer_stop(&logserv);
    libappframework_internal_msgserv_endpoint = saved_msg;
    libappframework_internal_logserv_endpoint = saved_log;
    check_passed();
    return 0;
}
