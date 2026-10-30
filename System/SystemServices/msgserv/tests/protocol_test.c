/*
 * L2: сам протокол. Эталонные байты каждого типа, усечения и лишние байты на
 * каждом поле, невалидный UTF-8, неизвестный message_type и детерминированные
 * порчи кадров.
 *
 * Требование ко всем порченым кадрам одно: брокер либо отвечает ошибкой, либо
 * закрывает соединение, и в любом случае продолжает правильно обслуживать
 * следующего клиента. Отдельная проверка - попытка подделать отправителя в
 * publish: лишние байты вместо полей отправителя кадр не проходят.
 */

#define CHECK_TEST_NAME "msgserv-protocol"
#include "check.h"

#include "support.h"

#define SOCKET_PATH "/tmp/msgserv-protocol-test.sock"
#define FUZZ_ROUNDS 2048

static TestBroker test;

/* Исход отправки одного кадра. */
typedef enum Outcome {
    OUTCOME_REPLIED = 0, /* брокер ответил кадром */
    OUTCOME_CLOSED = 1   /* брокер закрыл соединение */
} Outcome;

/*
 * Отправляет кадр в новое соединение (при need_bind - уже прошедшее bind) и
 * сообщает, чем ответил брокер. Соединение всегда закрывается после пробы,
 * поэтому пробы не влияют друг на друга.
 */
static Outcome probe(IpcMessageType type, const u8 *data, usize size, bool need_bind,
                     u32 *out_code)
{
    IpcConnectionHandle connection;
    IpcMessage reply = { 0, { NULL, 0 } };
    Outcome outcome = OUTCOME_CLOSED;

    if (need_bind) {
        connection = test_bound(&test, SOCKET_PATH, "alpha", TEST_ALPHA_SECRET, NULL);
    } else {
        connection = test_connect(SOCKET_PATH);
    }
    if (connection == NULL) {
        return OUTCOME_CLOSED;
    }
    if (test_send(connection, type, data, size) && test_receive(&test, connection, &reply)) {
        outcome = OUTCOME_REPLIED;
        if (out_code != NULL) {
            *out_code = test_reply_code(&reply);
        }
        (void)ipc_message_destroy(&reply);
    }
    (void)ipc_connection_close(&connection);
    for (usize i = 0; i < 4; ++i) {
        test_turn(&test);
    }
    return outcome;
}

/* ---- эталонные байты ----------------------------------------------------- */

static void check_reference_bytes(void)
{
    /* bind: версия 1, "alpha", секрет. Раскладка пишется здесь руками, чтобы
     * изменение кодека ломало тест, а не только собственный же кодек. */
    static const u8 bind[] = {
        0x01, 0x00,                                     /* protocol_version */
        0x05, 0x00, 'a', 'l', 'p', 'h', 'a',             /* application_id */
        0x03, 0x00, 's', 'e', 'c'                       /* secret */
    };
    static const u8 subscribe[] = {
        0x01, 0x00,                                     /* protocol_version */
        0x08, 0x00, 'u', 'i', '.', 't', 'h', 'e', 'm', 'e'
    };
    static const u8 publish[] = {
        0x01, 0x00,
        0x08, 0x00, 'u', 'i', '.', 't', 'h', 'e', 'm', 'e',
        0x02, 0x00, 0x00, 0x00, 0xaa, 0xbb               /* payload */
    };
    static const u8 request[] = {
        0x01, 0x00,
        0x00,                                            /* target_kind: client */
        0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* target client_id */
        0x04, 0x00, 'p', 'i', 'n', 'g',                  /* method */
        0x00, 0x00, 0x00, 0x00                           /* пустой payload */
    };
    static const u8 respond[] = {
        0x01, 0x00,
        0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* correlation_id */
        0x2a, 0x00, 0x00, 0x00,                          /* status */
        0x00, 0x00, 0x00, 0x00
    };
    u8 built[64];
    usize size;

    size = test_build_bind(built, "alpha", "sec");
    EXPECT(1, size == sizeof(bind));
    EXPECT(2, msg_bytes_equal(built, size, bind, sizeof(bind)));

    size = test_build_topic(built, "ui.theme");
    EXPECT(3, size == sizeof(subscribe));
    EXPECT(4, msg_bytes_equal(built, size, subscribe, sizeof(subscribe)));

    {
        static const u8 payload[2] = { 0xaa, 0xbb };

        size = test_build_publish(built, sizeof(built), "ui.theme", payload, 2);
        EXPECT(5, size == sizeof(publish));
        EXPECT(6, msg_bytes_equal(built, size, publish, sizeof(publish)));
    }
    {
        MsgWriter writer = msg_writer(built, sizeof(built));

        msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
        msg_write_u8(&writer, MSG_TARGET_CLIENT);
        msg_write_u64(&writer, 7);
        msg_write_text(&writer, (const u8 *)"ping", 4);
        msg_write_blob(&writer, NULL, 0);
        EXPECT(7, writer.offset == sizeof(request));
        EXPECT(8, msg_bytes_equal(built, writer.offset, request, sizeof(request)));
    }
    {
        MsgWriter writer = msg_writer(built, sizeof(built));

        msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
        msg_write_u64(&writer, 5);
        msg_write_u32(&writer, 42);
        msg_write_blob(&writer, NULL, 0);
        EXPECT(9, writer.offset == sizeof(respond));
        EXPECT(10, msg_bytes_equal(built, writer.offset, respond, sizeof(respond)));
    }
}

/* ---- усечения и лишние байты --------------------------------------------- */

/*
 * Любой префикс корректного кадра и любой кадр с лишним байтом - нарушение
 * протокола. Брокер обязан закрыть соединение: отвечать на кадр, который он
 * не разобрал, нельзя.
 */
static void check_truncations(void)
{
    u8 frame[512];
    usize size = test_build_bind(frame, "alpha", TEST_ALPHA_SECRET);

    for (usize prefix = 0; prefix < size; ++prefix) {
        EXPECT(11, probe(MSG_TYPE_BIND, frame, prefix, false, NULL) == OUTCOME_CLOSED);
    }
    frame[size] = 0x00;
    EXPECT(12, probe(MSG_TYPE_BIND, frame, size + 1, false, NULL) == OUTCOME_CLOSED);

    size = test_build_topic(frame, "ui.theme");
    for (usize prefix = 0; prefix < size; ++prefix) {
        EXPECT(13, probe(MSG_TYPE_SUBSCRIBE, frame, prefix, true, NULL) == OUTCOME_CLOSED);
    }
    frame[size] = 0x00;
    EXPECT(14, probe(MSG_TYPE_SUBSCRIBE, frame, size + 1, true, NULL) == OUTCOME_CLOSED);

    {
        static const u8 payload[4] = { 1, 2, 3, 4 };

        size = test_build_publish(frame, sizeof(frame), "ui.theme", payload, 4);
        for (usize prefix = 0; prefix < size; ++prefix) {
            EXPECT(15, probe(MSG_TYPE_PUBLISH, frame, prefix, true, NULL) == OUTCOME_CLOSED);
        }
        frame[size] = 0x00;
        EXPECT(16, probe(MSG_TYPE_PUBLISH, frame, size + 1, true, NULL) == OUTCOME_CLOSED);
    }
}

/*
 * Подделка отправителя. В publish полей отправителя нет: их пишет брокер. Кадр,
 * в который клиент дописал client_id и application_id "beta", отличается от
 * корректного только лишними байтами - и обязан быть отвергнут, а не принят с
 * чужой личностью.
 */
static void check_sender_forgery(void)
{
    u8 frame[512];
    MsgWriter writer = msg_writer(frame, sizeof(frame));
    static const u8 payload[2] = { 9, 9 };

    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    msg_write_text(&writer, (const u8 *)"ui.theme", 8);
    msg_write_blob(&writer, payload, sizeof(payload));
    msg_write_u64(&writer, 1234);                        /* будто бы client_id */
    msg_write_text(&writer, (const u8 *)"beta", 4);      /* будто бы отправитель */
    EXPECT(17, !writer.bad);
    EXPECT(18, probe(MSG_TYPE_PUBLISH, frame, writer.offset, true, NULL) == OUTCOME_CLOSED);
}

static void check_invalid_utf8(void)
{
    u8 frame[512];
    MsgWriter writer = msg_writer(frame, sizeof(frame));
    static const u8 bad_id[3] = { 0xff, 0xfe, 0xfd };
    static const u8 lone_continuation[2] = { 'a', 0x80 };

    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    msg_write_text(&writer, bad_id, sizeof(bad_id));
    msg_write_text(&writer, (const u8 *)"sec", 3);
    EXPECT(19, probe(MSG_TYPE_BIND, frame, writer.offset, false, NULL) == OUTCOME_CLOSED);

    writer = msg_writer(frame, sizeof(frame));
    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    msg_write_text(&writer, lone_continuation, sizeof(lone_continuation));
    EXPECT(20, probe(MSG_TYPE_SUBSCRIBE, frame, writer.offset, true, NULL) == OUTCOME_CLOSED);

    /* NUL внутри строки тоже запрещён. */
    writer = msg_writer(frame, sizeof(frame));
    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    msg_write_text(&writer, (const u8 *)"ui\0theme", 8);
    EXPECT(21, probe(MSG_TYPE_SUBSCRIBE, frame, writer.offset, true, NULL) == OUTCOME_CLOSED);
}

static void check_oversized_payload(void)
{
    static u8 frame[MSG_PAYLOAD_MAX_SIZE + 256];
    MsgWriter writer = msg_writer(frame, sizeof(frame));

    /* Заявленный размер больше MSG_PAYLOAD_MAX_SIZE: кадр не разбирается, даже
     * если сам влез в IPC_MESSAGE_MAX_SIZE. */
    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    msg_write_text(&writer, (const u8 *)"ui.theme", 8);
    msg_write_u32(&writer, (u32)(MSG_PAYLOAD_MAX_SIZE + 1));
    for (usize i = 0; i < MSG_PAYLOAD_MAX_SIZE + 1; ++i) {
        msg_write_u8(&writer, 0x5a);
    }
    EXPECT(22, !writer.bad);
    EXPECT(23, probe(MSG_TYPE_PUBLISH, frame, writer.offset, true, NULL) == OUTCOME_CLOSED);

    /* Ровно предел - принимается. */
    writer = msg_writer(frame, sizeof(frame));
    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    msg_write_text(&writer, (const u8 *)"ui.theme", 8);
    msg_write_u32(&writer, (u32)MSG_PAYLOAD_MAX_SIZE);
    for (usize i = 0; i < MSG_PAYLOAD_MAX_SIZE; ++i) {
        msg_write_u8(&writer, 0x5a);
    }
    {
        u32 code = 0xffffffffu;

        EXPECT(24, probe(MSG_TYPE_PUBLISH, frame, writer.offset, true, &code) ==
                       OUTCOME_REPLIED);
        EXPECT(25, code == (u32)CORE_OK);
    }
}

static void check_unknown_types(void)
{
    u8 frame[2];
    MsgWriter writer = msg_writer(frame, sizeof(frame));
    /* Типы ответов и незапрошенных кадров брокер от клиента не принимает. */
    static const IpcMessageType forbidden[] = {
        0, 2, 4, 6, 8, 10, 12, 13, 19, MSG_TYPE_EVENT, MSG_TYPE_DELIVER_REQUEST,
        MSG_TYPE_DELIVER_RESPONSE, 100, 0xffffffffu
    };

    msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
    for (usize i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); ++i) {
        EXPECT(26, probe(forbidden[i], frame, writer.offset, true, NULL) == OUTCOME_CLOSED);
    }
}

/* ---- детерминированные порчи --------------------------------------------- */

/* xorshift64*: одна и та же последовательность на любой машине. */
static u64 fuzz_state = 0x2545f4914f6cdd1dULL;

static u64 fuzz_next(void)
{
    fuzz_state ^= fuzz_state >> 12;
    fuzz_state ^= fuzz_state << 25;
    fuzz_state ^= fuzz_state >> 27;
    return fuzz_state * 0x2545f4914f6cdd1dULL;
}

/*
 * Берёт корректный кадр каждого типа, портит в нём один байт и проверяет, что
 * брокер остался работоспособным: после каждой порчи новое соединение
 * нормально проходит bind и получает ответ на subscribe.
 */
static void check_fuzz(void)
{
    u8 valid[5][512];
    IpcMessageType types[5] = { MSG_TYPE_BIND, MSG_TYPE_SUBSCRIBE, MSG_TYPE_PUBLISH,
                                MSG_TYPE_REQUEST, MSG_TYPE_RESPOND };
    usize sizes[5];
    u8 frame[512];
    static const u8 payload[4] = { 1, 2, 3, 4 };

    sizes[0] = test_build_bind(valid[0], "alpha", TEST_ALPHA_SECRET);
    sizes[1] = test_build_topic(valid[1], "ui.theme");
    sizes[2] = test_build_publish(valid[2], sizeof(valid[2]), "ui.theme", payload, 4);
    {
        MsgWriter writer = msg_writer(valid[3], sizeof(valid[3]));

        msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
        msg_write_u8(&writer, MSG_TARGET_APPLICATION);
        msg_write_text(&writer, (const u8 *)"beta", 4);
        msg_write_text(&writer, (const u8 *)"open_image", 10);
        msg_write_blob(&writer, payload, 4);
        sizes[3] = writer.offset;
    }
    {
        MsgWriter writer = msg_writer(valid[4], sizeof(valid[4]));

        msg_write_u16(&writer, MSG_PROTOCOL_VERSION);
        msg_write_u64(&writer, 1);
        msg_write_u32(&writer, 0);
        msg_write_blob(&writer, payload, 4);
        sizes[4] = writer.offset;
    }

    for (usize round = 0; round < FUZZ_ROUNDS; ++round) {
        usize which = (usize)(fuzz_next() % 5);
        usize size = sizes[which];
        usize index = (usize)(fuzz_next() % size);
        u8 mask = (u8)(fuzz_next() | 1u);
        IpcConnectionHandle probe_connection;
        IpcMessage reply = { 0, { NULL, 0 } };

        for (usize i = 0; i < size; ++i) {
            frame[i] = valid[which][i];
        }
        frame[index] ^= mask;
        probe_connection = test_connect(SOCKET_PATH);
        EXPECT(27, probe_connection != NULL);
        /* bind нужен всем типам кроме самого bind. */
        if (which != 0) {
            u8 bind_frame[512];
            usize bind_size = test_build_bind(bind_frame, "alpha", TEST_ALPHA_SECRET);

            EXPECT(28, test_call(&test, probe_connection, MSG_TYPE_BIND, bind_frame,
                                 bind_size, &reply));
            EXPECT(29, test_reply_code(&reply) == (u32)CORE_OK);
            (void)ipc_message_destroy(&reply);
        }
        (void)test_send(probe_connection, types[which], frame, size);
        if (test_receive(&test, probe_connection, &reply)) {
            /* Ответ на порченый кадр допустим, но только с кодом ошибки или
             * CORE_OK для порчи, не меняющей смысла. */
            EXPECT(30, reply.payload.size >= 4);
            EXPECT(31, test_reply_code(&reply) <= (u32)CORE_OVERFLOW);
            (void)ipc_message_destroy(&reply);
        }
        (void)ipc_connection_close(&probe_connection);
        test_turn(&test);
    }

    /* Брокер жив: обычный клиент работает как раньше. */
    {
        IpcConnectionHandle beta = test_bound(&test, SOCKET_PATH, "beta", TEST_BETA_SECRET,
                                              NULL);
        IpcMessage reply = { 0, { NULL, 0 } };

        EXPECT(32, beta != NULL);
        EXPECT(33, test_call(&test, beta, MSG_TYPE_SUBSCRIBE, frame,
                             test_build_topic(frame, "ui.theme"), &reply));
        EXPECT(34, test_reply_code(&reply) == (u32)CORE_OK);
        (void)ipc_message_destroy(&reply);
        (void)ipc_connection_close(&beta);
        test_turn(&test);
    }
    EXPECT(35, test.broker.clients == NULL);
}

int main(void)
{
    check_reference_bytes();
    EXPECT(36, test_broker_start(&test, SOCKET_PATH));
    check_truncations();
    check_sender_forgery();
    check_invalid_utf8();
    check_oversized_payload();
    check_unknown_types();
    check_fuzz();
    test_broker_stop(&test);
    check_passed();
    return 0;
}
