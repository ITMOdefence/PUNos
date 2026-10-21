#include <appserv/appserv.h>
#include <special/ipc.h>
#include <sys/eventloop.h>
#include <punos/syscall/syscall.h>
#include "logger.h"
#include "wire.h"

extern const AppCatalog app_generated_catalog;

typedef struct Client {
    MemoryBlock allocation;
    struct Client *next;
    IpcConnectionHandle connection;
    EventSourceId source;
    bool buffered;
} Client;

static volatile i32 stop_requested;

static void stop_signal(i32 signal)
{
    (void)signal;
    stop_requested = 1;
}

static void close_client(EventLoopHandle loop, Client **link)
{
    Client *client = *link;
    MemoryBlock allocation = client->allocation;
    (void)eventloop_remove_source(loop, client->source);
    (void)ipc_connection_close(&client->connection);
    *link = client->next;
    (void)memory_free(&allocation);
}

static ErrorCode accept_clients(EventLoopHandle loop, IpcServerHandle server,
                                 Client **clients)
{
    for (usize i = 0; i < 64 && !stop_requested; ++i) {
        IpcConnectionHandle connection;
        MemoryBlock allocation;
        i32 fd;
        ErrorCode error = ipc_server_accept(server, &connection);
        if (error == CORE_WOULD_BLOCK) return CORE_OK;
        if (error != CORE_OK) return error;
        error = memory_allocate(sizeof(Client), &allocation);
        if (error != CORE_OK) {
            (void)ipc_connection_close(&connection);
            return error;
        }
        Client *client = (Client *)allocation.data;
        *client = (Client){ .allocation = allocation, .connection = connection };
        error = ipc_connection_get_fd(connection, &fd);
        if (error == CORE_OK)
            error = eventloop_add_fd(loop, fd, EVENT_READABLE, &client->source);
        if (error != CORE_OK) {
            (void)ipc_connection_close(&connection);
            (void)memory_free(&allocation);
            return error;
        }
        client->next = *clients;
        *clients = client;
    }
    return CORE_OK;
}

static ErrorCode serve_client(EventLoopHandle loop, Client *client, AppService *service)
{
    bool pending;
    ErrorCode error;
    client->buffered = true;
    for (usize i = 0; i < 64 && !stop_requested; ++i) {
        IpcReceiveStatus status;
        IpcMessage request = { 0 };
        AppReply reply = { 0 };
        error = ipc_connection_receive(client->connection, &status, &request);
        if (error != CORE_OK) return error;
        if (status == IPC_RECEIVE_STATUS_PEER_CLOSED) return CORE_NOT_FOUND;
        if (status == IPC_RECEIVE_STATUS_NO_MESSAGE) { client->buffered = false; break; }
        error = app_service_request(service, request.type, request.payload, &reply);
        (void)ipc_message_destroy(&request);
        if (error == CORE_OK) {
            IpcMessage response = { reply.type, { reply.payload.data, reply.size } };
            error = ipc_connection_send(client->connection, &response);
            (void)memory_free(&reply.payload);
        } else if (error == CORE_OUT_OF_MEMORY) {
            /* Reserve-free error encoding; ipclib may still fail to queue it. */
            u8 bytes[4];
            wire_write(bytes, 4, CORE_OUT_OF_MEMORY);
            IpcMessage response = { reply.type, { bytes, sizeof(bytes) } };
            error = ipc_connection_send(client->connection, &response);
        }
        if (error != CORE_OK) return error;
    }
    error = ipc_connection_flush(client->connection, &pending);
    if (error != CORE_OK) return error;
    return eventloop_update_fd(loop, client->source,
                               EVENT_READABLE | (pending ? EVENT_WRITABLE : 0));
}

int main(int argc, char **argv, char **envp)
{
    AppService service = { 0 };
    AppLogger logger = { 0 };
    EventLoopHandle loop = NULL;
    IpcServerHandle server = NULL;
    EventSourceId listener = 0, monitor = 0;
    Client *clients = NULL;
    ErrorCode error;
    int result = PROCESS_EXIT_INITIALIZATION_FAILED;
    String endpoint = { (char *)APP_ENDPOINT, sizeof(APP_ENDPOINT) - 1 };
    struct sigaction action = { .sa_handler = stop_signal };
    (void)argv;
    (void)envp;
    if (argc != 1) return PROCESS_EXIT_INVALID_ARGUMENT;
    /* No IPC endpoint is exposed until every startup check has succeeded. */
    error = app_service_init(&service, &app_generated_catalog, app_logger_record, &logger);
    if (error != CORE_OK) return result;
    if (sigaction(SIGTERM, &action, NULL) != 0 || sigaction(SIGINT, &action, NULL) != 0)
        goto cleanup;
    error = eventloop_create(&loop);
    if (error != CORE_OK) goto cleanup;
    error = eventloop_create_timer(loop, APP_MONITOR_PERIOD_MS, APP_MONITOR_PERIOD_MS, &monitor);
    if (error != CORE_OK) goto cleanup;
    error = app_logger_open(&logger, loop);
    if (error != CORE_OK) goto cleanup;
    /* initserv starts appserv only after logserv is ready. This finite wait
     * merely completes our nonblocking connect; it is not app readiness. */
    for (usize attempts = 0; !logger.connected && attempts < 50 && !stop_requested; ++attempts) {
        Event events[16];
        usize count;
        error = eventloop_wait(loop, events, 16, 100, &count);
        if (error == CORE_INTERRUPTED) continue;
        if (error != CORE_OK) goto cleanup;
        for (usize i = 0; i < count; ++i) {
            if (events[i].source_id == logger.source && app_logger_event(&logger) != CORE_OK)
                goto cleanup;
        }
    }
    if (!logger.connected || stop_requested) goto cleanup;
    app_logger_record(&logger, "ready", NULL, 0, NULL, CORE_OK);
    if (logger.error != CORE_OK) goto cleanup;
    error = ipc_server_create(&endpoint, &server);
    if (error != CORE_OK) goto cleanup;
    i32 fd;
    error = ipc_server_get_fd(server, &fd);
    if (error == CORE_OK) error = eventloop_add_fd(loop, fd, EVENT_READABLE, &listener);
    if (error != CORE_OK) goto cleanup;
    result = PROCESS_EXIT_SUCCESS;
    while (!stop_requested) {
        Event events[32];
        usize count;
        bool buffered = false;
        Client **link = &clients;
        while (*link != NULL && !stop_requested) {
            Client *client = *link;
            if (client->buffered) {
                error = serve_client(loop, client, &service);
                if (error != CORE_OK) { close_client(loop, link); continue; }
            }
            buffered = buffered || client->buffered;
            link = &client->next;
        }
        if (logger.error != CORE_OK) { result = PROCESS_EXIT_FAILURE; break; }
        /* Finite wait prevents a signal just before epoll_wait from stranding
         * shutdown. The signal handler never calls resource-owning APIs. */
        error = eventloop_wait(loop, events, 32, buffered ? 0 : APP_MONITOR_PERIOD_MS, &count);
        if (error == CORE_INTERRUPTED) continue;
        if (error != CORE_OK) { result = PROCESS_EXIT_FAILURE; break; }
        for (usize i = 0; i < count && !stop_requested; ++i) {
            EventSourceId source = events[i].source_id;
            if (source == listener) error = accept_clients(loop, server, &clients);
            else if (source == monitor) error = app_service_poll(&service);
            else if (source == logger.source) error = app_logger_event(&logger);
            else {
                Client **link = &clients;
                while (*link != NULL && (*link)->source != source) link = &(*link)->next;
                if (*link != NULL) {
                    error = serve_client(loop, *link, &service);
                    if (error != CORE_OK) {
                        if (error != CORE_NOT_FOUND)
                            app_logger_record(&logger, "ipc_failed", NULL, 0, NULL, error);
                        close_client(loop, link);
                    }
                }
                error = CORE_OK; /* A broken client does not stop the service. */
            }
            if (error != CORE_OK || logger.error != CORE_OK) {
                result = PROCESS_EXIT_FAILURE;
                stop_requested = 1;
            }
        }
    }
cleanup:
    service.ready = false;
    if (listener != 0) (void)eventloop_remove_source(loop, listener);
    /* Closing the listening socket also removes the endpoint before waits. */
    if (server != NULL) (void)ipc_server_close(&server);
    while (clients != NULL) close_client(loop, &clients);
    if (app_service_shutdown(&service) != CORE_OK) result = PROCESS_EXIT_FAILURE;
    if (logger.connected && logger.error == CORE_OK) {
        app_logger_record(&logger, "stopped", NULL, 0, NULL, CORE_OK);
        /* Best-effort bounded transport drain, never a child stop timeout. */
        for (usize i = 0; i < 10 && logger.outstanding != 0 && logger.error == CORE_OK; ++i) {
            Event events[16];
            usize count;
            ErrorCode drain = eventloop_wait(loop, events, 16, 100, &count);
            if (drain == CORE_INTERRUPTED) continue;
            if (drain != CORE_OK) break;
            for (usize j = 0; j < count; ++j)
                if (events[j].source_id == logger.source)
                    logger.error = app_logger_event(&logger);
        }
    }
    app_logger_close(&logger);
    if (logger.error != CORE_OK && result == PROCESS_EXIT_SUCCESS)
        result = PROCESS_EXIT_FAILURE;
    if (loop != NULL) (void)eventloop_close(&loop);
    return result;
}
