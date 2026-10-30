/*
 * Участник сквозного теста messaging (L4, messaging_integration.py): один бинарник
 * в пяти режимах, каждый - настоящее приложение на appframework, которое
 * подключается к настоящему msgserv.
 *
 *   publish  публикует "first" по маркеру go1 и "second" по маркеру go2
 *   listen   печатает события topic news
 *   serve    отвечает на "echo" из таймера: сначала публикует событие "between",
 *            потом отвечает; на "hang" не отвечает
 *   ask      подписан на news, шлёт echo и выходит после ответа
 *   dangling шлёт запрос, пока msgserv не поднят, и делает destroy: запрос, ещё
 *            не принятый брокером, получает CORE_INVALID_STATE ровно один раз
 *   nosecret запускается без PUNOS_MSG_SECRET и печатает код app_msg_start
 *
 * argv[1] режим, argv[2] endpoint msgserv, argv[3] каталог маркеров, argv[4]
 * application_id. Секрет приходит через envp, как у настоящего приложения.
 * Результаты печатаются строками в stdout сразу, без буферизации.
 */

#include <appframework/app.h>
#include <punos/syscall/constants.h>
#include <punos/syscall/syscall.h>

#include "internal.h"

#define PEER_PATH_MAX 256
#define PEER_MODE_PUBLISH 1
#define PEER_MODE_LISTEN 2
#define PEER_MODE_SERVE 3
#define PEER_MODE_ASK 4
#define PEER_MODE_NOSECRET 5
#define PEER_MODE_DANGLING 6

typedef struct Peer {
    int mode;
    const char *directory;
    AppMsgCorrelationId correlation;
    u32 responses;
    bool sent_first;
    bool sent_second;
} Peer;

static Peer peer;
static AppEventSourceId timer_source;

static usize text_length(const char *text)
{
    usize length = 0;

    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static bool text_equal(const char *left, const char *right)
{
    usize i = 0;

    while (left[i] != '\0' && left[i] == right[i]) {
        i++;
    }
    return left[i] == right[i];
}

static void say(const char *text)
{
    (void)write(1, text, text_length(text));
}

static void say_bytes(const u8 *data, usize size)
{
    if (size > 0) {
        (void)write(1, data, size);
    }
}

static void say_number(u64 value)
{
    char digits[24];
    usize at = sizeof(digits);

    do {
        digits[--at] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    (void)write(1, digits + at, sizeof(digits) - at);
}

static const char *error_name(ErrorCode error)
{
    switch (error) {
    case CORE_OK:
        return "OK";
    case CORE_INVALID_STATE:
        return "INVALID_STATE";
    case CORE_UNSUPPORTED:
        return "UNSUPPORTED";
    case CORE_TIMEOUT:
        return "TIMEOUT";
    default:
        return "OTHER";
    }
}

static void path_join(char *out, const char *directory, const char *name)
{
    usize length = text_length(directory);
    usize tail = text_length(name);

    for (usize i = 0; i < length; ++i) {
        out[i] = directory[i];
    }
    out[length] = '/';
    for (usize i = 0; i < tail; ++i) {
        out[length + 1 + i] = name[i];
    }
    out[length + 1 + tail] = '\0';
}

static bool marker_exists(const char *name)
{
    char path[PEER_PATH_MAX];
    i32 fd;

    path_join(path, peer.directory, name);
    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        return false;
    }
    (void)close(fd);
    return true;
}

static void marker_create(const char *name)
{
    char path[PEER_PATH_MAX];
    i32 fd;

    path_join(path, peer.directory, name);
    fd = open(path, O_WRONLY | O_CREAT, 0600);
    if (fd >= 0) {
        (void)close(fd);
    }
}

static void on_quiet(AppHandle app, AppRequestId request, ErrorCode error,
                     const AppMsgResult *result, void *context)
{
    (void)app;
    (void)request;
    (void)error;
    (void)result;
    (void)context;
}

static void on_published(AppHandle app, AppRequestId request, ErrorCode error,
                         const AppMsgResult *result, void *context)
{
    (void)app;
    (void)request;
    (void)context;
    say("published ");
    if (error != CORE_OK) {
        say("error=");
        say(error_name(error));
    } else {
        say("delivered=");
        say_number(result->delivered_count);
        say(" dropped=");
        say_number(result->dropped_count);
    }
    say("\n");
}

static void on_dangling(AppHandle app, AppRequestId request, ErrorCode error,
                        const AppMsgResult *result, void *context)
{
    (void)app;
    (void)request;
    (void)result;
    (void)context;
    say("destroy-callback error=");
    say(error_name(error));
    say("\n");
}

static void on_link(AppHandle app, ErrorCode error, AppMsgClientId client, void *context)
{
    (void)app;
    (void)client;
    (void)context;
    if (error == CORE_OK) {
        say("linked\n");
        if (peer.mode == PEER_MODE_SERVE) {
            marker_create("ready-serve");
        }
    } else {
        say("refused error=");
        say(error_name(error));
        say(" code=");
        say_number(error);
        say("\n");
    }
}

static void on_event(AppHandle app, const AppMsgEvent *event, void *context)
{
    (void)app;
    (void)context;
    say("event ");
    say(event->topic);
    say(" ");
    say_bytes(event->payload.data, event->payload.size);
    say("\n");
}

static void on_answer_later(AppHandle app, AppEventSourceId source, EventMask mask,
                            void *context)
{
    static const u8 between[] = "between";
    static const u8 pong[] = "pong";

    (void)source;
    (void)mask;
    (void)context;
    /* Событие, пришедшее между запросом и ответом, не должно стать ответом. */
    (void)app_msg_publish(app, "news", (Bytes){ (u8 *)between, sizeof(between) - 1 },
                          on_quiet, NULL, NULL);
    (void)app_msg_respond(app, peer.correlation, 7, (Bytes){ (u8 *)pong, sizeof(pong) - 1 },
                          on_quiet, NULL, NULL);
    say("timer respond\n");
}

static void on_incoming(AppHandle app, const AppMsgIncoming *incoming, void *context)
{
    (void)context;
    if (text_equal(incoming->method, "echo")) {
        say("handler echo\n");
        peer.correlation = incoming->correlation;
        (void)app_event_create_timer(app, 10, 0, on_answer_later, NULL, &timer_source);
    }
}

static void on_quit_later(AppHandle app, AppEventSourceId source, EventMask mask,
                          void *context)
{
    (void)source;
    (void)mask;
    (void)context;
    (void)app_request_quit(app);
}

static void on_response(AppHandle app, const AppMsgAnswer *answer, void *context)
{
    (void)context;
    say("response error=");
    say(error_name(answer->error));
    say(" status=");
    say_number(answer->status);
    say(" payload=");
    say_bytes(answer->payload.data, answer->payload.size);
    say("\n");
    peer.responses++;
    if (peer.responses == 1 && answer->error == CORE_OK) {
        (void)app_event_create_timer(app, 50, 0, on_quit_later, NULL, &timer_source);
    }
}

static void on_poll(AppHandle app, AppEventSourceId source, EventMask mask, void *context)
{
    static const u8 first[] = "first";
    static const u8 second[] = "second";

    (void)source;
    (void)mask;
    (void)context;
    if (marker_exists("stop")) {
        (void)app_request_quit(app);
        return;
    }
    if (peer.mode != PEER_MODE_PUBLISH) {
        return;
    }
    if (!peer.sent_first && marker_exists("go1")) {
        peer.sent_first = true;
        (void)app_msg_publish(app, "news", (Bytes){ (u8 *)first, sizeof(first) - 1 },
                              on_published, NULL, NULL);
    }
    if (!peer.sent_second && marker_exists("go2")) {
        peer.sent_second = true;
        (void)app_msg_publish(app, "news", (Bytes){ (u8 *)second, sizeof(second) - 1 },
                              on_published, NULL, NULL);
    }
}

static int mode_of(const char *name)
{
    if (text_equal(name, "publish")) {
        return PEER_MODE_PUBLISH;
    }
    if (text_equal(name, "listen")) {
        return PEER_MODE_LISTEN;
    }
    if (text_equal(name, "serve")) {
        return PEER_MODE_SERVE;
    }
    if (text_equal(name, "ask")) {
        return PEER_MODE_ASK;
    }
    if (text_equal(name, "nosecret")) {
        return PEER_MODE_NOSECRET;
    }
    if (text_equal(name, "dangling")) {
        return PEER_MODE_DANGLING;
    }
    return 0;
}

int main(int argc, char **argv, char **envp)
{
    AppConfig config;
    AppHandle app = NULL;
    AppMsgHandlers handlers = { on_event, on_incoming, on_response, on_link, NULL };
    ErrorCode result;
    ErrorCode run_result;

    if (argc != 5 || mode_of(argv[1]) == 0) {
        return 90;
    }
    peer.mode = mode_of(argv[1]);
    libappframework_internal_msgserv_endpoint = argv[2];
    libappframework_internal_logserv_endpoint = "/nonexistent/punos-test-logserv.sock";
    peer.directory = argv[3];
    config.application_id = argv[4];
    config.log_component = argv[4];
    if (app_create(&config, &app) != CORE_OK) {
        return 91;
    }

    if (peer.mode == PEER_MODE_NOSECRET) {
        result = app_msg_start(app, envp, &handlers);
        say("start=");
        say(error_name(result));
        say("\n");
        (void)app_destroy(&app);
        return 0;
    }

    result = app_msg_start(app, envp, &handlers);
    if (result != CORE_OK) {
        return 92;
    }
    if (peer.mode == PEER_MODE_DANGLING) {
        /* msgserv не поднят: запрос остаётся в очереди до bind и не принят брокером. */
        (void)app_msg_request_application(app, "srv", "echo", (Bytes){ (u8 *)"late", 4 },
                                          on_dangling, NULL, NULL);
        say("queued\n");
        (void)app_destroy(&app);
        return 0;
    }
    if (peer.mode == PEER_MODE_LISTEN) {
        (void)app_msg_subscribe(app, "news", on_quiet, NULL, NULL);
    }
    if (peer.mode == PEER_MODE_ASK) {
        (void)app_msg_subscribe(app, "news", on_quiet, NULL, NULL);
        (void)app_msg_request_application(app, "srv", "echo",
                                          (Bytes){ (u8 *)"ping", 4 }, on_quiet, NULL, NULL);
    }
    (void)app_event_create_timer(app, 20, 20, on_poll, NULL, &timer_source);

    run_result = app_run(app);
    if (app_destroy(&app) != CORE_OK) {
        return 93;
    }
    return run_result == CORE_OK ? 0 : 94;
}
