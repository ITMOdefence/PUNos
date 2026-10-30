/*
 * Логика брокера msgserv: соединения, личность, подписки, рассылка и
 * корреляция запросов. Файл не знает ни про главный цикл, ни про logserv:
 * всё наружу уходит через msgserv.h, поэтому юнит-тесты собирают его без
 * main.c.
 *
 * Правило, от которого зависит безопасность: поля отправителя в event и
 * deliver_request пишет только этот файл, из записи каталога, подтверждённой
 * при bind. Содержимое кадра клиента на них не влияет.
 */

#include <msgserv.h>

#include "wire.h"

/* Один кадр в очереди получателя. Данные лежат сразу за структурой. */
struct MsgFrame {
    MemoryBlock block;
    MsgFrame *next;
    IpcMessageType type;
    usize size;
};

/* Наибольший ответ брокера без полезной нагрузки: bind_result с полным
 * application_id. */
#define MSG_SMALL_REPLY_MAX (4 + 2 + MSG_APPLICATION_ID_MAX_LENGTH + 8)

static u8 *frame_data(MsgFrame *frame)
{
    return (u8 *)frame + sizeof(MsgFrame);
}

static void broker_log(MsgBroker *broker, const char *event, const MsgClient *client,
                       ErrorCode error)
{
    const char *application_id = NULL;

    if (broker->log == NULL) {
        return;
    }
    if (client != NULL && client->entry != NULL) {
        application_id = client->entry->application_id.data;
    }
    broker->log(broker->log_context, event, application_id,
                client != NULL ? client->id : 0, error);
}

/* ------------------------------------------------------------------------ */
/* Очередь получателя                                                       */
/* ------------------------------------------------------------------------ */

static void queue_release(MsgClient *client)
{
    while (client->queue_head != NULL) {
        MsgFrame *frame = client->queue_head;
        MemoryBlock block = frame->block;

        client->queue_head = frame->next;
        (void)memory_free(&block);
    }
    client->queue_tail = NULL;
    client->queue_records = 0;
    client->queue_bytes = 0;
}

/*
 * Ставит кадр в очередь получателя. CORE_OVERFLOW - очередь полна: кадр
 * отброшен только для этого получателя, остальные его получат.
 */
static ErrorCode queue_push(MsgClient *client, IpcMessageType type, const u8 *data, usize size)
{
    MemoryBlock block = { NULL, 0 };
    MsgFrame *frame;
    ErrorCode error;

    if (client->queue_records >= MSG_QUEUE_MAX_RECORDS ||
        client->queue_bytes + size > MSG_QUEUE_MAX_BYTES) {
        return CORE_OVERFLOW;
    }
    error = memory_allocate(sizeof(MsgFrame) + size, &block);
    if (error != CORE_OK) {
        return error;
    }
    frame = (MsgFrame *)block.data;
    frame->block = block;
    frame->next = NULL;
    frame->type = type;
    frame->size = size;
    for (usize i = 0; i < size; ++i) {
        frame_data(frame)[i] = data[i];
    }
    if (client->queue_tail == NULL) {
        client->queue_head = frame;
    } else {
        client->queue_tail->next = frame;
    }
    client->queue_tail = frame;
    client->queue_records++;
    client->queue_bytes += size;
    return CORE_OK;
}

ErrorCode msg_broker_drain(MsgBroker *broker, MsgClient *client)
{
    bool pending = false;
    ErrorCode error;

    while (client->queue_head != NULL) {
        MsgFrame *frame = client->queue_head;
        IpcMessage message = { frame->type, { frame_data(frame), frame->size } };
        MemoryBlock block;

        error = ipc_connection_send(client->connection, &message);
        if (error == CORE_OVERFLOW) {
            break; /* очередь ipclib полна: остальное подождёт записи */
        }
        if (error != CORE_OK) {
            return error;
        }
        client->queue_head = frame->next;
        if (client->queue_head == NULL) {
            client->queue_tail = NULL;
        }
        client->queue_records--;
        client->queue_bytes -= frame->size;
        block = frame->block;
        (void)memory_free(&block);
    }
    error = ipc_connection_flush(client->connection, &pending);
    if (error != CORE_OK) {
        return error;
    }
    pending = pending || client->queue_head != NULL;
    if (pending != client->write_interest) {
        error = eventloop_update_fd(broker->loop, client->source,
                                    EVENT_READABLE | (pending ? EVENT_WRITABLE : 0));
        if (error != CORE_OK) {
            return error;
        }
        client->write_interest = pending;
    }
    return CORE_OK;
}

/* Ответ клиенту на его собственный запрос. Отказ очереди здесь означает, что
 * клиент шлёт запросы и не читает ответы: такое соединение закрывается. */
static ErrorCode reply(MsgClient *client, IpcMessageType type, const u8 *data, usize size)
{
    ErrorCode error = queue_push(client, type, data, size);

    return error == CORE_OK ? CORE_OK : CORE_NOT_FOUND;
}

static ErrorCode reply_error(MsgClient *client, IpcMessageType type, ErrorCode code)
{
    u8 payload[4];
    MsgWriter writer = msg_writer(payload, sizeof(payload));

    msg_write_u32(&writer, (u32)code);
    return reply(client, type, payload, writer.offset);
}

/* ------------------------------------------------------------------------ */
/* Каталог и личность                                                       */
/* ------------------------------------------------------------------------ */

static const MsgAppEntry *catalog_find(const MsgCatalog *catalog, const u8 *id, usize length)
{
    for (usize i = 0; i < catalog->count; ++i) {
        const MsgAppEntry *entry = &catalog->entries[i];

        if (msg_bytes_equal((const u8 *)entry->application_id.data,
                            entry->application_id.length, id, length)) {
            return entry;
        }
    }
    return NULL;
}

static bool list_contains(const String *list, usize count, const u8 *value, usize length)
{
    for (usize i = 0; i < count; ++i) {
        if (msg_bytes_equal((const u8 *)list[i].data, list[i].length, value, length)) {
            return true;
        }
    }
    return false;
}

static usize connections_of(const MsgBroker *broker, const MsgAppEntry *entry)
{
    usize count = 0;

    for (const MsgClient *client = broker->clients; client != NULL; client = client->next) {
        if (client->entry == entry) {
            count++;
        }
    }
    return count;
}

static MsgClient *client_by_id(MsgBroker *broker, MsgClientId id)
{
    for (MsgClient *client = broker->clients; client != NULL; client = client->next) {
        if (client->id == id && client->entry != NULL) {
            return client;
        }
    }
    return NULL;
}

/*
 * Единственное подключённое соединение приложения. CORE_NOT_FOUND - ни
 * одного, CORE_ALREADY_EXISTS - несколько: адресация по application_id это
 * удобство для случая "подключён ровно один экземпляр".
 */
static ErrorCode client_by_application(MsgBroker *broker, const u8 *id, usize length,
                                       MsgClient **out_client)
{
    MsgClient *found = NULL;

    for (MsgClient *client = broker->clients; client != NULL; client = client->next) {
        if (client->entry == NULL ||
            !msg_bytes_equal((const u8 *)client->entry->application_id.data,
                             client->entry->application_id.length, id, length)) {
            continue;
        }
        if (found != NULL) {
            return CORE_ALREADY_EXISTS;
        }
        found = client;
    }
    if (found == NULL) {
        return CORE_NOT_FOUND;
    }
    *out_client = found;
    return CORE_OK;
}

/* ------------------------------------------------------------------------ */
/* Подписки                                                                 */
/* ------------------------------------------------------------------------ */

static MsgSubscription *subscription_find(MsgClient *client, const u8 *topic, usize length)
{
    for (usize i = 0; i < MSG_SUBSCRIPTIONS_MAX_PER_CLIENT; ++i) {
        MsgSubscription *subscription = &client->subscriptions[i];

        if (subscription->used &&
            msg_bytes_equal(subscription->topic, subscription->topic_length, topic, length)) {
            return subscription;
        }
    }
    return NULL;
}

static ErrorCode subscription_add(MsgClient *client, const u8 *topic, usize length)
{
    for (usize i = 0; i < MSG_SUBSCRIPTIONS_MAX_PER_CLIENT; ++i) {
        MsgSubscription *subscription = &client->subscriptions[i];

        if (subscription->used) {
            continue;
        }
        for (usize byte = 0; byte < length; ++byte) {
            subscription->topic[byte] = topic[byte];
        }
        subscription->topic_length = length;
        subscription->sequence = 0;
        subscription->used = true;
        client->subscription_count++;
        return CORE_OK;
    }
    return CORE_OVERFLOW;
}

/* ------------------------------------------------------------------------ */
/* Запросы, ожидающие ответа                                                */
/* ------------------------------------------------------------------------ */

static MsgPending *pending_reserve(MsgClient *caller)
{
    for (usize i = 0; i < MSG_PENDING_MAX_PER_CLIENT; ++i) {
        if (!caller->pending[i].used) {
            return &caller->pending[i];
        }
    }
    return NULL;
}

/* Сообщает отправителю исход запроса и освобождает запись. */
static ErrorCode pending_finish(MsgClient *caller, MsgPending *pending, ErrorCode error,
                                u32 status, const u8 *payload, usize payload_size)
{
    u8 header[16];
    MsgWriter writer = msg_writer(header, sizeof(header));
    MemoryBlock block = { NULL, 0 };
    ErrorCode queued;

    msg_write_u64(&writer, pending->correlation);
    msg_write_u32(&writer, (u32)error);
    msg_write_u32(&writer, status);
    pending->used = false;
    caller->pending_count--;
    if (payload_size == 0) {
        u8 empty[20];
        MsgWriter tail = msg_writer(empty, sizeof(empty));

        msg_write_bytes(&tail, header, writer.offset);
        msg_write_blob(&tail, NULL, 0);
        return queue_push(caller, MSG_TYPE_DELIVER_RESPONSE, empty, tail.offset);
    }
    queued = memory_allocate(writer.offset + 4 + payload_size, &block);
    if (queued != CORE_OK) {
        return queued;
    }
    {
        MsgWriter body = msg_writer(block.data, block.size);

        msg_write_bytes(&body, header, writer.offset);
        msg_write_blob(&body, payload, payload_size);
        queued = queue_push(caller, MSG_TYPE_DELIVER_RESPONSE, block.data, body.offset);
    }
    (void)memory_free(&block);
    return queued;
}

/* ------------------------------------------------------------------------ */
/* Обработчики кадров                                                       */
/* ------------------------------------------------------------------------ */

static ErrorCode handle_bind(MsgBroker *broker, MsgClient *client, MsgReader *reader)
{
    MsgField id;
    MsgField secret;
    const MsgAppEntry *entry;
    u8 payload[MSG_SMALL_REPLY_MAX];
    MsgWriter writer = msg_writer(payload, sizeof(payload));
    ErrorCode error = CORE_OK;

    id = msg_read_text(reader, 1, MSG_APPLICATION_ID_MAX_LENGTH);
    secret = msg_read_text(reader, 1, MSG_SECRET_MAX_LENGTH);
    if (!msg_reader_done(reader)) {
        return CORE_NOT_FOUND; /* кадр не по протоколу: соединение закрыть */
    }
    if (client->entry != NULL) {
        return reply_error(client, MSG_TYPE_BIND_RESULT, CORE_INVALID_STATE);
    }
    entry = catalog_find(broker->catalog, id.data, id.length);
    if (entry == NULL) {
        error = CORE_NOT_FOUND;
    } else if (!msg_bytes_equal_constant_time((const u8 *)entry->secret.data,
                                              entry->secret.length, secret.data,
                                              secret.length)) {
        error = CORE_PERMISSION_DENIED;
    } else if (connections_of(broker, entry) >= MSG_CONNECTIONS_MAX_PER_APPLICATION) {
        error = CORE_OVERFLOW;
    }
    if (error != CORE_OK) {
        /* Ни секрета, ни различий между "нет такого приложения" и "не тот
         * секрет" в журнале: иначе брокер подсказывает подбирающему. */
        broker_log(broker, "bind rejected", NULL, error);
        return reply_error(client, MSG_TYPE_BIND_RESULT, error);
    }
    client->entry = entry;
    client->id = broker->next_client_id++;
    msg_write_u32(&writer, (u32)CORE_OK);
    msg_write_text(&writer, (const u8 *)entry->application_id.data,
                   entry->application_id.length);
    msg_write_u64(&writer, client->id);
    broker_log(broker, "bind", client, CORE_OK);
    return reply(client, MSG_TYPE_BIND_RESULT, payload, writer.offset);
}

static ErrorCode handle_subscribe(MsgClient *client, MsgReader *reader, bool subscribe)
{
    IpcMessageType type = subscribe ? MSG_TYPE_SUBSCRIBE_RESULT : MSG_TYPE_UNSUBSCRIBE_RESULT;
    MsgField topic = msg_read_text(reader, 1, MSG_TOPIC_MAX_LENGTH);
    MsgSubscription *existing;

    if (!msg_reader_done(reader)) {
        return CORE_NOT_FOUND;
    }
    existing = subscription_find(client, topic.data, topic.length);
    if (!subscribe) {
        if (existing == NULL) {
            return reply_error(client, type, CORE_NOT_FOUND);
        }
        existing->used = false;
        client->subscription_count--;
        return reply_error(client, type, CORE_OK);
    }
    if (!list_contains(client->entry->subscribe_topics, client->entry->subscribe_topic_count,
                       topic.data, topic.length)) {
        return reply_error(client, type, CORE_PERMISSION_DENIED);
    }
    if (existing != NULL) {
        return reply_error(client, type, CORE_ALREADY_EXISTS);
    }
    return reply_error(client, type, subscription_add(client, topic.data, topic.length));
}

/*
 * Рассылка. Подписчик, потерявший право получать topic, молча не попадает в
 * рассылку и не учитывается в счётчиках: иначе издатель узнавал бы о правах
 * чужих приложений. sequence растёт и у того, кому кадр не влез, - разрыв в
 * нумерации и есть сигнал "я пропустил события".
 */
static ErrorCode handle_publish(MsgBroker *broker, MsgClient *client, MsgReader *reader)
{
    MsgField topic = msg_read_text(reader, 1, MSG_TOPIC_MAX_LENGTH);
    MsgField payload = msg_read_blob(reader, MSG_PAYLOAD_MAX_SIZE);
    usize delivered = 0;
    usize dropped = 0;
    MemoryBlock block = { NULL, 0 };
    u8 result[12];
    MsgWriter writer = msg_writer(result, sizeof(result));
    ErrorCode error;

    if (!msg_reader_done(reader)) {
        return CORE_NOT_FOUND;
    }
    if (!list_contains(client->entry->publish_topics, client->entry->publish_topic_count,
                       topic.data, topic.length)) {
        return reply_error(client, MSG_TYPE_PUBLISH_RESULT, CORE_PERMISSION_DENIED);
    }
    error = memory_allocate(8 + 2 + MSG_APPLICATION_ID_MAX_LENGTH + 2 + MSG_TOPIC_MAX_LENGTH +
                                8 + 4 + payload.length,
                            &block);
    if (error != CORE_OK) {
        return reply_error(client, MSG_TYPE_PUBLISH_RESULT, error);
    }
    for (MsgClient *peer = broker->clients; peer != NULL; peer = peer->next) {
        MsgSubscription *subscription;
        MsgWriter event;

        if (peer == client || peer->entry == NULL) {
            continue;
        }
        subscription = subscription_find(peer, topic.data, topic.length);
        if (subscription == NULL) {
            continue;
        }
        if (!list_contains(peer->entry->subscribe_topics, peer->entry->subscribe_topic_count,
                           topic.data, topic.length)) {
            continue;
        }
        subscription->sequence++;
        event = msg_writer(block.data, block.size);
        msg_write_u64(&event, client->id);
        msg_write_text(&event, (const u8 *)client->entry->application_id.data,
                       client->entry->application_id.length);
        msg_write_text(&event, topic.data, topic.length);
        msg_write_u64(&event, subscription->sequence);
        msg_write_blob(&event, payload.data, payload.length);
        if (event.bad) {
            dropped++;
            continue;
        }
        if (queue_push(peer, MSG_TYPE_EVENT, block.data, event.offset) == CORE_OK) {
            delivered++;
        } else {
            dropped++;
        }
    }
    (void)memory_free(&block);
    msg_write_u32(&writer, (u32)CORE_OK);
    msg_write_u16(&writer, (u16)delivered);
    msg_write_u16(&writer, (u16)dropped);
    return reply(client, MSG_TYPE_PUBLISH_RESULT, result, writer.offset);
}

static ErrorCode handle_request(MsgBroker *broker, MsgClient *client, MsgReader *reader,
                                u64 now_ms)
{
    u8 kind = msg_read_u8(reader);
    MsgClient *target = NULL;
    MsgField target_id = { NULL, 0 };
    MsgField method;
    MsgField payload;
    MsgPending *pending;
    MemoryBlock block = { NULL, 0 };
    MsgWriter deliver;
    u8 result[12];
    MsgWriter writer = msg_writer(result, sizeof(result));
    ErrorCode error;

    if (kind == MSG_TARGET_CLIENT) {
        MsgClientId id = msg_read_u64(reader);

        target = client_by_id(broker, id);
        error = target != NULL ? CORE_OK : CORE_NOT_FOUND;
    } else if (kind == MSG_TARGET_APPLICATION) {
        target_id = msg_read_text(reader, 1, MSG_APPLICATION_ID_MAX_LENGTH);
        error = reader->bad ? CORE_OK
                            : client_by_application(broker, target_id.data, target_id.length,
                                                    &target);
    } else {
        reader->bad = true;
        error = CORE_OK;
    }
    method = msg_read_text(reader, 1, MSG_METHOD_MAX_LENGTH);
    payload = msg_read_blob(reader, MSG_PAYLOAD_MAX_SIZE);
    if (!msg_reader_done(reader)) {
        return CORE_NOT_FOUND;
    }
    if (error != CORE_OK) {
        return reply_error(client, MSG_TYPE_REQUEST_RESULT, error);
    }
    /* Права проверяются с обеих сторон: вызывающему разрешён адресат, а
     * адресату - метод. */
    if (!list_contains(client->entry->call_targets, client->entry->call_target_count,
                       (const u8 *)target->entry->application_id.data,
                       target->entry->application_id.length) ||
        !list_contains(target->entry->serve_methods, target->entry->serve_method_count,
                       method.data, method.length)) {
        return reply_error(client, MSG_TYPE_REQUEST_RESULT, CORE_PERMISSION_DENIED);
    }
    if (client->pending_count >= MSG_PENDING_MAX_PER_CLIENT) {
        return reply_error(client, MSG_TYPE_REQUEST_RESULT, CORE_WOULD_BLOCK);
    }
    error = memory_allocate(8 + 8 + 2 + MSG_APPLICATION_ID_MAX_LENGTH + 2 +
                                MSG_METHOD_MAX_LENGTH + 4 + payload.length,
                            &block);
    if (error != CORE_OK) {
        return reply_error(client, MSG_TYPE_REQUEST_RESULT, error);
    }
    pending = pending_reserve(client);
    if (pending == NULL) {
        (void)memory_free(&block);
        return reply_error(client, MSG_TYPE_REQUEST_RESULT, CORE_WOULD_BLOCK);
    }
    deliver = msg_writer(block.data, block.size);
    msg_write_u64(&deliver, broker->next_correlation);
    msg_write_u64(&deliver, client->id);
    msg_write_text(&deliver, (const u8 *)client->entry->application_id.data,
                   client->entry->application_id.length);
    msg_write_text(&deliver, method.data, method.length);
    msg_write_blob(&deliver, payload.data, payload.length);
    error = deliver.bad ? CORE_OVERFLOW
                        : queue_push(target, MSG_TYPE_DELIVER_REQUEST, block.data,
                                     deliver.offset);
    (void)memory_free(&block);
    if (error != CORE_OK) {
        /* Адресат не читает сокет: запрос не принят, запись не заведена. */
        return reply_error(client, MSG_TYPE_REQUEST_RESULT, CORE_WOULD_BLOCK);
    }
    pending->correlation = broker->next_correlation++;
    pending->target = target->id;
    pending->deadline_ms = now_ms + MSG_REQUEST_TIMEOUT_MS;
    pending->used = true;
    client->pending_count++;
    msg_write_u32(&writer, (u32)CORE_OK);
    msg_write_u64(&writer, pending->correlation);
    return reply(client, MSG_TYPE_REQUEST_RESULT, result, writer.offset);
}

static ErrorCode handle_respond(MsgBroker *broker, MsgClient *client, MsgReader *reader)
{
    MsgCorrelationId correlation = msg_read_u64(reader);
    u32 status = msg_read_u32(reader);
    MsgField payload = msg_read_blob(reader, MSG_PAYLOAD_MAX_SIZE);
    ErrorCode error;

    if (!msg_reader_done(reader)) {
        return CORE_NOT_FOUND;
    }
    for (MsgClient *caller = broker->clients; caller != NULL; caller = caller->next) {
        for (usize i = 0; i < MSG_PENDING_MAX_PER_CLIENT; ++i) {
            MsgPending *pending = &caller->pending[i];

            if (!pending->used || pending->correlation != correlation) {
                continue;
            }
            /* Ответить может только тот, кому запрос был доставлен. */
            if (pending->target != client->id) {
                return reply_error(client, MSG_TYPE_RESPOND_RESULT, CORE_PERMISSION_DENIED);
            }
            error = pending_finish(caller, pending, CORE_OK, status, payload.data,
                                   payload.length);
            return reply_error(client, MSG_TYPE_RESPOND_RESULT, error);
        }
    }
    return reply_error(client, MSG_TYPE_RESPOND_RESULT, CORE_NOT_FOUND);
}

/* ------------------------------------------------------------------------ */
/* Соединения                                                               */
/* ------------------------------------------------------------------------ */

ErrorCode msg_broker_init(MsgBroker *broker, const MsgCatalog *catalog, EventLoopHandle loop)
{
    if (broker == NULL || catalog == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    broker->loop = loop;
    broker->server = NULL;
    broker->listener = 0;
    broker->catalog = catalog;
    broker->clients = NULL;
    broker->client_count = 0;
    broker->next_client_id = 1;
    broker->next_correlation = 1;
    broker->log = NULL;
    broker->log_context = NULL;
    return CORE_OK;
}

ErrorCode msg_broker_accept(MsgBroker *broker)
{
    for (usize i = 0; i < MSG_CLIENTS_MAX; ++i) {
        IpcConnectionHandle connection = NULL;
        MemoryBlock block = { NULL, 0 };
        MsgClient *client;
        MonotonicTime now = 0;
        i32 fd = -1;
        ErrorCode error = ipc_server_accept(broker->server, &connection);

        if (error == CORE_WOULD_BLOCK) {
            return CORE_OK;
        }
        if (error != CORE_OK) {
            return error;
        }
        if (broker->client_count >= MSG_CLIENTS_MAX) {
            /* Мест нет: соединение закрывается сразу, без bind_result. */
            (void)ipc_connection_close(&connection);
            broker_log(broker, "connection refused", NULL, CORE_OVERFLOW);
            continue;
        }
        error = memory_allocate(sizeof(MsgClient), &block);
        if (error != CORE_OK) {
            (void)ipc_connection_close(&connection);
            return error;
        }
        client = (MsgClient *)block.data;
        client->block = block;
        client->connection = connection;
        client->buffered = true;
        (void)time_monotonic_now(&now);
        client->connected_ms = now;
        error = ipc_connection_get_fd(connection, &fd);
        if (error == CORE_OK) {
            error = eventloop_add_fd(broker->loop, fd, EVENT_READABLE, &client->source);
        }
        if (error != CORE_OK) {
            (void)ipc_connection_close(&connection);
            (void)memory_free(&block);
            return error;
        }
        client->next = broker->clients;
        broker->clients = client;
        broker->client_count++;
    }
    return CORE_OK;
}

ErrorCode msg_broker_serve(MsgBroker *broker, MsgClient *client, u64 now_ms)
{
    client->buffered = true;
    for (usize i = 0; i < MSG_FRAMES_PER_TURN; ++i) {
        IpcMessage request = { 0, { NULL, 0 } };
        IpcReceiveStatus status = IPC_RECEIVE_STATUS_NO_MESSAGE;
        IpcMessageType type;
        MsgReader reader;
        ErrorCode error = ipc_connection_receive(client->connection, &status, &request);

        if (error != CORE_OK) {
            return error;
        }
        if (status == IPC_RECEIVE_STATUS_PEER_CLOSED) {
            return CORE_NOT_FOUND;
        }
        if (status == IPC_RECEIVE_STATUS_NO_MESSAGE) {
            client->buffered = false;
            break;
        }
        /* ipc_message_destroy() обнуляет и тип, поэтому он нужен заранее. */
        type = request.type;
        reader = msg_reader(request.payload.data, request.payload.size);
        if (msg_read_u16(&reader) != MSG_PROTOCOL_VERSION) {
            (void)ipc_message_destroy(&request);
            broker_log(broker, "protocol version rejected", client, CORE_UNSUPPORTED);
            return CORE_NOT_FOUND;
        }
        if (type != MSG_TYPE_BIND && client->entry == NULL) {
            /* До bind брокер не знает, кто это: отвечать на запросы нечем. */
            (void)ipc_message_destroy(&request);
            broker_log(broker, "request before bind", client, CORE_INVALID_STATE);
            return CORE_NOT_FOUND;
        }
        switch (type) {
        case MSG_TYPE_BIND:
            error = handle_bind(broker, client, &reader);
            break;
        case MSG_TYPE_SUBSCRIBE:
            error = handle_subscribe(client, &reader, true);
            break;
        case MSG_TYPE_UNSUBSCRIBE:
            error = handle_subscribe(client, &reader, false);
            break;
        case MSG_TYPE_PUBLISH:
            error = handle_publish(broker, client, &reader);
            break;
        case MSG_TYPE_REQUEST:
            error = handle_request(broker, client, &reader, now_ms);
            break;
        case MSG_TYPE_RESPOND:
            error = handle_respond(broker, client, &reader);
            break;
        default:
            /* Неизвестный message_type - закрытие соединения. */
            broker_log(broker, "unknown message type", client, CORE_UNSUPPORTED);
            error = CORE_NOT_FOUND;
            break;
        }
        (void)ipc_message_destroy(&request);
        if (error != CORE_OK) {
            return error;
        }
    }
    return msg_broker_drain(broker, client);
}

void msg_broker_flush_all(MsgBroker *broker)
{
    MsgClient **link = &broker->clients;

    while (*link != NULL) {
        MsgClient *client = *link;

        if (client->queue_head != NULL && msg_broker_drain(broker, client) != CORE_OK) {
            msg_broker_close_client(broker, link);
            continue;
        }
        link = &client->next;
    }
}

void msg_broker_tick(MsgBroker *broker, u64 now_ms)
{
    MsgClient **link = &broker->clients;

    while (*link != NULL) {
        MsgClient *client = *link;

        if (client->entry == NULL && now_ms - client->connected_ms >= MSG_BIND_TIMEOUT_MS) {
            broker_log(broker, "bind timeout", NULL, CORE_TIMEOUT);
            msg_broker_close_client(broker, link);
            continue;
        }
        for (usize i = 0; i < MSG_PENDING_MAX_PER_CLIENT && client->pending_count != 0; ++i) {
            MsgPending *pending = &client->pending[i];

            if (pending->used && now_ms >= pending->deadline_ms) {
                (void)pending_finish(client, pending, CORE_TIMEOUT, 0, NULL, 0);
            }
        }
        link = &client->next;
    }
}

MsgClient **msg_broker_find_source(MsgBroker *broker, EventSourceId source)
{
    MsgClient **link = &broker->clients;

    while (*link != NULL && (*link)->source != source) {
        link = &(*link)->next;
    }
    return *link != NULL ? link : NULL;
}

void msg_broker_close_client(MsgBroker *broker, MsgClient **link)
{
    MsgClient *client = *link;
    MsgClientId id = client->id;
    MemoryBlock block;

    /* Запросы, адресованные уходящему, завершаются у их отправителей: иначе
     * отправитель ждал бы до таймаута ответа, который уже невозможен. */
    for (MsgClient *caller = broker->clients; caller != NULL; caller = caller->next) {
        if (caller == client || id == 0) {
            continue;
        }
        for (usize i = 0; i < MSG_PENDING_MAX_PER_CLIENT && caller->pending_count != 0; ++i) {
            MsgPending *pending = &caller->pending[i];

            if (pending->used && pending->target == id) {
                (void)pending_finish(caller, pending, CORE_NOT_FOUND, 0, NULL, 0);
            }
        }
    }
    (void)eventloop_remove_source(broker->loop, client->source);
    (void)ipc_connection_close(&client->connection);
    queue_release(client);
    *link = client->next;
    broker->client_count--;
    block = client->block;
    (void)memory_free(&block);
}

void msg_broker_shutdown(MsgBroker *broker)
{
    while (broker->clients != NULL) {
        msg_broker_close_client(broker, &broker->clients);
    }
    if (broker->listener != 0) {
        (void)eventloop_remove_source(broker->loop, broker->listener);
        broker->listener = 0;
    }
    if (broker->server != NULL) {
        (void)ipc_server_close(&broker->server);
    }
}
