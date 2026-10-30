/*
 * L1: логика брокера на настоящих соединениях ipclib и настоящем event loop'е,
 * без процесса msgserv. Правило проекта: на каждый контракт спецификации -
 * хотя бы один EXPECT, и номера проверок уникальны внутри теста.
 */

#define CHECK_TEST_NAME "msgserv-broker"
#include "check.h"

#include "support.h"

#define SOCKET_PATH "/tmp/msgserv-broker-test.sock"

static TestBroker test;

/* ---- личность ------------------------------------------------------------ */

static void check_bind(void)
{
    u8 frame[512];
    IpcConnectionHandle connection;
    IpcMessage reply = { 0, { NULL, 0 } };
    MsgReader reader;
    MsgField id;
    MsgClientId first = 0;
    MsgClientId second = 0;
    IpcConnectionHandle extra[MSG_CONNECTIONS_MAX_PER_APPLICATION + 1];

    /* Верный секрет: брокер подтверждает application_id и выдаёт client_id. */
    connection = test_connect(SOCKET_PATH);
    EXPECT(1, connection != NULL);
    EXPECT(2, test_call(&test, connection, MSG_TYPE_BIND,
                        frame, test_build_bind(frame, "alpha", TEST_ALPHA_SECRET), &reply));
    EXPECT(3, reply.type == MSG_TYPE_BIND_RESULT);
    reader = msg_reader(reply.payload.data, reply.payload.size);
    EXPECT(4, msg_read_u32(&reader) == (u32)CORE_OK);
    id = msg_read_text(&reader, 1, MSG_APPLICATION_ID_MAX_LENGTH);
    EXPECT(5, id.length == 5 && msg_bytes_equal(id.data, id.length, (const u8 *)"alpha", 5));
    first = msg_read_u64(&reader);
    EXPECT(6, first != 0 && msg_reader_done(&reader));
    (void)ipc_message_destroy(&reply);

    /* Второй bind в том же соединении запрещён, но соединение живёт. */
    EXPECT(7, test_call(&test, connection, MSG_TYPE_BIND,
                        frame, test_build_bind(frame, "alpha", TEST_ALPHA_SECRET), &reply));
    EXPECT(8, test_reply_code(&reply) == (u32)CORE_INVALID_STATE);
    (void)ipc_message_destroy(&reply);

    /* Два экземпляра одного приложения различаются по client_id. */
    extra[0] = test_bound(&test, SOCKET_PATH, "alpha", TEST_ALPHA_SECRET, &second);
    EXPECT(9, extra[0] != NULL);
    EXPECT(10, second != first);

    (void)ipc_connection_close(&connection);
    (void)ipc_connection_close(&extra[0]);
    test_turn(&test);
}

static void check_bind_refusals(void)
{
    u8 frame[512];
    IpcConnectionHandle connection;
    IpcMessage reply = { 0, { NULL, 0 } };

    /* Неверный секрет: CORE_PERMISSION_DENIED, личность не выдана. */
    connection = test_connect(SOCKET_PATH);
    EXPECT(11, connection != NULL);
    EXPECT(12, test_call(&test, connection, MSG_TYPE_BIND,
                         frame, test_build_bind(frame, "alpha", "wrong"), &reply));
    EXPECT(13, test_reply_code(&reply) == (u32)CORE_PERMISSION_DENIED);
    EXPECT(14, reply.payload.size == 4); /* ни client_id, ни application_id */
    (void)ipc_message_destroy(&reply);
    (void)ipc_connection_close(&connection);

    /* Приложения нет в каталоге. */
    connection = test_connect(SOCKET_PATH);
    EXPECT(15, test_call(&test, connection, MSG_TYPE_BIND,
                         frame, test_build_bind(frame, "nobody", "secret"), &reply));
    EXPECT(16, test_reply_code(&reply) == (u32)CORE_NOT_FOUND);
    (void)ipc_message_destroy(&reply);
    (void)ipc_connection_close(&connection);
    test_turn(&test);
}

static void check_connection_limit(void)
{
    IpcConnectionHandle bound[MSG_CONNECTIONS_MAX_PER_APPLICATION];
    IpcConnectionHandle extra;
    u8 frame[512];
    IpcMessage reply = { 0, { NULL, 0 } };

    for (usize i = 0; i < MSG_CONNECTIONS_MAX_PER_APPLICATION; ++i) {
        bound[i] = test_bound(&test, SOCKET_PATH, "beta", TEST_BETA_SECRET, NULL);
        EXPECT(17, bound[i] != NULL);
    }
    /* Секрет верный, но слоты приложения заняты: CORE_OVERFLOW, а не отказ
     * по правам - иначе отладка превращается в угадывание. */
    extra = test_connect(SOCKET_PATH);
    EXPECT(18, test_call(&test, extra, MSG_TYPE_BIND,
                         frame, test_build_bind(frame, "beta", TEST_BETA_SECRET), &reply));
    EXPECT(19, test_reply_code(&reply) == (u32)CORE_OVERFLOW);
    (void)ipc_message_destroy(&reply);
    (void)ipc_connection_close(&extra);
    for (usize i = 0; i < MSG_CONNECTIONS_MAX_PER_APPLICATION; ++i) {
        (void)ipc_connection_close(&bound[i]);
    }
    test_turn(&test);
    EXPECT(20, test.broker.clients == NULL);
}

static void check_request_before_bind(void)
{
    IpcConnectionHandle connection = test_connect(SOCKET_PATH);
    u8 frame[512];
    usize size = test_build_topic(frame, "ui.theme");
    IpcReceiveStatus status = IPC_RECEIVE_STATUS_NO_MESSAGE;
    IpcMessage reply = { 0, { NULL, 0 } };
    usize before = test.closed;

    EXPECT(21, connection != NULL);
    EXPECT(22, test_send(connection, MSG_TYPE_SUBSCRIBE, frame, size));
    for (usize attempt = 0; attempt < 16; ++attempt) {
        test_turn(&test);
    }
    /* До bind брокер не знает, кто это: соединение закрывается без ответа. */
    EXPECT(23, test.closed == before + 1);
    EXPECT(24, ipc_connection_receive(connection, &status, &reply) != CORE_OK ||
                   status == IPC_RECEIVE_STATUS_PEER_CLOSED);
    (void)ipc_connection_close(&connection);
}

static void check_unknown_type(void)
{
    IpcConnectionHandle connection = test_bound(&test, SOCKET_PATH, "alpha",
                                                TEST_ALPHA_SECRET, NULL);
    u8 frame[2];
    MsgWriter writer = msg_writer(frame, sizeof(frame));
    usize before = test.closed;

    EXPECT(25, connection != NULL);
    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    EXPECT(26, test_send(connection, 99, frame, writer.offset));
    for (usize attempt = 0; attempt < 16; ++attempt) {
        test_turn(&test);
    }
    EXPECT(27, test.closed == before + 1);
    (void)ipc_connection_close(&connection);
}

static void check_bad_version(void)
{
    IpcConnectionHandle connection = test_connect(SOCKET_PATH);
    u8 frame[512];
    MsgWriter writer = msg_writer(frame, sizeof(frame));
    usize before = test.closed;

    EXPECT(28, connection != NULL);
    msg_write_u16(&writer, 2); /* версия не 1 */
    msg_write_text(&writer, (const u8 *)"alpha", 5);
    msg_write_text(&writer, (const u8 *)TEST_ALPHA_SECRET, sizeof(TEST_ALPHA_SECRET) - 1);
    EXPECT(29, test_send(connection, MSG_TYPE_BIND, frame, writer.offset));
    for (usize attempt = 0; attempt < 16; ++attempt) {
        test_turn(&test);
    }
    EXPECT(30, test.closed == before + 1);
    (void)ipc_connection_close(&connection);
}

/* ---- подписки ------------------------------------------------------------ */

static void check_subscriptions(void)
{
    IpcConnectionHandle beta = test_bound(&test, SOCKET_PATH, "beta", TEST_BETA_SECRET, NULL);
    u8 frame[512];
    IpcMessage reply = { 0, { NULL, 0 } };

    EXPECT(31, beta != NULL);
    EXPECT(32, test_call(&test, beta, MSG_TYPE_SUBSCRIBE, frame,
                         test_build_topic(frame, "ui.theme"), &reply));
    EXPECT(33, reply.type == MSG_TYPE_SUBSCRIBE_RESULT);
    EXPECT(34, test_reply_code(&reply) == (u32)CORE_OK);
    (void)ipc_message_destroy(&reply);

    /* Повторная подписка на тот же topic. */
    EXPECT(35, test_call(&test, beta, MSG_TYPE_SUBSCRIBE, frame,
                         test_build_topic(frame, "ui.theme"), &reply));
    EXPECT(36, test_reply_code(&reply) == (u32)CORE_ALREADY_EXISTS);
    (void)ipc_message_destroy(&reply);

    /* Topic, которого нет в правах приложения. */
    EXPECT(37, test_call(&test, beta, MSG_TYPE_SUBSCRIBE, frame,
                         test_build_topic(frame, "sensor.gps"), &reply));
    EXPECT(38, test_reply_code(&reply) == (u32)CORE_PERMISSION_DENIED);
    (void)ipc_message_destroy(&reply);

    EXPECT(39, test_call(&test, beta, MSG_TYPE_UNSUBSCRIBE, frame,
                         test_build_topic(frame, "ui.theme"), &reply));
    EXPECT(40, test_reply_code(&reply) == (u32)CORE_OK);
    (void)ipc_message_destroy(&reply);

    EXPECT(41, test_call(&test, beta, MSG_TYPE_UNSUBSCRIBE, frame,
                         test_build_topic(frame, "ui.theme"), &reply));
    EXPECT(42, test_reply_code(&reply) == (u32)CORE_NOT_FOUND);
    (void)ipc_message_destroy(&reply);

    (void)ipc_connection_close(&beta);
    test_turn(&test);
}

static void check_subscription_limit(void)
{
    IpcConnectionHandle gamma = test_bound(&test, SOCKET_PATH, "gamma", TEST_GAMMA_SECRET,
                                           NULL);
    u8 frame[512];
    IpcMessage reply = { 0, { NULL, 0 } };
    char topic[8] = { 't', 0, 0, 0, 0, 0, 0, 0 };

    EXPECT(43, gamma != NULL);
    for (usize i = 0; i < MSG_SUBSCRIPTIONS_MAX_PER_CLIENT; ++i) {
        usize length = 1;

        if (i >= 10) {
            topic[length++] = (char)('0' + i / 10);
        }
        topic[length++] = (char)('0' + i % 10);
        topic[length] = '\0';
        EXPECT(44, test_call(&test, gamma, MSG_TYPE_SUBSCRIBE, frame,
                             test_build_topic(frame, topic), &reply));
        EXPECT(45, test_reply_code(&reply) == (u32)CORE_OK);
        (void)ipc_message_destroy(&reply);
    }
    /* 33-я подписка разрешена правами, но не влезает в таблицу. */
    EXPECT(46, test_call(&test, gamma, MSG_TYPE_SUBSCRIBE, frame,
                         test_build_topic(frame, "t32"), &reply));
    EXPECT(47, test_reply_code(&reply) == (u32)CORE_OVERFLOW);
    (void)ipc_message_destroy(&reply);
    (void)ipc_connection_close(&gamma);
    test_turn(&test);
}

/* ---- публикация ---------------------------------------------------------- */

static void check_publish(void)
{
    MsgClientId alpha_id = 0;
    IpcConnectionHandle alpha;
    IpcConnectionHandle beta;
    u8 frame[512];
    u8 payload[4] = { 1, 2, 3, 4 };
    IpcMessage reply = { 0, { NULL, 0 } };
    IpcMessage event = { 0, { NULL, 0 } };
    MsgReader reader;
    MsgField sender;
    MsgField topic;
    MsgField body;

    alpha = test_bound(&test, SOCKET_PATH, "alpha", TEST_ALPHA_SECRET, &alpha_id);
    beta = test_bound(&test, SOCKET_PATH, "beta", TEST_BETA_SECRET, NULL);
    EXPECT(48, alpha != NULL && beta != NULL);
    EXPECT(49, test_call(&test, beta, MSG_TYPE_SUBSCRIBE, frame,
                         test_build_topic(frame, "ui.theme"), &reply));
    EXPECT(50, test_reply_code(&reply) == (u32)CORE_OK);
    (void)ipc_message_destroy(&reply);

    EXPECT(51, test_call(&test, alpha, MSG_TYPE_PUBLISH, frame,
                         test_build_publish(frame, sizeof(frame), "ui.theme", payload, 4),
                         &reply));
    EXPECT(52, reply.type == MSG_TYPE_PUBLISH_RESULT);
    reader = msg_reader(reply.payload.data, reply.payload.size);
    EXPECT(53, msg_read_u32(&reader) == (u32)CORE_OK);
    EXPECT(54, msg_read_u16(&reader) == 1); /* delivered */
    EXPECT(55, msg_read_u16(&reader) == 0); /* dropped */
    EXPECT(56, msg_reader_done(&reader));
    (void)ipc_message_destroy(&reply);

    /* Поля отправителя заполняет брокер: они совпадают с личностью alpha. */
    EXPECT(57, test_receive(&test, beta, &event));
    EXPECT(58, event.type == MSG_TYPE_EVENT);
    reader = msg_reader(event.payload.data, event.payload.size);
    EXPECT(59, msg_read_u64(&reader) == alpha_id);
    sender = msg_read_text(&reader, 1, MSG_APPLICATION_ID_MAX_LENGTH);
    EXPECT(60, msg_bytes_equal(sender.data, sender.length, (const u8 *)"alpha", 5));
    topic = msg_read_text(&reader, 1, MSG_TOPIC_MAX_LENGTH);
    EXPECT(61, msg_bytes_equal(topic.data, topic.length, (const u8 *)"ui.theme", 8));
    EXPECT(62, msg_read_u64(&reader) == 1); /* первый sequence по topic'у */
    body = msg_read_blob(&reader, MSG_PAYLOAD_MAX_SIZE);
    EXPECT(63, body.length == 4 && body.data[0] == 1 && body.data[3] == 4);
    EXPECT(64, msg_reader_done(&reader));
    (void)ipc_message_destroy(&event);

    /* sequence растёт по topic'у и у конкретного подписчика. */
    EXPECT(65, test_call(&test, alpha, MSG_TYPE_PUBLISH, frame,
                         test_build_publish(frame, sizeof(frame), "ui.theme", payload, 4),
                         &reply));
    (void)ipc_message_destroy(&reply);
    EXPECT(66, test_receive(&test, beta, &event));
    reader = msg_reader(event.payload.data, event.payload.size);
    (void)msg_read_u64(&reader);
    (void)msg_read_text(&reader, 1, MSG_APPLICATION_ID_MAX_LENGTH);
    (void)msg_read_text(&reader, 1, MSG_TOPIC_MAX_LENGTH);
    EXPECT(67, msg_read_u64(&reader) == 2);
    (void)ipc_message_destroy(&event);

    /* Topic без права публикации. */
    EXPECT(68, test_call(&test, alpha, MSG_TYPE_PUBLISH, frame,
                         test_build_publish(frame, sizeof(frame), "app.state", payload, 4),
                         &reply));
    EXPECT(69, test_reply_code(&reply) == (u32)CORE_PERMISSION_DENIED);
    (void)ipc_message_destroy(&reply);

    /* Подписчик ушёл: рассылать некому, но publish остаётся успешным. */
    (void)ipc_connection_close(&beta);
    test_turn(&test);
    test_turn(&test);
    EXPECT(70, test_call(&test, alpha, MSG_TYPE_PUBLISH, frame,
                         test_build_publish(frame, sizeof(frame), "ui.theme", payload, 4),
                         &reply));
    reader = msg_reader(reply.payload.data, reply.payload.size);
    EXPECT(71, msg_read_u32(&reader) == (u32)CORE_OK);
    EXPECT(72, msg_read_u16(&reader) == 0); /* подписки ушли вместе с соединением */
    (void)ipc_message_destroy(&reply);
    (void)ipc_connection_close(&alpha);
    test_turn(&test);
}

/* ---- запрос и ответ ------------------------------------------------------ */

static usize build_request_by_client(u8 *buffer, usize capacity, MsgClientId target,
                                     const char *method, const u8 *payload, usize size)
{
    MsgWriter writer = msg_writer(buffer, capacity);
    usize length = 0;

    while (method[length] != '\0') {
        length++;
    }
    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    msg_write_u8(&writer, MSG_TARGET_CLIENT);
    msg_write_u64(&writer, target);
    msg_write_text(&writer, (const u8 *)method, length);
    msg_write_blob(&writer, payload, size);
    return writer.offset;
}

static usize build_request_by_application(u8 *buffer, usize capacity, const char *target,
                                          const char *method)
{
    MsgWriter writer = msg_writer(buffer, capacity);
    usize target_length = 0;
    usize method_length = 0;

    while (target[target_length] != '\0') {
        target_length++;
    }
    while (method[method_length] != '\0') {
        method_length++;
    }
    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    msg_write_u8(&writer, MSG_TARGET_APPLICATION);
    msg_write_text(&writer, (const u8 *)target, target_length);
    msg_write_text(&writer, (const u8 *)method, method_length);
    msg_write_blob(&writer, NULL, 0);
    return writer.offset;
}

static usize build_respond(u8 *buffer, usize capacity, MsgCorrelationId correlation,
                           u32 status, const u8 *payload, usize size)
{
    MsgWriter writer = msg_writer(buffer, capacity);

    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    msg_write_u64(&writer, correlation);
    msg_write_u32(&writer, status);
    msg_write_blob(&writer, payload, size);
    return writer.offset;
}

static void check_request_response(void)
{
    MsgClientId alpha_id = 0;
    MsgClientId beta_id = 0;
    IpcConnectionHandle alpha = test_bound(&test, SOCKET_PATH, "alpha", TEST_ALPHA_SECRET,
                                           &alpha_id);
    IpcConnectionHandle beta = test_bound(&test, SOCKET_PATH, "beta", TEST_BETA_SECRET,
                                          &beta_id);
    u8 frame[512];
    u8 payload[2] = { 7, 8 };
    IpcMessage reply = { 0, { NULL, 0 } };
    IpcMessage delivered = { 0, { NULL, 0 } };
    MsgReader reader;
    MsgField method;
    MsgField sender;
    MsgCorrelationId correlation = 0;
    MsgCorrelationId echoed = 0;

    EXPECT(73, alpha != NULL && beta != NULL);
    EXPECT(74, test_call(&test, alpha, MSG_TYPE_REQUEST, frame,
                         build_request_by_client(frame, sizeof(frame), beta_id, "open_image",
                                                 payload, 2),
                         &reply));
    EXPECT(75, reply.type == MSG_TYPE_REQUEST_RESULT);
    reader = msg_reader(reply.payload.data, reply.payload.size);
    EXPECT(76, msg_read_u32(&reader) == (u32)CORE_OK);
    correlation = msg_read_u64(&reader);
    EXPECT(77, correlation != 0 && msg_reader_done(&reader));
    (void)ipc_message_destroy(&reply);

    /* Адресат получает незапрошенный кадр с тем же correlation_id и полями
     * отправителя от брокера. */
    EXPECT(78, test_receive(&test, beta, &delivered));
    EXPECT(79, delivered.type == MSG_TYPE_DELIVER_REQUEST);
    reader = msg_reader(delivered.payload.data, delivered.payload.size);
    EXPECT(80, msg_read_u64(&reader) == correlation);
    EXPECT(81, msg_read_u64(&reader) == alpha_id);
    sender = msg_read_text(&reader, 1, MSG_APPLICATION_ID_MAX_LENGTH);
    EXPECT(82, msg_bytes_equal(sender.data, sender.length, (const u8 *)"alpha", 5));
    method = msg_read_text(&reader, 1, MSG_METHOD_MAX_LENGTH);
    EXPECT(83, msg_bytes_equal(method.data, method.length, (const u8 *)"open_image", 10));
    EXPECT(84, msg_read_blob(&reader, MSG_PAYLOAD_MAX_SIZE).length == 2);
    EXPECT(85, msg_reader_done(&reader));
    (void)ipc_message_destroy(&delivered);

    /* Ответ: транспортная ошибка и прикладной статус разделены. */
    EXPECT(86, test_call(&test, beta, MSG_TYPE_RESPOND, frame,
                         build_respond(frame, sizeof(frame), correlation, 42, payload, 2),
                         &reply));
    EXPECT(87, reply.type == MSG_TYPE_RESPOND_RESULT);
    EXPECT(88, test_reply_code(&reply) == (u32)CORE_OK);
    (void)ipc_message_destroy(&reply);
    EXPECT(89, test_receive(&test, alpha, &delivered));
    EXPECT(90, delivered.type == MSG_TYPE_DELIVER_RESPONSE);
    reader = msg_reader(delivered.payload.data, delivered.payload.size);
    echoed = msg_read_u64(&reader);
    EXPECT(91, echoed == correlation);
    EXPECT(92, msg_read_u32(&reader) == (u32)CORE_OK); /* транспорт */
    EXPECT(93, msg_read_u32(&reader) == 42);           /* прикладной статус */
    EXPECT(94, msg_read_blob(&reader, MSG_PAYLOAD_MAX_SIZE).length == 2);
    EXPECT(95, msg_reader_done(&reader));
    (void)ipc_message_destroy(&delivered);

    /* Чужой correlation_id: ответить нельзя. */
    EXPECT(96, test_call(&test, beta, MSG_TYPE_RESPOND, frame,
                         build_respond(frame, sizeof(frame), correlation + 1000, 0, NULL, 0),
                         &reply));
    EXPECT(97, test_reply_code(&reply) == (u32)CORE_NOT_FOUND);
    (void)ipc_message_destroy(&reply);

    /* Метод, которого адресат не обслуживает. */
    EXPECT(98, test_call(&test, alpha, MSG_TYPE_REQUEST, frame,
                         build_request_by_client(frame, sizeof(frame), beta_id, "ping", NULL, 0),
                         &reply));
    EXPECT(99, test_reply_code(&reply) == (u32)CORE_PERMISSION_DENIED);
    (void)ipc_message_destroy(&reply);

    /* Нет такого соединения. */
    EXPECT(100, test_call(&test, alpha, MSG_TYPE_REQUEST, frame,
                          build_request_by_client(frame, sizeof(frame), beta_id + 777,
                                                  "open_image", NULL, 0),
                          &reply));
    EXPECT(101, test_reply_code(&reply) == (u32)CORE_NOT_FOUND);
    (void)ipc_message_destroy(&reply);

    (void)ipc_connection_close(&alpha);
    (void)ipc_connection_close(&beta);
    test_turn(&test);
}

static void check_request_by_application(void)
{
    IpcConnectionHandle alpha;
    IpcConnectionHandle beta;
    IpcConnectionHandle beta_second;
    u8 frame[512];
    IpcMessage reply = { 0, { NULL, 0 } };

    alpha = test_bound(&test, SOCKET_PATH, "alpha", TEST_ALPHA_SECRET, NULL);
    EXPECT(102, alpha != NULL);

    /* Ни одного подключённого экземпляра. */
    EXPECT(103, test_call(&test, alpha, MSG_TYPE_REQUEST, frame,
                          build_request_by_application(frame, sizeof(frame), "beta",
                                                       "open_image"),
                          &reply));
    EXPECT(104, test_reply_code(&reply) == (u32)CORE_NOT_FOUND);
    (void)ipc_message_destroy(&reply);

    /* Ровно один: адресация по application_id работает. */
    beta = test_bound(&test, SOCKET_PATH, "beta", TEST_BETA_SECRET, NULL);
    EXPECT(105, beta != NULL);
    EXPECT(106, test_call(&test, alpha, MSG_TYPE_REQUEST, frame,
                          build_request_by_application(frame, sizeof(frame), "beta",
                                                       "open_image"),
                          &reply));
    EXPECT(107, test_reply_code(&reply) == (u32)CORE_OK);
    (void)ipc_message_destroy(&reply);

    /* Два подключённых экземпляра: адрес неоднозначен. */
    beta_second = test_bound(&test, SOCKET_PATH, "beta", TEST_BETA_SECRET, NULL);
    EXPECT(108, beta_second != NULL);
    EXPECT(109, test_call(&test, alpha, MSG_TYPE_REQUEST, frame,
                          build_request_by_application(frame, sizeof(frame), "beta",
                                                       "open_image"),
                          &reply));
    EXPECT(110, test_reply_code(&reply) == (u32)CORE_ALREADY_EXISTS);
    (void)ipc_message_destroy(&reply);

    (void)ipc_connection_close(&alpha);
    (void)ipc_connection_close(&beta);
    (void)ipc_connection_close(&beta_second);
    test_turn(&test);
}

static void check_request_deadline(void)
{
    MsgClientId beta_id = 0;
    IpcConnectionHandle alpha = test_bound(&test, SOCKET_PATH, "alpha", TEST_ALPHA_SECRET,
                                           NULL);
    IpcConnectionHandle beta = test_bound(&test, SOCKET_PATH, "beta", TEST_BETA_SECRET,
                                          &beta_id);
    u8 frame[512];
    IpcMessage reply = { 0, { NULL, 0 } };
    IpcMessage delivered = { 0, { NULL, 0 } };
    MsgReader reader;

    EXPECT(111, alpha != NULL && beta != NULL);
    EXPECT(112, test_call(&test, alpha, MSG_TYPE_REQUEST, frame,
                          build_request_by_client(frame, sizeof(frame), beta_id, "open_image",
                                                  NULL, 0),
                          &reply));
    EXPECT(113, test_reply_code(&reply) == (u32)CORE_OK);
    (void)ipc_message_destroy(&reply);
    EXPECT(114, test_receive(&test, beta, &delivered)); /* beta молчит нарочно */
    (void)ipc_message_destroy(&delivered);

    /* Дедлайн ещё не истёк. */
    test.clock_ms += MSG_REQUEST_TIMEOUT_MS - 1;
    msg_broker_tick(&test.broker, test.clock_ms);
    test_turn(&test);
    {
        IpcReceiveStatus status = IPC_RECEIVE_STATUS_NO_MESSAGE;

        EXPECT(115, ipc_connection_receive(alpha, &status, &delivered) == CORE_OK);
        EXPECT(116, status == IPC_RECEIVE_STATUS_NO_MESSAGE);
    }

    /* Истёк: отправитель получает CORE_TIMEOUT в транспортном поле. */
    test.clock_ms += 2;
    msg_broker_tick(&test.broker, test.clock_ms);
    EXPECT(117, test_receive(&test, alpha, &delivered));
    EXPECT(118, delivered.type == MSG_TYPE_DELIVER_RESPONSE);
    reader = msg_reader(delivered.payload.data, delivered.payload.size);
    (void)msg_read_u64(&reader);
    EXPECT(119, msg_read_u32(&reader) == (u32)CORE_TIMEOUT);
    EXPECT(120, msg_read_u32(&reader) == 0);
    EXPECT(121, msg_read_blob(&reader, MSG_PAYLOAD_MAX_SIZE).length == 0);
    (void)ipc_message_destroy(&delivered);

    (void)ipc_connection_close(&alpha);
    (void)ipc_connection_close(&beta);
    test_turn(&test);
}

static void check_target_disconnect(void)
{
    MsgClientId beta_id = 0;
    IpcConnectionHandle alpha = test_bound(&test, SOCKET_PATH, "alpha", TEST_ALPHA_SECRET,
                                           NULL);
    IpcConnectionHandle beta = test_bound(&test, SOCKET_PATH, "beta", TEST_BETA_SECRET,
                                          &beta_id);
    u8 frame[512];
    IpcMessage reply = { 0, { NULL, 0 } };
    IpcMessage delivered = { 0, { NULL, 0 } };
    MsgReader reader;

    EXPECT(122, alpha != NULL && beta != NULL);
    EXPECT(123, test_call(&test, alpha, MSG_TYPE_REQUEST, frame,
                          build_request_by_client(frame, sizeof(frame), beta_id, "open_image",
                                                  NULL, 0),
                          &reply));
    EXPECT(124, test_reply_code(&reply) == (u32)CORE_OK);
    (void)ipc_message_destroy(&reply);

    /* Адресат ушёл: отправитель узнаёт сразу, а не через MSG_REQUEST_TIMEOUT_MS. */
    (void)ipc_connection_close(&beta);
    test_turn(&test);
    EXPECT(125, test_receive(&test, alpha, &delivered));
    EXPECT(126, delivered.type == MSG_TYPE_DELIVER_RESPONSE);
    reader = msg_reader(delivered.payload.data, delivered.payload.size);
    (void)msg_read_u64(&reader);
    EXPECT(127, msg_read_u32(&reader) == (u32)CORE_NOT_FOUND);
    (void)ipc_message_destroy(&delivered);
    (void)ipc_connection_close(&alpha);
    test_turn(&test);
}

static void check_bind_timeout(void)
{
    IpcConnectionHandle connection = test_connect(SOCKET_PATH);
    usize before;

    EXPECT(128, connection != NULL);
    test_turn(&test);
    before = test.broker.client_count;
    EXPECT(129, before == 1);
    test.clock_ms += MSG_BIND_TIMEOUT_MS;
    msg_broker_tick(&test.broker, test.clock_ms);
    /* Соединение без bind не держит слот дольше таймаута. */
    EXPECT(130, test.broker.client_count == 0);
    (void)ipc_connection_close(&connection);
}

static void check_clients_limit(void)
{
    static IpcConnectionHandle connections[MSG_CLIENTS_MAX + 1];
    usize opened = 0;

    for (usize i = 0; i < MSG_CLIENTS_MAX + 1; ++i) {
        connections[i] = test_connect(SOCKET_PATH);
        if (connections[i] != NULL) {
            opened++;
        }
        test_turn(&test);
    }
    EXPECT(131, opened == MSG_CLIENTS_MAX + 1);
    /* Выше MSG_CLIENTS_MAX брокер соединение не принимает. */
    EXPECT(132, test.broker.client_count == MSG_CLIENTS_MAX);
    for (usize i = 0; i < MSG_CLIENTS_MAX + 1; ++i) {
        if (connections[i] != NULL) {
            (void)ipc_connection_close(&connections[i]);
        }
    }
    test.clock_ms += MSG_BIND_TIMEOUT_MS;
    msg_broker_tick(&test.broker, test.clock_ms);
    test_turn(&test);
    EXPECT(133, test.broker.client_count == 0);
}

/*
 * Переполнение очереди получателя: подписчик не читает сокет, издатель
 * продолжает публиковать. Кадр отбрасывается только для него, а sequence по
 * topic'у всё равно растёт - разрыв в нумерации и есть сигнал пропуска.
 */
static void check_queue_overflow(void)
{
    static u8 big[MSG_PAYLOAD_MAX_SIZE];
    static u8 frame[MSG_PAYLOAD_MAX_SIZE + 256];
    IpcConnectionHandle alpha = test_bound(&test, SOCKET_PATH, "alpha", TEST_ALPHA_SECRET,
                                           NULL);
    IpcConnectionHandle beta = test_bound(&test, SOCKET_PATH, "beta", TEST_BETA_SECRET, NULL);
    IpcMessage reply = { 0, { NULL, 0 } };
    usize published = 0;
    usize dropped_total = 0;
    u64 last_sequence = 0;
    usize received = 0;
    bool gap = false;

    EXPECT(134, alpha != NULL && beta != NULL);
    EXPECT(135, test_call(&test, beta, MSG_TYPE_SUBSCRIBE, frame,
                          test_build_topic(frame, "ui.theme"), &reply));
    EXPECT(136, test_reply_code(&reply) == (u32)CORE_OK);
    (void)ipc_message_destroy(&reply);

    /* Публикуем, пока брокер не начнёт отбрасывать, и ещё немного после. */
    while (published < 300) {
        usize size = test_build_publish(frame, sizeof(frame), "ui.theme", big, sizeof(big));
        MsgReader reader;

        if (!test_call(&test, alpha, MSG_TYPE_PUBLISH, frame, size, &reply)) {
            break;
        }
        reader = msg_reader(reply.payload.data, reply.payload.size);
        if (msg_read_u32(&reader) != (u32)CORE_OK) {
            (void)ipc_message_destroy(&reply);
            break;
        }
        (void)msg_read_u16(&reader);
        dropped_total += msg_read_u16(&reader);
        (void)ipc_message_destroy(&reply);
        published++;
        if (dropped_total >= 3) {
            break;
        }
    }
    /* Отказ приходит от брокера, а не из транспорта: publish остаётся
     * успешным, а потеря видна в dropped_count. */
    EXPECT(137, dropped_total >= 3);
    EXPECT(138, published < 300);

    /* Разбираем всё, что доехало: до первого отброшенного кадра нумерация
     * непрерывна. */
    for (usize attempt = 0; attempt < 8192; ++attempt) {
        IpcMessage event = { 0, { NULL, 0 } };
        IpcReceiveStatus status = IPC_RECEIVE_STATUS_NO_MESSAGE;

        test_turn(&test);
        if (ipc_connection_receive(beta, &status, &event) != CORE_OK) {
            break;
        }
        if (status != IPC_RECEIVE_STATUS_MESSAGE) {
            continue;
        }
        if (event.type == MSG_TYPE_EVENT) {
            MsgReader reader = msg_reader(event.payload.data, event.payload.size);
            u64 sequence;

            (void)msg_read_u64(&reader);
            (void)msg_read_text(&reader, 1, MSG_APPLICATION_ID_MAX_LENGTH);
            (void)msg_read_text(&reader, 1, MSG_TOPIC_MAX_LENGTH);
            sequence = msg_read_u64(&reader);
            if (sequence != last_sequence + 1) {
                gap = true;
            }
            last_sequence = sequence;
            received++;
        }
        (void)ipc_message_destroy(&event);
    }
    EXPECT(139, received > 0);
    EXPECT(140, received == published - dropped_total && !gap);

    /* Очередь снова свободна. Следующее событие приходит с номером, который
     * учёл отброшенные: разрыв и есть сигнал "я пропустил события". */
    EXPECT(141, test_call(&test, alpha, MSG_TYPE_PUBLISH, frame,
                          test_build_publish(frame, sizeof(frame), "ui.theme", big, 4),
                          &reply));
    EXPECT(142, test_reply_code(&reply) == (u32)CORE_OK);
    (void)ipc_message_destroy(&reply);
    {
        IpcMessage event = { 0, { NULL, 0 } };
        MsgReader reader;
        u64 sequence;

        EXPECT(143, test_receive(&test, beta, &event));
        EXPECT(144, event.type == MSG_TYPE_EVENT);
        reader = msg_reader(event.payload.data, event.payload.size);
        (void)msg_read_u64(&reader);
        (void)msg_read_text(&reader, 1, MSG_APPLICATION_ID_MAX_LENGTH);
        (void)msg_read_text(&reader, 1, MSG_TOPIC_MAX_LENGTH);
        sequence = msg_read_u64(&reader);
        EXPECT(145, sequence == published + 1);
        EXPECT(146, sequence != last_sequence + 1);
        (void)ipc_message_destroy(&event);
    }

    (void)ipc_connection_close(&alpha);
    (void)ipc_connection_close(&beta);
    test_turn(&test);
}

int main(void)
{
    EXPECT(147, test_broker_start(&test, SOCKET_PATH));
    check_bind();
    check_bind_refusals();
    check_connection_limit();
    check_request_before_bind();
    check_unknown_type();
    check_bad_version();
    check_subscriptions();
    check_subscription_limit();
    check_publish();
    check_request_response();
    check_request_by_application();
    check_request_deadline();
    check_target_disconnect();
    check_bind_timeout();
    check_clients_limit();
    check_queue_overflow();
    test_broker_stop(&test);
    check_passed();
    return 0;
}
