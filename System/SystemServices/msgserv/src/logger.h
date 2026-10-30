#ifndef MSGSERV_LOGGER_H
#define MSGSERV_LOGGER_H

/*
 * Неблокирующий клиент logserv для msgserv. Образец - AppLogger в appserv:
 * одно соединение в том же event loop'е, ограниченная очередь неподтверждённых
 * записей, отказ журнала не останавливает брокера.
 *
 * В журнал не попадают ни секреты, ни topic'и, ни payload: только имя события,
 * application_id, client_id и ErrorCode. Иначе брокер стал бы каналом утечки
 * прикладных данных в общесистемный журнал.
 */

#include <msgserv.h>
#include <special/ipc.h>
#include <sys/eventloop.h>

#ifndef MSG_LOG_ENDPOINT
#define MSG_LOG_ENDPOINT "/run/punos/logserv.sock"
#endif

typedef struct MsgLogger {
    IpcConnectionHandle connection;
    EventSourceId source;
    EventLoopHandle loop;
    bool connected;
    u64 outstanding;
    ErrorCode error;
    i32 pid;
    i32 tid;
} MsgLogger;

ErrorCode msg_logger_open(MsgLogger *logger, EventLoopHandle loop);

/* Обрабатывает событие соединения: завершает connect, читает подтверждения. */
ErrorCode msg_logger_event(MsgLogger *logger);

/* Подпись совпадает с полем log брокера. */
void msg_logger_record(void *context, const char *event, const char *application_id,
                       MsgClientId client, ErrorCode error);

void msg_logger_close(MsgLogger *logger);

#endif
