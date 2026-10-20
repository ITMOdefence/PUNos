/*
 * Runtime test of the logserv client and app_log() against a fake logserv
 * (tests/fake_peer.h). The event loop is run by hand with test_pump().
 *
 * Check groups:
 *   1-20   lazy connection, the record on the wire, replies
 *   21-40  app_log() argument checks; nothing invalid reaches the peer
 *   41-55  records queued while Connecting keep their order
 *   56-70  limit of pending records, error replies, recovery
 *   71-90  protocol violations and peer failures close the connection
 *   91-100 unreachable logserv, reconnection
 *   101-110 app_destroy() hands the last records over; Stopping state
 */

#define CHECK_TEST_NAME "appframework-logserv"
#include <check.h>

#include "fake_peer.h"
#include "fault.h"

/* Like EXPECT() for a number that is only known at run time. */
#define CHECK_AT(number, condition) \
    do {                            \
        if (!(condition)) {         \
            check_fail(number);     \
        }                           \
    } while (0)

#define SOCKET_PATH "/tmp/appframework-test-logserv.sock"
#define SUBMIT 1
#define SUBMIT_RESULT 2

typedef struct Record {
    u16 version;
    i32 process_id;
    i32 thread_id;
    u8 level;
    u16 component_length;
    u32 message_length;
    const u8 *component;
    const u8 *message;
} Record;

static bool same_text(const u8 *data, usize size, const char *text)
{
    usize i = 0;

    for (; i < size && text[i] != '\0'; ++i) {
        if (data[i] != (u8)text[i]) {
            return false;
        }
    }
    return i == size && text[i] == '\0';
}

/* Decodes a log.submit payload; false if its sizes do not add up. */
static bool decode(const IpcMessage *message, Record *out)
{
    const u8 *p = message->payload.data;

    if (message->type != SUBMIT || message->payload.size < 17) {
        return false;
    }
    out->version = get_le16(p);
    out->process_id = (i32)get_le32(p + 2);
    out->thread_id = (i32)get_le32(p + 6);
    out->level = p[10];
    out->component_length = get_le16(p + 11);
    out->message_length = get_le32(p + 13);
    if (message->payload.size != 17 + (usize)out->component_length + out->message_length) {
        return false;
    }
    out->component = p + 17;
    out->message = p + 17 + out->component_length;
    return true;
}

/* Receives one record and checks its level and text; replies if asked to. */
static bool expect_record(FakePeer *peer, u8 level, const char *text, bool reply)
{
    IpcMessage message = { 0, { NULL, 0 } };
    Record record;
    bool ok;

    if (fake_peer_receive(peer, &message) != 1) {
        return false;
    }
    ok = decode(&message, &record) && record.version == 1 && record.level == level &&
         record.process_id == getpid() && record.thread_id == gettid() &&
         same_text(record.component, record.component_length, "test") &&
         same_text(record.message, record.message_length, text);
    (void)ipc_message_destroy(&message);
    return ok && (!reply || fake_peer_reply(peer, SUBMIT_RESULT, CORE_OK));
}

/* Counts the messages that arrive, answering each one with code. */
static u32 drain(AppHandle app, FakePeer *peer, bool reply, ErrorCode code)
{
    u32 received = 0;
    IpcMessage message = { 0, { NULL, 0 } };

    for (u32 round = 0; round < 100; ++round) {
        test_pump(app, 5);
        (void)fake_peer_accept(peer);
        while (fake_peer_receive(peer, &message) == 1) {
            (void)ipc_message_destroy(&message);
            received++;
            if (reply) {
                (void)fake_peer_reply(peer, SUBMIT_RESULT, code);
            }
        }
    }
    test_pump(app, 5);
    return received;
}

static bool connection_closed(AppHandle app, FakePeer *peer)
{
    IpcMessage message = { 0, { NULL, 0 } };

    test_pump(app, 20);
    return fake_peer_receive(peer, &message) == -1;
}

static AppHandle create_app(FakePeer *peer, bool accept)
{
    AppHandle app = test_app();

    test_pump(app, 20);
    if (accept && !fake_peer_accept(peer)) {
        return NULL;
    }
    return app;
}

/* ---- the basics ---------------------------------------------------------- */

static void basics(FakePeer *peer)
{
    AppHandle app = create_app(peer, true);
    IpcMessage message = { 0, { NULL, 0 } };

    EXPECT(1, app != NULL);               /* app_create() connected by itself */
    EXPECT(2, app->logserv.connection != NULL && app->logserv.open);
    EXPECT(3, app->logserv.unacknowledged == 1 && app->source_count == 1);
    EXPECT(4, expect_record(peer, APP_LOG_LEVEL_INFO, "AppHandle created", true));
    test_pump(app, 20);
    EXPECT(5, app->logserv.unacknowledged == 0);
    /* the library's own source is not the application's to remove */
    EXPECT(133, app->logserv.source != 0 &&
                    app_event_remove(app, app->logserv.source) == CORE_NOT_FOUND);
    EXPECT(134, app->source_count == 1 && app_log(app, APP_LOG_LEVEL_INFO, "still wired") == CORE_OK);
    test_pump(app, 20);
    EXPECT(135, expect_record(peer, APP_LOG_LEVEL_INFO, "still wired", true));
    /* every level, the record format and the order */
    EXPECT(6, app_log(app, APP_LOG_LEVEL_TRACE, "t") == CORE_OK);
    EXPECT(7, app_log(app, APP_LOG_LEVEL_DEBUG, "d") == CORE_OK);
    EXPECT(8, app_log(app, APP_LOG_LEVEL_INFO, "i") == CORE_OK);
    EXPECT(9, app_log(app, APP_LOG_LEVEL_WARNING, "w") == CORE_OK);
    EXPECT(10, app_log(app, APP_LOG_LEVEL_ERROR, "e") == CORE_OK);
    EXPECT(11, app_log(app, APP_LOG_LEVEL_FATAL, "f") == CORE_OK);
    test_pump(app, 20);
    EXPECT(12, expect_record(peer, 0, "t", true) && expect_record(peer, 1, "d", true));
    EXPECT(13, expect_record(peer, 2, "i", true) && expect_record(peer, 3, "w", true));
    EXPECT(14, expect_record(peer, 4, "e", true) && expect_record(peer, 5, "f", true));
    EXPECT(15, fake_peer_receive(peer, &message) == 0);
    test_pump(app, 20);
    EXPECT(16, app->logserv.unacknowledged == 0);
    /* a multi-byte text is counted in bytes */
    EXPECT(17, app_log(app, APP_LOG_LEVEL_INFO, "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82") ==
                   CORE_OK);
    test_pump(app, 20);
    EXPECT(18, fake_peer_receive(peer, &message) == 1 && message.payload.size == 17 + 4 + 12);
    (void)ipc_message_destroy(&message);
    EXPECT(19, app_log(app, APP_LOG_LEVEL_INFO, "") == CORE_OK); /* an empty message is a message */
    EXPECT(20, app_destroy(&app) == CORE_OK);
}

static void argument_checks(FakePeer *peer)
{
    AppHandle app = create_app(peer, true);
    IpcMessage message = { 0, { NULL, 0 } };
    static char longest[16385 + 1];
    usize before;

    EXPECT(21, app != NULL && expect_record(peer, 2, "AppHandle created", true));
    before = app->logserv.unacknowledged;
    for (usize i = 0; i < 16385; ++i) {
        longest[i] = 'x';
    }
    longest[16385] = '\0';
    EXPECT(22, app_log(NULL, APP_LOG_LEVEL_INFO, "m") == CORE_INVALID_ARGUMENT);
    EXPECT(23, app_log(app, APP_LOG_LEVEL_INFO, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(24, app_log(app, (AppLogLevel)6, "m") == CORE_INVALID_ARGUMENT);
    EXPECT(25, app_log(app, (AppLogLevel)-1, "m") == CORE_INVALID_ARGUMENT);
    EXPECT(26, app_log(app, APP_LOG_LEVEL_INFO, longest) == CORE_INVALID_ARGUMENT);
    EXPECT(27, app_log(app, APP_LOG_LEVEL_INFO, "\xC0\x80") == CORE_INVALID_ARGUMENT);
    EXPECT(28, app_log(app, APP_LOG_LEVEL_INFO, "ok\xED\xA0\x80") == CORE_INVALID_ARGUMENT);
    EXPECT(29, app_log(app, APP_LOG_LEVEL_INFO, "\xF4\x90\x80\x80") == CORE_INVALID_ARGUMENT);
    EXPECT(30, app_log(app, APP_LOG_LEVEL_INFO, "truncated\xE2\x82") == CORE_INVALID_ARGUMENT);
    test_pump(app, 20);
    EXPECT(31, fake_peer_receive(peer, &message) == 0); /* none of them was sent */
    EXPECT(32, app->logserv.unacknowledged == 0 && before == 1);
    longest[16384] = '\0';
    EXPECT(33, app_log(app, APP_LOG_LEVEL_INFO, longest) == CORE_OK); /* the exact limit */
    /* A 16 KiB record may not fit the socket buffer at once: read while the
     * application keeps flushing. */
    for (u32 round = 0, got = 0; round < 100 && got != 1; ++round) {
        test_pump(app, 20);
        got = (u32)fake_peer_receive(peer, &message);
        if (got == 1) {
            break;
        }
    }
    EXPECT(34, message.payload.size == 17 + 4 + 16384);
    (void)ipc_message_destroy(&message);
    EXPECT(35, true);
    EXPECT(36, app_destroy(&app) == CORE_OK);
}

/* ---- Connecting ---------------------------------------------------------------- */

static void connecting_queue(FakePeer *peer)
{
    AppHandle app;
    IpcMessage message = { 0, { NULL, 0 } };

    fault_plan.connecting_polls = 3;
    app = test_app(); /* the first record is queued, the connection is not open */
    EXPECT(41, app != NULL && app->logserv.connection != NULL && !app->logserv.open);
    EXPECT(42, app->logserv.queued == 1);
    EXPECT(43, app_log(app, APP_LOG_LEVEL_WARNING, "second") == CORE_OK);
    EXPECT(44, app_log(app, APP_LOG_LEVEL_ERROR, "third") == CORE_OK);
    EXPECT(45, app->logserv.queued == 3 && app->logserv.unacknowledged == 0);
    EXPECT(46, fake_peer_accept(peer));
    EXPECT(47, fake_peer_receive(peer, &message) == 0); /* nothing before Open */
    test_pump(app, 50);
    EXPECT(48, fault_plan.connecting_polls == 0 && app->logserv.open);
    EXPECT(49, app->logserv.queued == 0 && app->logserv.unacknowledged == 3);
    EXPECT(50, expect_record(peer, 2, "AppHandle created", true));
    EXPECT(51, expect_record(peer, 3, "second", true));
    EXPECT(52, expect_record(peer, 4, "third", true));
    test_pump(app, 20);
    EXPECT(53, app->logserv.unacknowledged == 0);
    EXPECT(54, app->logserv.queue == NULL); /* the queue memory was released */
    EXPECT(55, app_destroy(&app) == CORE_OK);
}

/* ---- the pending limit ------------------------------------------------------------------ */

static void pending_limit(FakePeer *peer)
{
    AppHandle app = create_app(peer, true);
    u32 accepted = 1; /* the record about the creation */
    ErrorCode last = CORE_OK;
    u32 received;

    EXPECT(56, app != NULL);
    while (accepted < 1000 && (last = app_log(app, APP_LOG_LEVEL_INFO, "fill")) == CORE_OK) {
        accepted++;
    }
    EXPECT(57, last == CORE_WOULD_BLOCK);
    EXPECT(58, accepted == APP_LOG_MAX_PENDING);
    EXPECT(59, app_log(app, APP_LOG_LEVEL_INFO, "more") == CORE_WOULD_BLOCK); /* still full */
    /* The peer answers with an error code: the records are lost, but counted. */
    received = drain(app, peer, true, CORE_WOULD_BLOCK);
    EXPECT(60, received == APP_LOG_MAX_PENDING);
    EXPECT(61, app->logserv.unacknowledged == 0);
    EXPECT(62, app_log(app, APP_LOG_LEVEL_INFO, "again") == CORE_OK);
    test_pump(app, 20);
    EXPECT(63, expect_record(peer, 2, "again", true)); /* only the new one, nothing repeated */
    EXPECT(64, app_destroy(&app) == CORE_OK);
}

/* ipclib refuses a message: CORE_OVERFLOW (its queue is full) becomes
 * CORE_WOULD_BLOCK, any other error is passed on, and the connection stays. */
static void send_refused(FakePeer *peer)
{
    AppHandle app = create_app(peer, true);
    usize before;

    EXPECT(65, app != NULL);
    before = app->logserv.unacknowledged;
    fault_plan.send_error = CORE_OVERFLOW;
    EXPECT(66, app_log(app, APP_LOG_LEVEL_INFO, "refused") == CORE_WOULD_BLOCK);
    fault_plan.send_error = CORE_OUT_OF_MEMORY;
    EXPECT(67, app_log(app, APP_LOG_LEVEL_INFO, "refused") == CORE_OUT_OF_MEMORY);
    EXPECT(68, app->logserv.connection != NULL && app->logserv.open);
    EXPECT(69, app->logserv.unacknowledged == before); /* nothing was counted */
    EXPECT(70, app_log(app, APP_LOG_LEVEL_INFO, "fine") == CORE_OK);
    EXPECT(71, drain(app, peer, true, CORE_OK) == 2); /* "created" and "fine" */
    EXPECT(75, app_destroy(&app) == CORE_OK);
}

/* A socket error while writing closes the connection and drops what is unconfirmed. */
static void flush_failure(FakePeer *peer)
{
    AppHandle app = create_app(peer, true);
    IpcMessage message = { 0, { NULL, 0 } };

    EXPECT(77, app != NULL && expect_record(peer, 2, "AppHandle created", false));
    fault_plan.flush_error = CORE_IO_ERROR;
    EXPECT(78, app_log(app, APP_LOG_LEVEL_INFO, "broken") == CORE_IO_ERROR);
    EXPECT(79, app->logserv.connection == NULL && app->logserv.unacknowledged == 0);
    EXPECT(80, app->source_count == 0);
    EXPECT(81, fake_peer_receive(peer, &message) == -1);
    EXPECT(82, app_destroy(&app) == CORE_OK);
}

/* The socket buffer fills up: the rest waits in ipclib and goes out when the
 * peer reads, driven by the WRITABLE interest of the source. */
static void backpressure(FakePeer *peer)
{
    AppHandle app = create_app(peer, true);
    static char big[16385];
    u32 accepted = 1; /* the creation record */

    EXPECT(83, app != NULL);
    for (usize i = 0; i < 16384; ++i) {
        big[i] = 'p';
    }
    for (u32 i = 0; i < 100; ++i) {
        if (app_log(app, APP_LOG_LEVEL_INFO, big) != CORE_OK) {
            check_fail(84);
        }
        accepted++;
    }
    EXPECT(85, app->logserv.write_interest); /* more than the socket took */
    EXPECT(86, drain(app, peer, true, CORE_OK) == accepted);
    EXPECT(87, app->logserv.unacknowledged == 0 && !app->logserv.write_interest);
    EXPECT(88, app_destroy(&app) == CORE_OK);
}

/* ---- violations ------------------------------------------------------------------------------ */

/* Starts a connection with the created record unanswered, then runs bad. */
static void violation(FakePeer *peer, u32 first_check, u32 type, const u8 *payload, usize size,
                      bool raw)
{
    AppHandle app = create_app(peer, true);
    IpcMessage message = { 0, { NULL, 0 } };

    if (app == NULL || !expect_record(peer, 2, "AppHandle created", false)) {
        check_fail(first_check);
    }
    if (raw) {
        (void)fake_peer_write_raw(peer, payload, size);
    } else {
        (void)fake_peer_send(peer, type, payload, size);
    }
    test_pump(app, 30);
    CHECK_AT(first_check + 1, app->logserv.connection == NULL); /* closed ... */
    CHECK_AT(first_check + 2, app->logserv.unacknowledged == 0 && app->logserv.queued == 0 &&
                                  app->source_count == 0); /* the source went with the connection */
    CHECK_AT(first_check + 3, fake_peer_receive(peer, &message) == -1);
    /* ... and the next record connects again; the lost one is not resent. */
    CHECK_AT(first_check + 4, app_log(app, APP_LOG_LEVEL_INFO, "next") == CORE_OK);
    test_pump(app, 20);
    CHECK_AT(first_check + 5, fake_peer_accept(peer) && expect_record(peer, 2, "next", true));
    CHECK_AT(first_check + 6, app_destroy(&app) == CORE_OK);
}

static void protocol_violations(FakePeer *peer)
{
    static const u8 ok_payload[4] = { 0, 0, 0, 0 };
    static const u8 short_payload[3] = { 0, 0, 0 };
    static const u8 long_payload[8] = { 0 };
    /* a frame header promising 4 bytes, then the wire goes quiet for good: not a
     * violation yet, so it is checked as "no change" below */

    violation(peer, 72, 3, ok_payload, 4, false);        /* wrong message type */
    violation(peer, 80, SUBMIT_RESULT, short_payload, 3, false);
    violation(peer, 88, SUBMIT_RESULT, long_payload, 8, false);
}

static void extra_reply_and_close(FakePeer *peer)
{
    AppHandle app = create_app(peer, true);

    EXPECT(96, app != NULL && expect_record(peer, 2, "AppHandle created", true));
    test_pump(app, 20);
    EXPECT(97, app->logserv.unacknowledged == 0);
    EXPECT(98, fake_peer_reply(peer, SUBMIT_RESULT, CORE_OK)); /* nobody asked for it */
    EXPECT(99, connection_closed(app, peer));
    EXPECT(100, app->logserv.connection == NULL);
    EXPECT(101, app_destroy(&app) == CORE_OK);

    /* the peer hangs up: unconfirmed records are dropped, the next one reconnects */
    app = create_app(peer, true);
    EXPECT(102, app != NULL && expect_record(peer, 2, "AppHandle created", false));
    fake_peer_drop(peer);
    test_pump(app, 30);
    EXPECT(103, app->logserv.connection == NULL && app->logserv.unacknowledged == 0);
    EXPECT(104, app_log(app, APP_LOG_LEVEL_INFO, "after hangup") == CORE_OK);
    test_pump(app, 20);
    EXPECT(105, fake_peer_accept(peer) && expect_record(peer, 2, "after hangup", true));
    EXPECT(106, app_destroy(&app) == CORE_OK);
}

/* ---- no logserv --------------------------------------------------------------------------------- */

static void unreachable_logserv(FakePeer *peer)
{
    const char *saved = libappframework_internal_logserv_endpoint;
    AppHandle app;

    fake_peer_stop(peer);
    app = test_app();
    EXPECT(110, app != NULL); /* app_create() does not fail because of logserv */
    EXPECT(111, app->logserv.connection == NULL);
    EXPECT(112, app_log(app, APP_LOG_LEVEL_INFO, "nowhere") == core_error_from_errno(ENOENT));
    EXPECT(113, core_error_from_errno(ENOENT) == CORE_NOT_FOUND);
    EXPECT(114, app->logserv.queued == 0); /* the record was dropped, not kept */
    EXPECT(115, fake_peer_start(peer, SOCKET_PATH));
    EXPECT(116, app_log(app, APP_LOG_LEVEL_INFO, "now") == CORE_OK); /* tries again */
    test_pump(app, 20);
    EXPECT(117, fake_peer_accept(peer) && expect_record(peer, 2, "now", true));
    EXPECT(118, app_destroy(&app) == CORE_OK);
    libappframework_internal_logserv_endpoint = saved;
}

/* On a thread other than the main one the thread id differs from the process
 * id, so a swap of the two fields cannot go unnoticed. */
static FakePeer *thread_peer;

static i32 log_from_thread(void *argument)
{
    AppHandle app = create_app(thread_peer, true);
    (void)argument;

    if (app == NULL || gettid() == getpid()) {
        return 1;
    }
    /* expect_record() compares the ids with getpid() and gettid() of this thread */
    if (!expect_record(thread_peer, 2, "AppHandle created", true)) {
        return 2;
    }
    if (app_log(app, APP_LOG_LEVEL_INFO, "from thread") != CORE_OK) {
        return 3;
    }
    test_pump(app, 20);
    if (!expect_record(thread_peer, 2, "from thread", true)) {
        return 4;
    }
    return app_destroy(&app) == CORE_OK ? 0 : 5;
}

static void thread_identity(FakePeer *peer)
{
    thread_peer = peer;
    EXPECT(76, test_run_on_thread(log_from_thread, NULL));
}

/* ---- destroy and Stopping ------------------------------------------------------------------------- */

static void destroy_hands_over(FakePeer *peer)
{
    AppHandle app = create_app(peer, true);

    EXPECT(120, app != NULL && expect_record(peer, 2, "AppHandle created", true));
    EXPECT(121, app_log(app, APP_LOG_LEVEL_INFO, "last words") == CORE_OK);
    EXPECT(122, app_request_quit(app) == CORE_OK && app_run(app) == CORE_OK);
    EXPECT(123, app_log(app, APP_LOG_LEVEL_INFO, "late") == CORE_INVALID_STATE); /* Stopping */
    EXPECT(124, app_destroy(&app) == CORE_OK);
    EXPECT(125, expect_record(peer, 2, "last words", false));
    EXPECT(126, expect_record(peer, 2, "AppHandle destroyed", false));
    {
        IpcMessage message = { 0, { NULL, 0 } };

        EXPECT(127, fake_peer_receive(peer, &message) == -1); /* the connection is closed */
    }
}

/* Memory in use must not grow from one round to the next. */
static void no_leaks(FakePeer *peer)
{
    i64 baseline = 0;

    for (u32 round = 0; round < 30; ++round) {
        AppHandle app = create_app(peer, true);
        IpcMessage message = { 0, { NULL, 0 } };

        if (app == NULL || app_log(app, APP_LOG_LEVEL_INFO, "round") != CORE_OK) {
            check_fail(130);
        }
        test_pump(app, 5);
        while (fake_peer_receive(peer, &message) == 1) {
            (void)ipc_message_destroy(&message);
        }
        if (app_destroy(&app) != CORE_OK) {
            check_fail(131);
        }
        if (round == 2) {
            baseline = (i64)fault_plan.allocations - (i64)fault_plan.frees;
        }
    }
    EXPECT(132, (i64)fault_plan.allocations - (i64)fault_plan.frees == baseline);
}

int main(void)
{
    FakePeer peer;
    const char *saved = libappframework_internal_logserv_endpoint;

    libappframework_internal_logserv_endpoint = SOCKET_PATH;
    EXPECT(200, fake_peer_start(&peer, SOCKET_PATH));
    basics(&peer);
    argument_checks(&peer);
    connecting_queue(&peer);
    pending_limit(&peer);
    send_refused(&peer);
    flush_failure(&peer);
    backpressure(&peer);
    thread_identity(&peer);
    protocol_violations(&peer);
    extra_reply_and_close(&peer);
    unreachable_logserv(&peer);
    destroy_hands_over(&peer);
    no_leaks(&peer);
    fake_peer_stop(&peer);
    libappframework_internal_logserv_endpoint = saved;
    check_passed();
    return 0;
}
