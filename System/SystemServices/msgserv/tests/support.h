#ifndef MSGSERV_TESTS_SUPPORT_H
#define MSGSERV_TESTS_SUPPORT_H

/*
 * Общая обвязка юнит-тестов msgserv: настоящий брокер на сокете в /tmp,
 * настоящий event loop и настоящие соединения ipclib, но без main.c. Время
 * тесты задают сами, поэтому дедлайны проверяются без ожидания.
 *
 * Тест чередует test_turn() (один проход брокера) с вызовами клиента. Все
 * операции неблокирующие.
 */

#include <msgserv.h>
#include <punos/syscall/syscall.h>

#include "wire.h"

#define TEST_ALPHA_SECRET "alpha-secret-0123456789"
#define TEST_BETA_SECRET "beta-secret-9876543210"

/* Каталог тестов: alpha публикует ui.theme и зовёт beta, beta слушает
 * ui.theme и обслуживает open_image. Права нарочно несимметричны, чтобы
 * отказ по правам отличался от отказа по адресу. */
static const String test_alpha_publish[] = { { (char *)"ui.theme", 8 } };
static const String test_alpha_subscribe[] = { { (char *)"app.state", 9 } };
static const String test_alpha_serve[] = { { (char *)"ping", 4 } };
static const String test_alpha_targets[] = { { (char *)"beta", 4 } };
static const String test_beta_publish[] = { { (char *)"app.state", 9 } };
static const String test_beta_subscribe[] = { { (char *)"ui.theme", 8 } };
static const String test_beta_serve[] = { { (char *)"open_image", 10 } };
static const String test_beta_targets[] = { { (char *)"alpha", 5 } };

#define TEST_GAMMA_SECRET "gamma-secret-5555555555"

/* gamma существует ради лимитов: прав на подписку у неё на одну больше, чем
 * MSG_SUBSCRIPTIONS_MAX_PER_CLIENT, поэтому упор в лимит отличим от отказа
 * по правам. */
static const String test_gamma_subscribe[] = {
    { (char *)"t0", 2 },
    { (char *)"t1", 2 },
    { (char *)"t2", 2 },
    { (char *)"t3", 2 },
    { (char *)"t4", 2 },
    { (char *)"t5", 2 },
    { (char *)"t6", 2 },
    { (char *)"t7", 2 },
    { (char *)"t8", 2 },
    { (char *)"t9", 2 },
    { (char *)"t10", 3 },
    { (char *)"t11", 3 },
    { (char *)"t12", 3 },
    { (char *)"t13", 3 },
    { (char *)"t14", 3 },
    { (char *)"t15", 3 },
    { (char *)"t16", 3 },
    { (char *)"t17", 3 },
    { (char *)"t18", 3 },
    { (char *)"t19", 3 },
    { (char *)"t20", 3 },
    { (char *)"t21", 3 },
    { (char *)"t22", 3 },
    { (char *)"t23", 3 },
    { (char *)"t24", 3 },
    { (char *)"t25", 3 },
    { (char *)"t26", 3 },
    { (char *)"t27", 3 },
    { (char *)"t28", 3 },
    { (char *)"t29", 3 },
    { (char *)"t30", 3 },
    { (char *)"t31", 3 },
    { (char *)"t32", 3 }
};

static const MsgAppEntry test_entries[] = {
    { { (char *)"alpha", 5 }, { (char *)TEST_ALPHA_SECRET, sizeof(TEST_ALPHA_SECRET) - 1 },
      test_alpha_publish, 1, test_alpha_subscribe, 1, test_alpha_serve, 1,
      test_alpha_targets, 1 },
    { { (char *)"beta", 4 }, { (char *)TEST_BETA_SECRET, sizeof(TEST_BETA_SECRET) - 1 },
      test_beta_publish, 1, test_beta_subscribe, 1, test_beta_serve, 1, test_beta_targets, 1 },
    { { (char *)"gamma", 5 }, { (char *)TEST_GAMMA_SECRET, sizeof(TEST_GAMMA_SECRET) - 1 },
      NULL, 0, test_gamma_subscribe, 33, NULL, 0, NULL, 0 },
};

static const MsgCatalog test_catalog = { test_entries, 3 };

typedef struct TestBroker {
    EventLoopHandle loop;
    MsgBroker broker;
    u64 clock_ms;
    usize closed; /* сколько соединений брокер закрыл сам */
} TestBroker;

static inline bool test_broker_start(TestBroker *test, const char *path)
{
    String endpoint = { (char *)path, 0 };
    i32 fd = -1;

    while (path[endpoint.length] != '\0') {
        endpoint.length++;
    }
    (void)unlink(path);
    test->clock_ms = 1000;
    test->closed = 0;
    if (eventloop_create(&test->loop) != CORE_OK) {
        return false;
    }
    if (msg_broker_init(&test->broker, &test_catalog, test->loop) != CORE_OK) {
        return false;
    }
    if (ipc_server_create(&endpoint, &test->broker.server) != CORE_OK) {
        return false;
    }
    return ipc_server_get_fd(test->broker.server, &fd) == CORE_OK &&
           eventloop_add_fd(test->loop, fd, EVENT_READABLE, &test->broker.listener) == CORE_OK;
}

static inline void test_broker_stop(TestBroker *test)
{
    msg_broker_shutdown(&test->broker);
    if (test->loop != NULL) {
        (void)eventloop_close(&test->loop);
    }
}

/* Один проход брокера: принять соединения, обслужить каждое, снять упавшие. */
static inline void test_turn(TestBroker *test)
{
    MsgClient **link = &test->broker.clients;

    (void)msg_broker_accept(&test->broker);
    while (*link != NULL) {
        if (msg_broker_serve(&test->broker, *link, test->clock_ms) != CORE_OK) {
            msg_broker_close_client(&test->broker, link);
            test->closed++;
            continue;
        }
        link = &(*link)->next;
    }
}

static inline IpcConnectionHandle test_connect(const char *path)
{
    String endpoint = { (char *)path, 0 };
    IpcConnectionHandle connection = NULL;
    IpcConnectionState state = IPC_CONNECTION_STATE_CONNECTING;

    while (path[endpoint.length] != '\0') {
        endpoint.length++;
    }
    if (ipc_connection_begin_connect(&endpoint, &connection) != CORE_OK) {
        return NULL;
    }
    for (usize attempt = 0; attempt < 100; ++attempt) {
        if (ipc_connection_finish_connect(connection, &state) != CORE_OK) {
            (void)ipc_connection_close(&connection);
            return NULL;
        }
        if (state == IPC_CONNECTION_STATE_OPEN) {
            return connection;
        }
    }
    (void)ipc_connection_close(&connection);
    return NULL;
}

static inline bool test_send(IpcConnectionHandle connection, IpcMessageType type,
                             const u8 *data, usize size)
{
    IpcMessage message = { type, { (u8 *)data, size } };
    bool pending = false;

    return ipc_connection_send(connection, &message) == CORE_OK &&
           ipc_connection_flush(connection, &pending) == CORE_OK;
}

/*
 * Ждёт один кадр от брокера, прокручивая его между попытками. false, если
 * брокер ничего не прислал или закрыл соединение.
 */
static inline bool test_receive(TestBroker *test, IpcConnectionHandle connection,
                                IpcMessage *out)
{
    for (usize attempt = 0; attempt < 64; ++attempt) {
        IpcReceiveStatus status = IPC_RECEIVE_STATUS_NO_MESSAGE;

        test_turn(test);
        if (ipc_connection_receive(connection, &status, out) != CORE_OK) {
            return false;
        }
        if (status == IPC_RECEIVE_STATUS_MESSAGE) {
            return true;
        }
        if (status == IPC_RECEIVE_STATUS_PEER_CLOSED) {
            return false;
        }
    }
    return false;
}

/* Запрос и один ответ. type_out проверяется вызывающим. */
static inline bool test_call(TestBroker *test, IpcConnectionHandle connection,
                             IpcMessageType type, const u8 *data, usize size, IpcMessage *out)
{
    return test_send(connection, type, data, size) && test_receive(test, connection, out);
}

/* Первое поле любого ответа - ErrorCode. */
static inline u32 test_reply_code(const IpcMessage *message)
{
    MsgReader reader = msg_reader(message->payload.data, message->payload.size);

    return msg_read_u32(&reader);
}

static inline usize test_build_bind(u8 *buffer, const char *application_id, const char *secret)
{
    MsgWriter writer = msg_writer(buffer, 512);
    usize id_length = 0;
    usize secret_length = 0;

    while (application_id[id_length] != '\0') {
        id_length++;
    }
    while (secret[secret_length] != '\0') {
        secret_length++;
    }
    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    msg_write_text(&writer, (const u8 *)application_id, id_length);
    msg_write_text(&writer, (const u8 *)secret, secret_length);
    return writer.offset;
}

static inline usize test_build_topic(u8 *buffer, const char *topic)
{
    MsgWriter writer = msg_writer(buffer, 512);
    usize length = 0;

    while (topic[length] != '\0') {
        length++;
    }
    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    msg_write_text(&writer, (const u8 *)topic, length);
    return writer.offset;
}

static inline usize test_build_publish(u8 *buffer, usize capacity, const char *topic,
                                       const u8 *payload, usize payload_size)
{
    MsgWriter writer = msg_writer(buffer, capacity);
    usize length = 0;

    while (topic[length] != '\0') {
        length++;
    }
    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    msg_write_text(&writer, (const u8 *)topic, length);
    msg_write_blob(&writer, payload, payload_size);
    return writer.offset;
}

/* Соединение, прошедшее bind; *out_client_id - выданный брокером. */
static inline IpcConnectionHandle test_bound(TestBroker *test, const char *path,
                                             const char *application_id, const char *secret,
                                             MsgClientId *out_client_id)
{
    u8 frame[512];
    usize size = test_build_bind(frame, application_id, secret);
    IpcConnectionHandle connection = test_connect(path);
    IpcMessage reply = { 0, { NULL, 0 } };
    MsgReader reader;

    if (connection == NULL) {
        return NULL;
    }
    if (!test_call(test, connection, MSG_TYPE_BIND, frame, size, &reply)) {
        (void)ipc_connection_close(&connection);
        return NULL;
    }
    reader = msg_reader(reply.payload.data, reply.payload.size);
    if (reply.type != MSG_TYPE_BIND_RESULT || msg_read_u32(&reader) != (u32)CORE_OK) {
        (void)ipc_message_destroy(&reply);
        (void)ipc_connection_close(&connection);
        return NULL;
    }
    (void)msg_read_text(&reader, 1, MSG_APPLICATION_ID_MAX_LENGTH);
    if (out_client_id != NULL) {
        *out_client_id = msg_read_u64(&reader);
    }
    (void)ipc_message_destroy(&reply);
    return connection;
}

#endif
