/*
 * L1, отказы: memory_allocate, ipc_connection_send и ipc_connection_flush
 * ломаются по команде теста, ipc_message_destroy считается.
 *
 * Проверяется не конкретный номер неудачной аллокации - он зависит от
 * внутренностей ipclib, - а инвариант: какой бы вызов в проходе брокера ни
 * отказал, брокер остаётся работоспособным и следующий клиент обслуживается
 * правильно. Поэтому отказ переставляется по всем вызовам прохода подряд.
 */

#define CHECK_TEST_NAME "msgserv-fault"
#include "check.h"

#include "support.h"

#define SOCKET_PATH "/tmp/msgserv-fault-test.sock"
#define ALLOCATION_FAULTS 40

static TestBroker test;

/* ---- инъекция ------------------------------------------------------------ */

ErrorCode __real_memory_allocate(usize size, MemoryBlock *out_block);
ErrorCode __real_ipc_connection_send(IpcConnectionHandle connection, const IpcMessage *message);
ErrorCode __real_ipc_connection_flush(IpcConnectionHandle connection, bool *out_has_pending);
ErrorCode __real_ipc_message_destroy(IpcMessage *message);

/* 0 - отказов нет; иначе отказывает ровно этот по счёту вызов. */
static usize allocation_countdown;
static usize allocation_calls;

/* Отказ адресуется конкретному соединению: клиентская половина теста живёт в
 * том же процессе и через те же функции. */
static IpcConnectionHandle fault_connection;
static ErrorCode fault_send_error;
static ErrorCode fault_flush_error;

static usize destroy_calls;

ErrorCode __wrap_memory_allocate(usize size, MemoryBlock *out_block)
{
    allocation_calls++;
    if (allocation_countdown != 0 && allocation_calls == allocation_countdown) {
        return CORE_OUT_OF_MEMORY;
    }
    return __real_memory_allocate(size, out_block);
}

ErrorCode __wrap_ipc_connection_send(IpcConnectionHandle connection, const IpcMessage *message)
{
    if (connection == fault_connection && fault_send_error != CORE_OK) {
        return fault_send_error;
    }
    return __real_ipc_connection_send(connection, message);
}

ErrorCode __wrap_ipc_connection_flush(IpcConnectionHandle connection, bool *out_has_pending)
{
    if (connection == fault_connection && fault_flush_error != CORE_OK) {
        return fault_flush_error;
    }
    return __real_ipc_connection_flush(connection, out_has_pending);
}

ErrorCode __wrap_ipc_message_destroy(IpcMessage *message)
{
    destroy_calls++;
    return __real_ipc_message_destroy(message);
}

static void faults_off(void)
{
    allocation_countdown = 0;
    fault_connection = NULL;
    fault_send_error = CORE_OK;
    fault_flush_error = CORE_OK;
}

/* Соединение брокера с клиентом, у которого выданный client_id равен id. */
static IpcConnectionHandle broker_side(MsgClientId id)
{
    for (MsgClient *client = test.broker.clients; client != NULL; client = client->next) {
        if (client->id == id) {
            return client->connection;
        }
    }
    return NULL;
}

/* ---- проверки ------------------------------------------------------------ */

/*
 * Каждый принятый кадр освобождается ровно один раз. В этом окне тест сам
 * ничего не освобождает и ответов не читает, поэтому счётчик считает только
 * освобождения брокера.
 */
static void check_message_release(void)
{
    IpcConnectionHandle alpha = test_bound(&test, SOCKET_PATH, "alpha", TEST_ALPHA_SECRET,
                                           NULL);
    u8 frame[512];
    usize sent = 0;
    usize before;

    EXPECT(1, alpha != NULL);
    destroy_calls = 0;
    before = destroy_calls;
    for (usize i = 0; i < 8; ++i) {
        /* Подписка alpha на app.state разрешена, повторная даёт
         * CORE_ALREADY_EXISTS - кадр всё равно принят и освобождён. */
        EXPECT(2, test_send(alpha, MSG_TYPE_SUBSCRIBE, frame,
                            test_build_topic(frame, "app.state")));
        sent++;
    }
    for (usize i = 0; i < 8; ++i) {
        test_turn(&test);
    }
    EXPECT(3, destroy_calls - before == sent);
    (void)ipc_connection_close(&alpha);
    test_turn(&test);
}

/*
 * Нехватка памяти в любом вызове прохода. После каждой инъекции брокер обязан
 * остаться живым: следующая публикация без отказов доходит до подписчика.
 */
static void check_allocation_faults(void)
{
    u8 frame[512];
    static const u8 payload[4] = { 1, 2, 3, 4 };

    for (usize fault = 1; fault <= ALLOCATION_FAULTS; ++fault) {
        IpcConnectionHandle alpha;
        IpcConnectionHandle beta;
        IpcMessage reply = { 0, { NULL, 0 } };

        faults_off();
        alpha = test_bound(&test, SOCKET_PATH, "alpha", TEST_ALPHA_SECRET, NULL);
        beta = test_bound(&test, SOCKET_PATH, "beta", TEST_BETA_SECRET, NULL);
        EXPECT(4, alpha != NULL && beta != NULL);
        EXPECT(5, test_call(&test, beta, MSG_TYPE_SUBSCRIBE, frame,
                            test_build_topic(frame, "ui.theme"), &reply));
        (void)ipc_message_destroy(&reply);

        /* Отказ ставится на fault-й вызов внутри самого прохода. */
        allocation_calls = 0;
        allocation_countdown = fault;
        (void)test_send(alpha, MSG_TYPE_PUBLISH, frame,
                        test_build_publish(frame, sizeof(frame), "ui.theme", payload, 4));
        for (usize i = 0; i < 4; ++i) {
            test_turn(&test);
        }
        faults_off();

        /* Брокер жив и обслуживает дальше: подписка beta на месте, новая
         * публикация доходит. */
        EXPECT(6, test.broker.clients != NULL);
        {
            IpcConnectionHandle fresh_alpha = alpha;
            IpcMessage event = { 0, { NULL, 0 } };
            bool alive = test_send(fresh_alpha, MSG_TYPE_PUBLISH, frame,
                                   test_build_publish(frame, sizeof(frame), "ui.theme",
                                                      payload, 4));

            if (alive) {
                /* Ответ может быть и publish_result, и ранее отложенный
                 * кадр: важно лишь, что брокер отвечает. */
                if (test_receive(&test, fresh_alpha, &event)) {
                    EXPECT(7, event.payload.size >= 4);
                    (void)ipc_message_destroy(&event);
                }
            }
        }
        (void)ipc_connection_close(&alpha);
        (void)ipc_connection_close(&beta);
        for (usize i = 0; i < 4; ++i) {
            test_turn(&test);
        }
    }
    EXPECT(8, test.broker.clients == NULL);
}

/*
 * ipclib отказал с CORE_OVERFLOW: своя очередь брокера держит кадр, ничего не
 * теряется, и после снятия отказа кадр доезжает.
 */
static void check_send_would_block(void)
{
    MsgClientId beta_id = 0;
    IpcConnectionHandle alpha;
    IpcConnectionHandle beta;
    u8 frame[512];
    static const u8 payload[4] = { 5, 6, 7, 8 };
    IpcMessage reply = { 0, { NULL, 0 } };
    IpcMessage event = { 0, { NULL, 0 } };

    faults_off();
    alpha = test_bound(&test, SOCKET_PATH, "alpha", TEST_ALPHA_SECRET, NULL);
    beta = test_bound(&test, SOCKET_PATH, "beta", TEST_BETA_SECRET, &beta_id);
    EXPECT(9, alpha != NULL && beta != NULL);
    EXPECT(10, test_call(&test, beta, MSG_TYPE_SUBSCRIBE, frame,
                         test_build_topic(frame, "ui.theme"), &reply));
    (void)ipc_message_destroy(&reply);

    fault_connection = broker_side(beta_id);
    EXPECT(11, fault_connection != NULL);
    fault_send_error = CORE_OVERFLOW;
    EXPECT(12, test_call(&test, alpha, MSG_TYPE_PUBLISH, frame,
                         test_build_publish(frame, sizeof(frame), "ui.theme", payload, 4),
                         &reply));
    /* Для издателя публикация удалась: кадр принят в память брокера. */
    {
        MsgReader reader = msg_reader(reply.payload.data, reply.payload.size);

        EXPECT(13, msg_read_u32(&reader) == (u32)CORE_OK);
        EXPECT(14, msg_read_u16(&reader) == 1);
        EXPECT(15, msg_read_u16(&reader) == 0);
    }
    (void)ipc_message_destroy(&reply);
    /* Пока отказ держится, подписчик ничего не получает. */
    {
        IpcReceiveStatus status = IPC_RECEIVE_STATUS_NO_MESSAGE;

        test_turn(&test);
        EXPECT(16, ipc_connection_receive(beta, &status, &event) == CORE_OK);
        EXPECT(17, status == IPC_RECEIVE_STATUS_NO_MESSAGE);
    }
    /* Отказ снят: кадр из очереди брокера доезжает без повторной публикации. */
    faults_off();
    EXPECT(18, test_receive(&test, beta, &event));
    EXPECT(19, event.type == MSG_TYPE_EVENT);
    (void)ipc_message_destroy(&event);

    (void)ipc_connection_close(&alpha);
    (void)ipc_connection_close(&beta);
    for (usize i = 0; i < 4; ++i) {
        test_turn(&test);
    }
}

/* Жёсткая ошибка записи: соединение закрывается, остальные не страдают. */
static void check_send_hard_error(void)
{
    MsgClientId beta_id = 0;
    MsgClientId alpha_id = 0;
    IpcConnectionHandle alpha;
    IpcConnectionHandle beta;
    u8 frame[512];
    static const u8 payload[4] = { 9, 9, 9, 9 };
    IpcMessage reply = { 0, { NULL, 0 } };
    usize before;

    faults_off();
    alpha = test_bound(&test, SOCKET_PATH, "alpha", TEST_ALPHA_SECRET, &alpha_id);
    beta = test_bound(&test, SOCKET_PATH, "beta", TEST_BETA_SECRET, &beta_id);
    EXPECT(20, alpha != NULL && beta != NULL);
    EXPECT(21, test_call(&test, beta, MSG_TYPE_SUBSCRIBE, frame,
                         test_build_topic(frame, "ui.theme"), &reply));
    (void)ipc_message_destroy(&reply);
    before = test.broker.client_count;
    EXPECT(22, before == 2);

    fault_connection = broker_side(beta_id);
    fault_send_error = CORE_IO_ERROR;
    (void)test_send(alpha, MSG_TYPE_PUBLISH, frame,
                    test_build_publish(frame, sizeof(frame), "ui.theme", payload, 4));
    for (usize i = 0; i < 4; ++i) {
        test_turn(&test);
    }
    faults_off();
    /* Упало одно соединение, а не брокер. */
    EXPECT(23, test.broker.client_count == 1);
    EXPECT(24, broker_side(alpha_id) != NULL);

    (void)ipc_connection_close(&alpha);
    (void)ipc_connection_close(&beta);
    for (usize i = 0; i < 4; ++i) {
        test_turn(&test);
    }
    EXPECT(25, test.broker.clients == NULL);
}

static void check_flush_error(void)
{
    MsgClientId alpha_id = 0;
    IpcConnectionHandle alpha;
    u8 frame[512];

    faults_off();
    alpha = test_bound(&test, SOCKET_PATH, "alpha", TEST_ALPHA_SECRET, &alpha_id);
    EXPECT(26, alpha != NULL);
    fault_connection = broker_side(alpha_id);
    EXPECT(27, fault_connection != NULL);
    fault_flush_error = CORE_IO_ERROR;
    (void)test_send(alpha, MSG_TYPE_SUBSCRIBE, frame, test_build_topic(frame, "app.state"));
    for (usize i = 0; i < 4; ++i) {
        test_turn(&test);
    }
    faults_off();
    EXPECT(28, test.broker.client_count == 0);
    (void)ipc_connection_close(&alpha);
    test_turn(&test);
}

int main(void)
{
    faults_off();
    EXPECT(29, test_broker_start(&test, SOCKET_PATH));
    check_message_release();
    check_allocation_faults();
    check_send_would_block();
    check_send_hard_error();
    check_flush_error();
    faults_off();
    test_broker_stop(&test);
    check_passed();
    return 0;
}
