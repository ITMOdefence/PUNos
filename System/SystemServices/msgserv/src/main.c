/*
 * Жизненный цикл msgserv: Starting -> Ready -> Stopping.
 *
 * Starting:  event loop, клиент logserv, затем endpoint. Доступный
 *            /run/punos/msgserv.sock означает, что брокер готов принимать
 *            bind - как у logserv, где доступный endpoint означает открытое
 *            хранилище.
 * Ready:     один поток, один event loop, неблокирующие соединения.
 * Stopping:  по SIGTERM или SIGINT; соединения закрываются после попытки
 *            отдать поставленные кадры.
 *
 * Своих потоков сервис не создаёт: вся работа делается между двумя
 * eventloop_wait.
 */

#include <msgserv.h>
#include <punos/syscall/syscall.h>
#include <sys/process.h>
#include <sys/time.h>

#include "logger.h"
#include "messaging_catalog.h"

#define MSG_EVENT_BATCH ((usize)32)
#define MSG_TICK_MS ((DurationMs)1000)

static volatile i32 stopping;

static void stop_signal(i32 number)
{
    (void)number;
    stopping = 1;
}

static u64 now_ms(void)
{
    MonotonicTime now = 0;

    (void)time_monotonic_now(&now);
    return now;
}

/*
 * Соединение, у которого ipclib мог сохранить ещё один кадр, обслуживается
 * до следующего ожидания: иначе кадр лежал бы до нового события readable,
 * которого уровневый epoll уже не даст.
 */
static bool serve_buffered(MsgBroker *broker)
{
    MsgClient **link = &broker->clients;
    bool buffered = false;

    while (*link != NULL && !stopping) {
        MsgClient *client = *link;

        if (client->buffered && msg_broker_serve(broker, client, now_ms()) != CORE_OK) {
            msg_broker_close_client(broker, link);
            continue;
        }
        buffered = buffered || client->buffered;
        link = &client->next;
    }
    return buffered;
}

int main(int argc, char **argv, char **envp)
{
    MsgBroker broker = { 0 };
    MsgLogger logger = { 0 };
    EventLoopHandle loop = NULL;
    String endpoint = { (char *)MSG_ENDPOINT, sizeof(MSG_ENDPOINT) - 1 };
    struct sigaction action = { .sa_handler = stop_signal };
    i32 fd = -1;
    int result = PROCESS_EXIT_INITIALIZATION_FAILED;
    ErrorCode error;

    (void)argv;
    (void)envp;
    if (argc != 1) {
        return PROCESS_EXIT_INVALID_ARGUMENT; /* аргументов у брокера нет */
    }
    if (sigaction(SIGTERM, &action, NULL) != 0 || sigaction(SIGINT, &action, NULL) != 0) {
        return PROCESS_EXIT_INITIALIZATION_FAILED;
    }
    error = eventloop_create(&loop);
    if (error != CORE_OK) {
        return PROCESS_EXIT_INITIALIZATION_FAILED;
    }
    error = msg_broker_init(&broker, &msg_generated_catalog, loop);
    if (error != CORE_OK) {
        goto cleanup;
    }
    /* Недоступный logserv не мешает брокеру: записи просто не уходят.
     * Одно обслуживание сразу после открытия доводит до конца connect к
     * AF_UNIX, который завершается без ожидания, - иначе самая первая запись
     * ушла бы в никуда. */
    if (msg_logger_open(&logger, loop) == CORE_OK) {
        (void)msg_logger_event(&logger);
        broker.log = msg_logger_record;
        broker.log_context = &logger;
    }
    error = ipc_server_create(&endpoint, &broker.server);
    if (error != CORE_OK) {
        goto cleanup;
    }
    error = ipc_server_get_fd(broker.server, &fd);
    if (error == CORE_OK) {
        error = eventloop_add_fd(loop, fd, EVENT_READABLE, &broker.listener);
    }
    if (error != CORE_OK) {
        goto cleanup;
    }
    msg_logger_record(&logger, "ready", NULL, 0, CORE_OK);
    result = PROCESS_EXIT_SUCCESS;

    while (!stopping) {
        Event events[MSG_EVENT_BATCH];
        usize count = 0;
        bool buffered = serve_buffered(&broker);

        if (stopping) {
            break;
        }
        error = eventloop_wait(loop, events, MSG_EVENT_BATCH, buffered ? 0 : MSG_TICK_MS,
                               &count);
        if (error == CORE_INTERRUPTED) {
            continue;
        }
        if (error != CORE_OK) {
            result = PROCESS_EXIT_FAILURE;
            break;
        }
        for (usize i = 0; i < count && !stopping; ++i) {
            MsgClient **link;

            if (events[i].source_id == broker.listener) {
                if (msg_broker_accept(&broker) != CORE_OK) {
                    result = PROCESS_EXIT_FAILURE;
                    stopping = 1;
                }
                continue;
            }
            if (logger.connection != NULL && events[i].source_id == logger.source) {
                if (msg_logger_event(&logger) != CORE_OK) {
                    /* Журнал отвалился: брокер снимает его и работает дальше. */
                    msg_logger_close(&logger);
                    broker.log = NULL;
                }
                continue;
            }
            link = msg_broker_find_source(&broker, events[i].source_id);
            if (link == NULL) {
                continue; /* источник уже снят в этом же пакете событий */
            }
            if (msg_broker_serve(&broker, *link, now_ms()) != CORE_OK) {
                msg_broker_close_client(&broker, link);
            }
        }
        msg_broker_tick(&broker, now_ms());
        /* Кадры, положенные в чужие очереди этим пакетом событий и
         * истёкшими дедлайнами, уходят здесь. */
        msg_broker_flush_all(&broker);
    }

    /* Последняя попытка отдать поставленные кадры: доставка at-most-once, но
     * то, что уже в очереди, стоит отправить. */
    for (MsgClient *client = broker.clients; client != NULL; client = client->next) {
        (void)msg_broker_drain(&broker, client);
    }
    msg_logger_record(&logger, "stopping", NULL, 0, CORE_OK);
cleanup:
    msg_broker_shutdown(&broker);
    msg_logger_close(&logger);
    if (loop != NULL) {
        (void)eventloop_close(&loop);
    }
    return result;
}
