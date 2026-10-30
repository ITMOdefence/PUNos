#ifndef PUNOS_MSGSERV_H
#define PUNOS_MSGSERV_H

/*
 * msgserv - брокер сообщений PUNos.
 *
 * Спецификация: DevDoc/SystemServices/base/msgserv.md в PUNos.
 *
 * Заголовок описывает протокол v1 и лимиты, которые брокер обязан соблюдать.
 * Клиентская сторона протокола живёт в модуле messaging библиотеки
 * appframework; константы там объявлены в appframework/app.h и обязаны
 * совпадать с этими значениями байт-в-байт.
 */

#include <special/ipc.h>
#include <sys/core.h>
#include <sys/eventloop.h>
#include <sys/memory.h>
#include <sys/string.h>
#include <sys/time.h>

/* ------------------------------------------------------------------------ */
/* Константы                                                                */
/* ------------------------------------------------------------------------ */

#ifndef MSG_ENDPOINT
#define MSG_ENDPOINT "/run/punos/msgserv.sock"
#endif

/* Версия протокола, первое поле каждого запроса клиента. */
#define MSG_PROTOCOL_VERSION ((u16)1)

/* Длиннейший application_id; совпадает с APP_ID_MAX_LENGTH appframework. */
#define MSG_APPLICATION_ID_MAX_LENGTH ((usize)64)

/* Длиннейший секрет из environment приложения. */
#define MSG_SECRET_MAX_LENGTH ((usize)128)

/* 64 КиБ x 64 подписчика = 4 МиБ, то есть полный fan-out влезает в
 * IPC_OUTGOING_QUEUE_MAX_SIZE. */
#define MSG_PAYLOAD_MAX_SIZE ((usize)65536)

/* Своя очередь получателя мягче очереди ipclib, чтобы отказ приходил от
 * брокера, а не из транспорта. */
#define MSG_QUEUE_MAX_RECORDS ((usize)1024)
#define MSG_QUEUE_MAX_BYTES ((usize)1048576)

/* Столько же, сколько записей в каталоге приложений. */
#define MSG_CLIENTS_MAX ((usize)64)

/* Секрет один на приложение, поэтому число его соединений ограничено явно. */
#define MSG_CONNECTIONS_MAX_PER_APPLICATION ((usize)4)

#define MSG_TOPIC_MAX_LENGTH ((usize)128)
#define MSG_METHOD_MAX_LENGTH ((usize)64)

/* Ограничивает работу на один publish. */
#define MSG_SUBSCRIPTIONS_MAX_PER_CLIENT ((usize)32)

/* Как APP_APPS_MAX_PENDING. */
#define MSG_PENDING_MAX_PER_CLIENT ((usize)256)

/* Предел жизни запроса без ответа. */
#define MSG_REQUEST_TIMEOUT_MS ((u64)30000)

/* Как таймаут logserv в initserv. */
#define MSG_BIND_TIMEOUT_MS ((u64)5000)

/* Сколько кадров брокер разбирает за один проход по соединению. */
#define MSG_FRAMES_PER_TURN ((usize)64)

/* ------------------------------------------------------------------------ */
/* Типы кадров                                                              */
/* ------------------------------------------------------------------------ */

/*
 * Запрос клиента имеет нечётный тип, ответ на него - тип запроса плюс один,
 * как в протоколе appserv. Незапрошенные кадры брокера лежат отдельным
 * блоком от 20: клиент обязан отличать их от ответа на свой запрос, и
 * разрыв в нумерации делает ошибку видимой.
 */
typedef enum MsgMessageType {
    MSG_TYPE_BIND = 1,
    MSG_TYPE_BIND_RESULT = 2,
    MSG_TYPE_SUBSCRIBE = 3,
    MSG_TYPE_SUBSCRIBE_RESULT = 4,
    MSG_TYPE_UNSUBSCRIBE = 5,
    MSG_TYPE_UNSUBSCRIBE_RESULT = 6,
    MSG_TYPE_PUBLISH = 7,
    MSG_TYPE_PUBLISH_RESULT = 8,
    MSG_TYPE_REQUEST = 9,
    MSG_TYPE_REQUEST_RESULT = 10,
    MSG_TYPE_RESPOND = 11,
    MSG_TYPE_RESPOND_RESULT = 12,
    MSG_TYPE_EVENT = 20,
    MSG_TYPE_DELIVER_REQUEST = 21,
    MSG_TYPE_DELIVER_RESPONSE = 22
} MsgMessageType;

/* Как адресован request: соединением или приложением. */
typedef enum MsgTargetKind {
    MSG_TARGET_CLIENT = 0,
    MSG_TARGET_APPLICATION = 1
} MsgTargetKind;

/* ------------------------------------------------------------------------ */
/* Каталог прав                                                             */
/* ------------------------------------------------------------------------ */

/*
 * Запись каталога: одно приложение, его секрет и его права. Генератор
 * tools/generate_messaging.py создаёт массив таких записей из
 * config/messaging.json и копии platform_registry.json.
 */
typedef struct MsgAppEntry {
    String application_id;
    String secret;
    const String *publish_topics;
    usize publish_topic_count;
    const String *subscribe_topics;
    usize subscribe_topic_count;
    const String *serve_methods;
    usize serve_method_count;
    const String *call_targets;
    usize call_target_count;
} MsgAppEntry;

typedef struct MsgCatalog {
    const MsgAppEntry *entries;
    usize count;
} MsgCatalog;

/* Определён в сгенерированном файле build/generated/messaging_catalog.c. */
extern const MsgCatalog msg_generated_catalog;

/* ------------------------------------------------------------------------ */
/* Состояние брокера                                                        */
/* ------------------------------------------------------------------------ */

typedef u64 MsgClientId;
typedef u64 MsgCorrelationId;

/* Один кадр, поставленный в очередь получателя. */
typedef struct MsgFrame MsgFrame;

/* Подписка одного соединения: topic и счётчик sequence по этому topic'у. */
typedef struct MsgSubscription {
    u8 topic[MSG_TOPIC_MAX_LENGTH];
    usize topic_length;
    u64 sequence;
    bool used;
} MsgSubscription;

/*
 * Запрос, ожидающий ответа. Живёт у отправителя, как APP_APPS_MAX_PENDING в
 * appframework; адресат помнит только correlation_id внутри доставленного
 * кадра. Поэтому respond ищет запись перебором соединений - тот же перебор
 * нужен и при обрыве соединения адресата.
 */
typedef struct MsgPending {
    MsgCorrelationId correlation;
    MsgClientId target;
    u64 deadline_ms;
    bool used;
} MsgPending;

typedef struct MsgClient MsgClient;

struct MsgClient {
    MemoryBlock block;
    MsgClient *next;
    IpcConnectionHandle connection;
    EventSourceId source;
    MsgClientId id;
    const MsgAppEntry *entry; /* NULL, пока bind не прошёл */
    u64 connected_ms;         /* для MSG_BIND_TIMEOUT_MS */
    bool buffered;            /* ipclib мог сохранить ещё один кадр */
    bool write_interest;
    MsgSubscription subscriptions[MSG_SUBSCRIPTIONS_MAX_PER_CLIENT];
    usize subscription_count;
    MsgPending pending[MSG_PENDING_MAX_PER_CLIENT];
    usize pending_count;
    MsgFrame *queue_head;
    MsgFrame *queue_tail;
    usize queue_records;
    usize queue_bytes;
};

typedef struct MsgBroker {
    EventLoopHandle loop;
    IpcServerHandle server;
    EventSourceId listener;
    const MsgCatalog *catalog;
    MsgClient *clients;
    usize client_count;
    MsgClientId next_client_id;
    MsgCorrelationId next_correlation;
    void (*log)(void *context, const char *event, const char *application_id,
                MsgClientId client, ErrorCode error);
    void *log_context;
} MsgBroker;

/* ------------------------------------------------------------------------ */
/* Операции                                                                */
/* ------------------------------------------------------------------------ */

/*
 * Готовит брокера к работе: каталог, пустые таблицы, первые идентификаторы.
 * Поля log и log_context обнуляются, поэтому вызывающий ставит журнал после
 * этого вызова.
 */
ErrorCode msg_broker_init(MsgBroker *broker, const MsgCatalog *catalog, EventLoopHandle loop);

/* Принимает все ожидающие соединения; лишние выше MSG_CLIENTS_MAX
 * закрываются сразу. */
ErrorCode msg_broker_accept(MsgBroker *broker);

/*
 * Разбирает кадры одного соединения и ставит ответы в его очередь.
 * CORE_NOT_FOUND - соединение надо закрыть (peer ушёл или нарушил протокол).
 */
ErrorCode msg_broker_serve(MsgBroker *broker, MsgClient *client, u64 now_ms);

/* Отдаёт очередь клиента в ipclib, насколько получится без блокировки. */
ErrorCode msg_broker_drain(MsgBroker *broker, MsgClient *client);

/*
 * Отдаёт непустые очереди всех клиентов и снимает тех, у кого запись упала.
 * Без этого кадр, положенный в очередь чужим publish'ом или истёкшим
 * дедлайном, лежал бы до следующего события на сокете получателя, которого
 * уровневый epoll не даст: читать там нечего, а запись брокер не запрашивал.
 */
void msg_broker_flush_all(MsgBroker *broker);

/* Завершает запросы, чей дедлайн истёк, и соединения, не сделавшие bind. */
void msg_broker_tick(MsgBroker *broker, u64 now_ms);

/* Находит клиента по источнику event loop'а. */
MsgClient **msg_broker_find_source(MsgBroker *broker, EventSourceId source);

/* Закрывает соединение: снимает подписки, гасит его запросы, освобождает
 * очередь. *link указывает на следующего клиента после возврата. */
void msg_broker_close_client(MsgBroker *broker, MsgClient **link);

/* Освобождает всё состояние брокера. */
void msg_broker_shutdown(MsgBroker *broker);

#endif
