/*
 * Runtime test of the appserv client (app_apps_*) against a fake appserv and a
 * fake logserv (tests/fake_peer.h). The event loop is run by hand with
 * test_pump(). Checks are numbered in the order they run.
 *
 *   round trips of the five operations      queue limit, send refusals
 *   FIFO and callback order                 protocol violations, peer failures
 *   errors of the service passed on         unavailable appserv, reconnection
 *   argument and state checks               app_destroy() with pending requests
 *   requests made while Connecting          callbacks that start new requests
 */

#define CHECK_TEST_NAME "appframework-apps"
#include <check.h>

#include "fake_peer.h"
#include "fault.h"

#define APPSERV_PATH "/tmp/appframework-test-appserv.sock"
#define LOGSERV_PATH "/tmp/appframework-test-logserv2.sock"

static FakePeer appserv;
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

/* The failing line is printed; the exit status is that line modulo 255 (the
 * scenarios run many times, so a per-check number would not be unique). */
#define CHECK(condition)                \
    do {                                \
        if (!(condition)) {             \
            fail_at(__LINE__);          \
        }                               \
    } while (0)

/* ---- recording callbacks ---------------------------------------------------- */

typedef struct Call {
    AppRequestId request;
    ErrorCode error;
    bool has_result;
    AppAppsOperation operation;
    usize count;
    char first_id[66];
    char first_name[66];
    AppInstanceId instance;
    AppInstanceStatus status;
    void *context;
} Call;

static Call calls[300];
static u32 call_count;

static void copy_text(char *destination, const char *source)
{
    usize i = 0;

    for (; source[i] != '\0' && i < 65; ++i) {
        destination[i] = source[i];
    }
    destination[i] = '\0';
}

static void record(AppHandle app, AppRequestId request, ErrorCode error,
                   const AppAppsResult *result, void *context)
{
    Call *call = &calls[call_count < 300 ? call_count : 299];

    (void)app;
    call_count++;
    call->request = request;
    call->error = error;
    call->has_result = result != NULL;
    call->context = context;
    call->first_id[0] = call->first_name[0] = '\0';
    if (result == NULL) {
        return;
    }
    call->operation = result->operation;
    switch (result->operation) {
    case APP_APPS_OPERATION_LIST:
        call->count = result->list.count;
        if (result->list.count > 0) {
            copy_text(call->first_id, result->list.items[0].application_id);
            copy_text(call->first_name, result->list.items[0].display_name);
        }
        break;
    case APP_APPS_OPERATION_GET_INFO:
        copy_text(call->first_id, result->info.application_id);
        copy_text(call->first_name, result->info.display_name);
        break;
    case APP_APPS_OPERATION_LAUNCH:
        call->instance = result->instance;
        break;
    case APP_APPS_OPERATION_GET_STATUS:
        call->status = result->status;
        break;
    case APP_APPS_OPERATION_STOP:
        break;
    }
}

static bool same_text(const char *left, const char *right)
{
    usize i = 0;

    for (; left[i] != '\0' && right[i] != '\0'; ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }
    return left[i] == right[i];
}

/* ---- the fake appserv -------------------------------------------------------------- */

/* One request from the application: its type and payload bytes. */
static bool take_request(u32 type, const u8 *payload, usize size)
{
    IpcMessage message = { 0, { NULL, 0 } };
    bool ok;

    if (fake_peer_receive(&appserv, &message) != 1) {
        return false;
    }
    ok = message.type == type && message.payload.size == size;
    for (usize i = 0; ok && i < size; ++i) {
        ok = message.payload.data[i] == payload[i];
    }
    (void)ipc_message_destroy(&message);
    return ok;
}

static u32 take_any_requests(void)
{
    IpcMessage message = { 0, { NULL, 0 } };
    u32 count = 0;

    while (fake_peer_receive(&appserv, &message) == 1) {
        (void)ipc_message_destroy(&message);
        count++;
    }
    return count;
}

/* Collects the requests that reach the fake appserv while the application keeps
 * running: the connection may still be completing, and a partial flush goes on
 * only while somebody reads. A fixed number of pumps proved too short on a slow
 * (QEMU without KVM) machine. Returns once `expected` requests arrived and one
 * more round brought nothing, or after about three seconds. */
static u32 wait_requests(AppHandle app, u32 expected)
{
    u32 count = 0;

    for (u32 round = 0; round < 150; ++round) {
        u32 got;

        (void)fake_peer_accept(&appserv);
        test_pump(app, 20);
        got = take_any_requests();
        count += got;
        if (count >= expected && got == 0) {
            break;
        }
    }
    return count;
}

static void put16(u8 *out, u16 value)
{
    out[0] = (u8)(value & 0xff);
    out[1] = (u8)(value >> 8);
}

static usize add_info(u8 *out, usize offset, const char *id, const char *name)
{
    usize id_length = 0;
    usize name_length = 0;

    while (id[id_length] != '\0') {
        ++id_length;
    }
    while (name[name_length] != '\0') {
        ++name_length;
    }
    put16(out + offset, (u16)id_length);
    offset += 2;
    for (usize i = 0; i < id_length; ++i) {
        out[offset++] = (u8)id[i];
    }
    put16(out + offset, (u16)name_length);
    offset += 2;
    for (usize i = 0; i < name_length; ++i) {
        out[offset++] = (u8)name[i];
    }
    return offset;
}

static bool reply_list_two(void)
{
    u8 payload[64] = { 0 };
    usize size = 6;

    put16(payload + 4, 2);
    size = add_info(payload, size, "clock", "Clock");
    size = add_info(payload, size, "notes", "Notes");
    return fake_peer_send(&appserv, 2, payload, size);
}

static bool reply_info(const char *id, const char *name)
{
    u8 payload[64] = { 0 };

    return fake_peer_send(&appserv, 4, payload, add_info(payload, 4, id, name));
}

static bool reply_launch(u64 instance)
{
    u8 payload[12] = { 0 };

    for (u32 i = 0; i < 8; ++i) {
        payload[4 + i] = (u8)((instance >> (8 * i)) & 0xff);
    }
    return fake_peer_send(&appserv, 6, payload, sizeof(payload));
}

static bool reply_status(u8 state, u8 kind, i32 value)
{
    u8 payload[10] = { 0 };

    payload[4] = state;
    payload[5] = kind;
    put_le32(payload + 6, (u32)value);
    return fake_peer_send(&appserv, 10, payload, sizeof(payload));
}

static bool reply_error(u32 type, ErrorCode code)
{
    return fake_peer_reply(&appserv, type, code);
}

/* ---- the environment of one scenario ------------------------------------------------- */

static AppHandle start(void)
{
    AppHandle app = test_app();

    call_count = 0;
    if (app != NULL) {
        test_pump(app, 10);
        (void)fake_peer_accept(&appserv); /* none yet; the connection is lazy */
    }
    return app;
}

static void settle(AppHandle app)
{
    test_pump(app, 20);
    (void)fake_peer_accept(&appserv);
    test_pump(app, 20);
}

static bool log_mentions(const char *needle)
{
    IpcMessage message = { 0, { NULL, 0 } };
    bool found = false;

    (void)fake_peer_accept(&logserv);
    while (fake_peer_receive(&logserv, &message) == 1) {
        for (usize i = 0; i + 1 < message.payload.size && !found; ++i) {
            usize k = 0;

            while (needle[k] != '\0' && i + k < message.payload.size &&
                   message.payload.data[i + k] == (u8)needle[k]) {
                ++k;
            }
            found = needle[k] == '\0';
        }
        (void)ipc_message_destroy(&message);
    }
    return found;
}

static AppRequestId quick(AppHandle app, AppAppsOperation operation)
{
    AppRequestId id = 0;
    ErrorCode result;

    switch (operation) {
    case APP_APPS_OPERATION_LIST:
        result = app_apps_list(app, record, NULL, &id);
        break;
    case APP_APPS_OPERATION_GET_INFO:
        result = app_apps_get_info(app, "clock", record, NULL, &id);
        break;
    case APP_APPS_OPERATION_LAUNCH:
        result = app_apps_launch(app, "clock", record, NULL, &id);
        break;
    case APP_APPS_OPERATION_STOP:
        result = app_apps_stop(app, 77, record, NULL, &id);
        break;
    default:
        result = app_apps_get_status(app, 77, record, NULL, &id);
        break;
    }
    return result == CORE_OK ? id : 0;
}

/* ---- round trips --------------------------------------------------------------------- */

static void round_trips(void)
{
    static const u8 clock_request[] = { 1, 0, 5, 0, 'c', 'l', 'o', 'c', 'k' };
    static const u8 instance_request[] = { 1, 0, 77, 0, 0, 0, 0, 0, 0, 0 };
    AppHandle app = start();
    AppRequestId id = 0;

    CHECK(app != NULL);
    CHECK(app_apps_list(app, record, (void *)1, &id) == CORE_OK && id == 1);
    settle(app);
    CHECK(take_request(1, NULL, 0));
    CHECK(reply_list_two());
    test_pump(app, 20);
    CHECK(call_count == 1 && calls[0].request == 1 && calls[0].error == CORE_OK);
    CHECK(calls[0].has_result && calls[0].operation == APP_APPS_OPERATION_LIST);
    CHECK(calls[0].count == 2 && same_text(calls[0].first_id, "clock") &&
          same_text(calls[0].first_name, "Clock") && calls[0].context == (void *)1);

    CHECK(app_apps_get_info(app, "clock", record, NULL, &id) == CORE_OK && id == 2);
    settle(app);
    CHECK(take_request(3, clock_request, sizeof(clock_request)));
    CHECK(reply_info("clock", "Clock"));
    test_pump(app, 20);
    CHECK(call_count == 2 && calls[1].request == 2 && calls[1].error == CORE_OK);
    CHECK(calls[1].operation == APP_APPS_OPERATION_GET_INFO &&
          same_text(calls[1].first_id, "clock") && same_text(calls[1].first_name, "Clock"));

    CHECK(app_apps_launch(app, "clock", record, NULL, NULL) == CORE_OK); /* out_request is optional */
    settle(app);
    CHECK(take_request(5, clock_request, sizeof(clock_request)));
    CHECK(reply_launch(77));
    test_pump(app, 20);
    CHECK(call_count == 3 && calls[2].request == 3 && calls[2].instance == 77);
    CHECK(calls[2].operation == APP_APPS_OPERATION_LAUNCH);

    CHECK(app_apps_get_status(app, 77, record, NULL, &id) == CORE_OK && id == 4);
    settle(app);
    CHECK(take_request(9, instance_request, sizeof(instance_request)));
    CHECK(reply_status(APP_INSTANCE_STATE_EXITED, APP_EXIT_KIND_SIGNALED, -15));
    test_pump(app, 20);
    CHECK(call_count == 4 && calls[3].status.state == APP_INSTANCE_STATE_EXITED);
    CHECK(calls[3].status.exit_kind == APP_EXIT_KIND_SIGNALED && calls[3].status.exit_value == -15);

    CHECK(app_apps_stop(app, 77, record, NULL, &id) == CORE_OK && id == 5);
    settle(app);
    CHECK(take_request(7, instance_request, sizeof(instance_request)));
    CHECK(reply_error(8, CORE_OK));
    test_pump(app, 20);
    CHECK(call_count == 5 && calls[4].error == CORE_OK && calls[4].has_result);
    CHECK(calls[4].operation == APP_APPS_OPERATION_STOP);
    CHECK(app->appserv.count == 0);
    CHECK(app_destroy(&app) == CORE_OK);
    CHECK(call_count == 5); /* nothing was pending: no extra callbacks */
}

static void fifo_and_errors(void)
{
    AppHandle app = start();
    AppRequestId first;
    AppRequestId second;
    AppRequestId third;

    CHECK(app != NULL);
    first = quick(app, APP_APPS_OPERATION_LIST);
    second = quick(app, APP_APPS_OPERATION_GET_STATUS);
    third = quick(app, APP_APPS_OPERATION_STOP);
    CHECK(first == 1 && second == 2 && third == 3);
    settle(app);
    CHECK(app->appserv.count == 3);
    CHECK(take_request(1, NULL, 0));
    CHECK(take_request(9, (const u8 *)"\1\0M\0\0\0\0\0\0\0", 10));
    CHECK(take_request(7, (const u8 *)"\1\0M\0\0\0\0\0\0\0", 10));
    /* the replies go out in the same order, and so do the callbacks */
    CHECK(reply_list_two() && reply_status(APP_INSTANCE_STATE_RUNNING, 0, 0) &&
          reply_error(8, CORE_OK));
    test_pump(app, 20);
    CHECK(call_count == 3);
    CHECK(calls[0].request == first && calls[1].request == second && calls[2].request == third);
    CHECK(calls[0].operation == APP_APPS_OPERATION_LIST &&
          calls[1].operation == APP_APPS_OPERATION_GET_STATUS &&
          calls[2].operation == APP_APPS_OPERATION_STOP);

    /* errors of the service pass through; the connection stays */
    CHECK(quick(app, APP_APPS_OPERATION_LAUNCH) == 4);
    CHECK(quick(app, APP_APPS_OPERATION_GET_STATUS) == 5);
    CHECK(quick(app, APP_APPS_OPERATION_STOP) == 6);
    settle(app);
    CHECK(wait_requests(app, 3) == 3);
    {
        u8 with_extra[9] = { 0 };

        put_le32(with_extra, CORE_NOT_FOUND);
        CHECK(fake_peer_send(&appserv, 6, with_extra, sizeof(with_extra)));
    }
    CHECK(reply_error(10, CORE_UNSUPPORTED));
    CHECK(reply_error(8, CORE_INVALID_STATE));
    test_pump(app, 20);
    CHECK(call_count == 6);
    CHECK(calls[3].error == CORE_NOT_FOUND && !calls[3].has_result);
    CHECK(calls[4].error == CORE_UNSUPPORTED && !calls[4].has_result);
    CHECK(calls[5].error == CORE_INVALID_STATE && !calls[5].has_result);
    CHECK(app->appserv.connection != NULL && app->appserv.open);
    CHECK(app_destroy(&app) == CORE_OK);
}

/* ---- arguments and states ------------------------------------------------------------- */

static i32 foreign_thread(void *argument)
{
    AppHandle app = argument;
    ErrorCode a = app_apps_list(app, record, NULL, NULL);
    ErrorCode b = app_apps_stop(app, 1, record, NULL, NULL);

    return a == CORE_INVALID_STATE && b == CORE_INVALID_STATE ? 0 : 1;
}

static void argument_and_state_checks(void)
{
    AppHandle app = start();
    AppRequestId id = 99;
    char id65[66];

    for (usize i = 0; i < 65; ++i) {
        id65[i] = 'i';
    }
    id65[65] = '\0';
    CHECK(app != NULL);
    CHECK(app_apps_list(NULL, record, NULL, &id) == CORE_INVALID_ARGUMENT);
    CHECK(app_apps_list(app, NULL, NULL, &id) == CORE_INVALID_ARGUMENT);
    CHECK(app_apps_get_info(app, NULL, record, NULL, &id) == CORE_INVALID_ARGUMENT);
    CHECK(app_apps_get_info(app, "", record, NULL, &id) == CORE_INVALID_ARGUMENT);
    CHECK(app_apps_get_info(app, id65, record, NULL, &id) == CORE_INVALID_ARGUMENT);
    CHECK(app_apps_get_info(app, "\xC0\x80", record, NULL, &id) == CORE_INVALID_ARGUMENT);
    CHECK(app_apps_get_info(app, "clock", NULL, NULL, &id) == CORE_INVALID_ARGUMENT);
    CHECK(app_apps_launch(app, NULL, record, NULL, &id) == CORE_INVALID_ARGUMENT);
    CHECK(app_apps_launch(app, "", record, NULL, &id) == CORE_INVALID_ARGUMENT);
    CHECK(app_apps_launch(app, "\xED\xA0\x80", record, NULL, &id) == CORE_INVALID_ARGUMENT);
    CHECK(app_apps_stop(app, 0, record, NULL, &id) == CORE_INVALID_ARGUMENT);
    CHECK(app_apps_stop(app, 1, NULL, NULL, &id) == CORE_INVALID_ARGUMENT);
    CHECK(app_apps_get_status(app, 0, record, NULL, &id) == CORE_INVALID_ARGUMENT);
    CHECK(app_apps_get_status(app, 1, NULL, NULL, &id) == CORE_INVALID_ARGUMENT);
    CHECK(id == 99);                          /* untouched on failure */
    CHECK(app->appserv.connection == NULL);   /* nothing connected for a bad request */
    {
        char id64[65];

        for (usize i = 0; i < 64; ++i) {
            id64[i] = 'k';
        }
        id64[64] = '\0';
        CHECK(app_apps_launch(app, id64, record, NULL, &id) == CORE_OK && id == 1);
        CHECK(app_destroy(&app) == CORE_OK);  /* ends the request */
        CHECK(call_count == 1 && calls[0].error == CORE_INVALID_STATE);
    }

    app = start();
    CHECK(app != NULL && test_run_on_thread(foreign_thread, app));
    CHECK(app_request_quit(app) == CORE_OK && app_run(app) == CORE_OK);
    CHECK(app_apps_list(app, record, NULL, &id) == CORE_INVALID_STATE); /* Stopping */
    CHECK(app_apps_stop(app, 1, record, NULL, &id) == CORE_INVALID_STATE);
    CHECK(app_destroy(&app) == CORE_OK);
}

/* ---- Connecting ----------------------------------------------------------------------------- */

static void connecting_queue(void)
{
    AppHandle app = start();

    CHECK(app != NULL);
    fault_plan.connecting_polls = 3;
    CHECK(quick(app, APP_APPS_OPERATION_LIST) == 1);
    CHECK(quick(app, APP_APPS_OPERATION_LAUNCH) == 2);
    CHECK(app->appserv.connection != NULL && !app->appserv.open);
    CHECK(app->appserv.count == 2 && !app->appserv.requests[0].sent);
    CHECK(fake_peer_accept(&appserv));
    CHECK(take_any_requests() == 0);              /* nothing before Open */
    test_pump(app, 50);
    CHECK(fault_plan.connecting_polls == 0 && app->appserv.open);
    CHECK(take_request(1, NULL, 0));              /* in the order they were made */
    CHECK(take_request(5, (const u8 *)"\1\0\5\0clock", 9));
    CHECK(app->appserv.requests[0].sent && app->appserv.requests[1].sent);
    CHECK(reply_list_two() && reply_launch(9));
    test_pump(app, 20);
    CHECK(call_count == 2 && calls[0].request == 1 && calls[1].request == 2);
    CHECK(calls[1].instance == 9);
    CHECK(app_destroy(&app) == CORE_OK);
}

/* ---- limits ---------------------------------------------------------------------------------- */

static void limits(void)
{
    AppHandle app = start();
    AppRequestId id = 0;
    u32 accepted = 0;
    ErrorCode last = CORE_OK;

    CHECK(app != NULL);
    while (accepted < 1000 && (last = app_apps_list(app, record, NULL, &id)) == CORE_OK) {
        accepted++;
        
    }
    CHECK(last == CORE_WOULD_BLOCK && accepted == APP_APPS_MAX_PENDING);
    CHECK(id == APP_APPS_MAX_PENDING);              /* the refused one took no number */
    settle(app);
    CHECK(wait_requests(app, APP_APPS_MAX_PENDING) == APP_APPS_MAX_PENDING);
    for (u32 i = 0; i < 3; ++i) {
        CHECK(reply_error(2, CORE_NOT_FOUND));
    }
    test_pump(app, 20);
    CHECK(call_count == 3 && calls[2].request == 3);
    CHECK(app_apps_list(app, record, NULL, &id) == CORE_OK && id == APP_APPS_MAX_PENDING + 1);
    CHECK(app_apps_list(app, record, NULL, &id) == CORE_OK && id == APP_APPS_MAX_PENDING + 2);
    CHECK(app_apps_list(app, record, NULL, &id) == CORE_OK && id == APP_APPS_MAX_PENDING + 3);
    CHECK(app_apps_list(app, record, NULL, &id) == CORE_WOULD_BLOCK);
    settle(app);
    CHECK(wait_requests(app, 3) == 3);
    for (u32 i = 0; i < 256; ++i) {
        bool sent = false;

        /* The peer writes faster than the application reads when the socket
         * buffer is small: let the application run and try again. */
        for (u32 attempt = 0; attempt < 100 && !sent; ++attempt) {
            sent = reply_error(2, CORE_NOT_FOUND);
            if (!sent) {
                test_pump(app, 20);
            }
        }
        if (!sent) {
            fail_at(__LINE__);
        }
    }
    for (u32 round = 0; round < 150 && call_count < 3 + 256; ++round) {
        bool pending = false;

        if (appserv.connection != NULL) {
            (void)ipc_connection_flush(appserv.connection, &pending); /* the tail of the replies */
        }
        test_pump(app, 20);
    }
    CHECK(call_count == 3 + 256);
    for (u32 i = 0; i < call_count; ++i) {
        if (calls[i < 300 ? i : 299].request != i + 1) {
            fail_at(__LINE__);
        }
    }
    CHECK(app_destroy(&app) == CORE_OK);
}

/* ipclib refuses a message: CORE_OVERFLOW is CORE_WOULD_BLOCK, other errors are
 * passed on; the request is not queued and takes no number. */
static void send_refusals(void)
{
    AppHandle app = start();
    AppRequestId id = 0;

    CHECK(app != NULL);
    CHECK(quick(app, APP_APPS_OPERATION_LIST) == 1);  /* opens the connection */
    fault_plan.send_error = CORE_OVERFLOW;
    CHECK(app_apps_list(app, record, NULL, &id) == CORE_WOULD_BLOCK);
    fault_plan.send_error = CORE_OUT_OF_MEMORY;
    CHECK(app_apps_list(app, record, NULL, &id) == CORE_OUT_OF_MEMORY);
    CHECK(app->appserv.count == 1 && app->appserv.open && app->appserv.connection != NULL);
    CHECK(app_apps_list(app, record, NULL, &id) == CORE_OK && id == 2);
    CHECK(app->appserv.count == 2);
    CHECK(app_destroy(&app) == CORE_OK);
    CHECK(call_count == 2);
}

/* ---- violations and failures ------------------------------------------------------------------- */

/* Three requests are pending when `breakage` happens; every one must end with
 * CORE_IO_ERROR, in order, and a new request must start a new connection. */
typedef enum Breakage {
    BREAK_WRONG_TYPE,
    BREAK_UNEXPECTED_REPLY,
    BREAK_MALFORMED,
    BREAK_BAD_ERROR_CODE,
    BREAK_PEER_CLOSED,
    BREAK_PARTIAL_FRAME_THEN_CLOSE,
    BREAK_HUGE_FRAME
} Breakage;

static void broken_connection(Breakage breakage, bool violation)
{
    AppHandle app = start();
    IpcMessage message = { 0, { NULL, 0 } };
    u8 raw[8] = { 4, 0, 0, 0, 2, 0, 0, 0 };
    u8 huge[8] = { 0xff, 0xff, 0xff, 0x7f, 2, 0, 0, 0 };
    usize expected_calls = 3;

    CHECK(app != NULL);
    CHECK(quick(app, APP_APPS_OPERATION_LIST) != 0);
    CHECK(quick(app, APP_APPS_OPERATION_GET_STATUS) != 0);
    CHECK(quick(app, APP_APPS_OPERATION_STOP) != 0);
    settle(app);
    CHECK(wait_requests(app, 3) == 3);
    switch (breakage) {
    case BREAK_WRONG_TYPE:
        {
            u8 empty_list[6] = { 0 };

            /* a reply of the wrong kind whose bytes would be a fine list */
            CHECK(fake_peer_send(&appserv, 4, empty_list, sizeof(empty_list)));
        }
        break;
    case BREAK_UNEXPECTED_REPLY:
        CHECK(reply_list_two() && reply_status(APP_INSTANCE_STATE_RUNNING, 0, 0) &&
              reply_error(8, CORE_OK));
        {
            u8 empty_list[6] = { 0 };

            /* a fourth reply that would suit an empty queue slot, nobody asked */
            CHECK(fake_peer_send(&appserv, 2, empty_list, sizeof(empty_list)));
        }
        expected_calls = 3;
        break;
    case BREAK_MALFORMED: {
        u8 shorter[3] = { 0, 0, 0 };

        CHECK(fake_peer_send(&appserv, 2, shorter, sizeof(shorter)));
        break;
    }
    case BREAK_BAD_ERROR_CODE:
        CHECK(reply_error(2, (ErrorCode)99));
        break;
    case BREAK_PEER_CLOSED:
        fake_peer_drop(&appserv);
        break;
    case BREAK_PARTIAL_FRAME_THEN_CLOSE:
        CHECK(fake_peer_write_raw(&appserv, raw, 6));
        fake_peer_drop(&appserv);
        break;
    case BREAK_HUGE_FRAME:
        CHECK(fake_peer_write_raw(&appserv, huge, sizeof(huge)));
        break;
    }
    test_pump(app, 40);
    CHECK(call_count == expected_calls);
    if (breakage == BREAK_UNEXPECTED_REPLY) {
        /* the three real replies were delivered, then the fourth broke it */
        CHECK(calls[0].error == CORE_OK && calls[1].error == CORE_OK && calls[2].error == CORE_OK);
    } else {
        CHECK(calls[0].error == CORE_IO_ERROR && calls[1].error == CORE_IO_ERROR &&
              calls[2].error == CORE_IO_ERROR);
        CHECK(!calls[0].has_result && !calls[1].has_result && !calls[2].has_result);
        CHECK(calls[0].request == 1 && calls[1].request == 2 && calls[2].request == 3);
    }
    CHECK(app->appserv.connection == NULL && app->appserv.count == 0);
    CHECK(app->appserv.source == 0);
    CHECK(fake_peer_receive(&appserv, &message) == -1 || breakage == BREAK_PEER_CLOSED ||
          breakage == BREAK_PARTIAL_FRAME_THEN_CLOSE);
    CHECK(log_mentions(violation ? "appserv protocol violation" : "appserv connection failed"));
    /* The next request makes a new connection; the old ones are not resent. */
    {
        AppRequestId id = quick(app, APP_APPS_OPERATION_GET_INFO);

        CHECK(id == 4);
        settle(app);
        CHECK(take_request(3, (const u8 *)"\1\0\5\0clock", 9));
        CHECK(take_any_requests() == 0);
        CHECK(reply_info("clock", "Clock"));
        test_pump(app, 20);
        CHECK(call_count == expected_calls + 1 && calls[expected_calls].request == 4 &&
              calls[expected_calls].error == CORE_OK);
    }
    CHECK(app_destroy(&app) == CORE_OK);
}

static void failures(void)
{
    broken_connection(BREAK_WRONG_TYPE, true);
    broken_connection(BREAK_UNEXPECTED_REPLY, true);
    broken_connection(BREAK_MALFORMED, true);
    broken_connection(BREAK_BAD_ERROR_CODE, true);
    broken_connection(BREAK_PEER_CLOSED, false);
    broken_connection(BREAK_PARTIAL_FRAME_THEN_CLOSE, false);
    broken_connection(BREAK_HUGE_FRAME, false);
}

/* A socket error while writing: the earlier requests are ended, the new one is
 * not accepted and gets no callback. */
static void flush_failure(void)
{
    AppHandle app = start();
    AppRequestId id = 0;

    CHECK(app != NULL && quick(app, APP_APPS_OPERATION_LIST) == 1);
    fault_plan.flush_error = CORE_IO_ERROR;
    CHECK(app_apps_list(app, record, NULL, &id) == CORE_IO_ERROR);
    CHECK(call_count == 1 && calls[0].request == 1 && calls[0].error == CORE_IO_ERROR);
    CHECK(app->appserv.connection == NULL && app->appserv.count == 0);
    CHECK(quick(app, APP_APPS_OPERATION_LIST) == 2);  /* the failed one took no number */
    CHECK(app_destroy(&app) == CORE_OK);
    CHECK(call_count == 2 && calls[1].error == CORE_INVALID_STATE);
}

/* ---- no appserv -------------------------------------------------------------------------------- */

static void unavailable(void)
{
    AppHandle app;
    AppRequestId id = 0;

    fake_peer_stop(&appserv);
    app = start();
    CHECK(app != NULL);
    CHECK(app_apps_list(app, record, NULL, &id) == CORE_NOT_FOUND);
    CHECK(id == 0 && app->appserv.count == 0 && app->appserv.connection == NULL);
    CHECK(app_apps_launch(app, "clock", record, NULL, &id) == CORE_NOT_FOUND);
    CHECK(log_mentions("appserv unavailable"));
    CHECK(fake_peer_start(&appserv, APPSERV_PATH));
    CHECK(app_apps_list(app, record, NULL, &id) == CORE_OK && id == 1); /* tries again */
    settle(app);
    CHECK(take_request(1, NULL, 0));
    CHECK(app_destroy(&app) == CORE_OK);
    CHECK(call_count == 1 && calls[0].error == CORE_INVALID_STATE);
}

/* ---- app_destroy() ------------------------------------------------------------------------------- */

typedef struct DestroyProbe {
    ErrorCode list;
    ErrorCode destroy;
    ErrorCode log;
    ErrorCode remove_other;
    u32 calls;
    u32 order[8];
} DestroyProbe;

static AppHandle probe_app;

static void probing_callback(AppHandle app, AppRequestId request, ErrorCode error,
                             const AppAppsResult *result, void *context)
{
    DestroyProbe *probe = context;
    AppHandle copy = app;

    (void)result;
    probe->order[probe->calls < 8 ? probe->calls : 7] = (u32)request;
    probe->calls++;
    if (error != CORE_INVALID_STATE) {
        probe->list = CORE_OK;
        return;
    }
    probe->list = app_apps_list(app, record, NULL, NULL);
    probe->destroy = app_destroy(&copy);
    probe->log = app_log(app, APP_LOG_LEVEL_INFO, "from callback");
    probe_app = app;
}

static void destroy_with_pending(void)
{
    AppHandle app = start();
    DestroyProbe probe = { 0 };
    IpcMessage message = { 0, { NULL, 0 } };

    CHECK(app != NULL);
    CHECK(app_apps_list(app, probing_callback, &probe, NULL) == CORE_OK);
    CHECK(app_apps_get_status(app, 5, probing_callback, &probe, NULL) == CORE_OK);
    CHECK(app_apps_stop(app, 5, probing_callback, &probe, NULL) == CORE_OK);
    settle(app);
    CHECK(wait_requests(app, 3) == 3);
    CHECK(app_destroy(&app) == CORE_OK && app == NULL);
    CHECK(probe.calls == 3);                                  /* once each */
    CHECK(probe.order[0] == 1 && probe.order[1] == 2 && probe.order[2] == 3);
    CHECK(probe.list == CORE_INVALID_STATE);                  /* Stopping: no new requests */
    CHECK(probe.destroy == CORE_INVALID_STATE);               /* no second destroy */
    CHECK(probe.log == CORE_INVALID_STATE);
    CHECK(fake_peer_receive(&appserv, &message) == -1);       /* the connection is closed */
    CHECK(call_count == 0);                                   /* the probe never reached record() */
}

/* A callback that makes the next request: it is sent and answered in turn. */
static AppRequestId chained_id;

static void chaining_callback(AppHandle app, AppRequestId request, ErrorCode error,
                              const AppAppsResult *result, void *context)
{
    (void)request;
    (void)error;
    (void)result;
    (void)context;
    chained_id = quick(app, APP_APPS_OPERATION_STOP);
}

static void chained_request(void)
{
    AppHandle app = start();

    chained_id = 0;
    CHECK(app != NULL);
    CHECK(app_apps_list(app, chaining_callback, NULL, NULL) == CORE_OK);
    settle(app);
    CHECK(take_request(1, NULL, 0) && reply_list_two());
    test_pump(app, 20);
    CHECK(chained_id == 2 && app->appserv.count == 1);
    settle(app);
    CHECK(take_request(7, (const u8 *)"\1\0M\0\0\0\0\0\0\0", 10));
    CHECK(reply_error(8, CORE_OK));
    test_pump(app, 20);
    CHECK(call_count == 1 && calls[0].request == 2 && app->appserv.count == 0);
    CHECK(app_destroy(&app) == CORE_OK);
}

/* ---- memory ---------------------------------------------------------------------------------------- */

/* Memory for the copy of an answer runs out: that request ends with
 * CORE_OUT_OF_MEMORY and the connection survives. */
static void answer_without_memory(void)
{
    bool found = false;

    for (u32 n = 1; n <= 12 && !found; ++n) {
        AppHandle app = start();
        u32 before;

        CHECK(app != NULL && quick(app, APP_APPS_OPERATION_LIST) == 1);
        settle(app);
        CHECK(take_request(1, NULL, 0) && reply_list_two());
        before = call_count;
        fault_plan.fail_allocation_at = n;
        test_pump(app, 20);
        fault_plan.fail_allocation_at = 0;
        if (call_count == before + 1 && calls[before].error == CORE_OUT_OF_MEMORY &&
            !calls[before].has_result && app->appserv.connection != NULL) {
            found = true;
            CHECK(quick(app, APP_APPS_OPERATION_GET_INFO) == 2);
            settle(app);
            CHECK(take_request(3, (const u8 *)"\1\0\5\0clock", 9) && reply_info("a", "b"));
            test_pump(app, 20);
            CHECK(calls[before + 1].error == CORE_OK && same_text(calls[before + 1].first_id, "a"));
        }
        CHECK(app_destroy(&app) == CORE_OK);
        (void)take_any_requests();
    }
    CHECK(found);
}

static void no_leaks(void)
{
    i64 baseline = 0;

    for (u32 round = 0; round < 30; ++round) {
        AppHandle app = start();

        if (app == NULL || quick(app, APP_APPS_OPERATION_LIST) == 0) {
            fail_at(__LINE__);
        }
        settle(app);
        (void)take_any_requests();
        (void)reply_list_two();
        test_pump(app, 20);
        (void)quick(app, APP_APPS_OPERATION_STOP); /* left pending on purpose */
        if (app_destroy(&app) != CORE_OK) {
            fail_at(__LINE__);
        }
        if (round == 2) {
            baseline = (i64)fault_plan.allocations - (i64)fault_plan.frees;
        }
    }
    CHECK((i64)fault_plan.allocations - (i64)fault_plan.frees == baseline);
}

int main(void)
{
    const char *saved_apps = libappframework_internal_appserv_endpoint;
    const char *saved_log = libappframework_internal_logserv_endpoint;

    libappframework_internal_appserv_endpoint = APPSERV_PATH;
    libappframework_internal_logserv_endpoint = LOGSERV_PATH;
    CHECK(fake_peer_start(&appserv, APPSERV_PATH) && fake_peer_start(&logserv, LOGSERV_PATH));
    round_trips();
    fifo_and_errors();
    argument_and_state_checks();
    connecting_queue();
    limits();
    send_refusals();
    failures();
    flush_failure();
    unavailable();
    destroy_with_pending();
    chained_request();
    answer_without_memory();
    no_leaks();
    fake_peer_stop(&appserv);
    fake_peer_stop(&logserv);
    libappframework_internal_appserv_endpoint = saved_apps;
    libappframework_internal_logserv_endpoint = saved_log;
    check_passed();
    return 0;
}
