#include <logserv.h>
#include <special/ipc.h>
#include <punos/syscall/syscall.h>
#include "wire.h"

typedef struct Client {
    MemoryBlock block;
    struct Client *next;
    IpcConnectionHandle connection;
    EventSourceId source;
    bool buffered;
} Client;
static volatile i32 stopping;
static void stop_signal(i32 number) { (void)number; stopping = 1; }

static void close_client(EventLoopHandle loop, Client **link)
{
    Client *client = *link;
    bool pending;
    (void)ipc_connection_flush(client->connection, &pending);
    (void)eventloop_remove_source(loop, client->source);
    (void)ipc_connection_close(&client->connection);
    *link = client->next;
    MemoryBlock block = client->block;
    (void)memory_free(&block);
}

static ErrorCode accept_clients(EventLoopHandle loop, IpcServerHandle server, Client **list)
{
    for (usize i = 0; i < 64 && !stopping; ++i) {
        IpcConnectionHandle connection;
        ErrorCode error = ipc_server_accept(server, &connection);
        if (error == CORE_WOULD_BLOCK) return CORE_OK;
        if (error != CORE_OK) return error;
        MemoryBlock block;
        error = memory_allocate(sizeof(Client), &block);
        if (error != CORE_OK) { (void)ipc_connection_close(&connection); return error; }
        Client *client = (Client *)block.data;
        *client = (Client){ .block = block, .connection = connection };
        i32 fd;
        error = ipc_connection_get_fd(connection, &fd);
        if (error == CORE_OK) error = eventloop_add_fd(loop, fd, EVENT_READABLE, &client->source);
        if (error != CORE_OK) {
            (void)ipc_connection_close(&connection); (void)memory_free(&block); return error;
        }
        client->next = *list; *list = client;
    }
    return CORE_OK;
}

static ErrorCode serve(EventLoopHandle loop, Client *client, LogQueue *q)
{
    client->buffered = true;
    for (usize i = 0; i < 64 && !stopping; ++i) {
        IpcMessage request = { 0 };
        IpcReceiveStatus status;
        ErrorCode error = ipc_connection_receive(client->connection, &status, &request);
        if (error != CORE_OK) return error;
        if (status == IPC_RECEIVE_STATUS_PEER_CLOSED) return CORE_NOT_FOUND;
        if (status == IPC_RECEIVE_STATUS_NO_MESSAGE) { client->buffered = false; break; }
        u32 type = request.type == 1 ? 2 : request.type;
        error = request.type == 1 ? log_queue_submit(q, request.payload) : CORE_INVALID_ARGUMENT;
        (void)ipc_message_destroy(&request);
        u8 bytes[4]; wire_write(bytes, 4, error);
        IpcMessage reply = { type, { bytes, 4 } };
        if (error == CORE_INVALID_ARGUMENT || error == CORE_UNSUPPORTED || error == CORE_OVERFLOW)
            (void)log_queue_self(q, "invalid IPC request");
        error = ipc_connection_send(client->connection, &reply);
        if (error != CORE_OK) return error;
        if (log_queue_failed(q)) break;
    }
    bool pending;
    ErrorCode error = ipc_connection_flush(client->connection, &pending);
    if (error != CORE_OK) return error;
    return eventloop_update_fd(loop, client->source, EVENT_READABLE | (pending ? EVENT_WRITABLE : 0));
}

int main(int argc, char **argv, char **envp)
{
    (void)envp;
    const char prefix[] = "--log-path=";
    if (argc != 2) return PROCESS_EXIT_INVALID_ARGUMENT;
    for (usize i = 0; i < sizeof(prefix)-1; ++i)
        if (argv[1][i] != prefix[i]) return PROCESS_EXIT_INVALID_ARGUMENT;
    String path = { 0 };
    if (argv[1][sizeof(prefix)-1] == '\0') return PROCESS_EXIT_INVALID_ARGUMENT;
    ErrorCode path_error=string_from_c_str(argv[1] + sizeof(prefix)-1, &path);
    if (path_error!=CORE_OK)
        return path_error==CORE_INVALID_ARGUMENT || path_error==CORE_OVERFLOW
            ? PROCESS_EXIT_INVALID_ARGUMENT : PROCESS_EXIT_INITIALIZATION_FAILED;
    LogQueue q = { 0 };
    EventLoopHandle loop = NULL;
    IpcServerHandle server = NULL;
    EventSourceId listener = 0;
    Client *clients = NULL;
    int result = PROCESS_EXIT_INITIALIZATION_FAILED;
    struct sigaction action = { .sa_handler = stop_signal };
    if (sigaction(SIGTERM, &action, NULL) != 0 || sigaction(SIGINT, &action, NULL) != 0) goto cleanup;
    /* The worker inherits a blocked stop mask. The owner handles stop signals. */
    sigset_t stop_mask = (1ULL << (SIGTERM-1)) | (1ULL << (SIGINT-1));
    sigset_t old_mask;
    if (sigprocmask(SIG_BLOCK, &stop_mask, &old_mask) != 0) goto cleanup;
    ErrorCode error = log_queue_start(&q, &path);
    (void)sigprocmask(SIG_SETMASK, &old_mask, NULL);
    if (error != CORE_OK || stopping) goto cleanup;
    error = eventloop_create(&loop);
    if (error != CORE_OK) goto cleanup;
    (void)mutex_lock(q.mutex, SYNC_WAIT_FOREVER);
    q.loop = loop;
    (void)mutex_unlock(q.mutex);
    if (log_queue_self(&q, "ready") != CORE_OK || log_queue_failed(&q)) goto cleanup;
    String endpoint = { (char *)LOG_ENDPOINT, sizeof(LOG_ENDPOINT)-1 };
    error = ipc_server_create(&endpoint, &server);
    if (error != CORE_OK) goto cleanup;
    i32 fd;
    error = ipc_server_get_fd(server, &fd);
    if (error == CORE_OK) error = eventloop_add_fd(loop, fd, EVENT_READABLE, &listener);
    if (error != CORE_OK) goto cleanup;
    result = PROCESS_EXIT_SUCCESS;
    while (!stopping && !log_queue_failed(&q)) {
        bool buffered = false;
        Client **link = &clients;
        while (*link != NULL && !stopping) {
            Client *client = *link;
            if (client->buffered && serve(loop, client, &q) != CORE_OK) {
                close_client(loop, link); continue;
            }
            buffered = buffered || client->buffered;
            link = &client->next;
        }
        Event events[32]; usize count;
        error = eventloop_wait(loop, events, 32, buffered ? 0 : 1000, &count);
        if (error == CORE_INTERRUPTED) continue;
        if (error != CORE_OK) { result = PROCESS_EXIT_FAILURE; break; }
        for (usize i = 0; i < count && !stopping && !log_queue_failed(&q); ++i) {
            if (events[i].source_id == listener) {
                if (accept_clients(loop, server, &clients) != CORE_OK) {
                    result = PROCESS_EXIT_FAILURE; stopping = 1;
                }
            } else {
                link = &clients;
                while (*link != NULL && (*link)->source != events[i].source_id) link = &(*link)->next;
                if (*link != NULL && serve(loop, *link, &q) != CORE_OK) {
                    (void)log_queue_self(&q, "IPC connection failed");
                    close_client(loop, link);
                }
            }
        }
    }
cleanup:
    if (listener != 0) (void)eventloop_remove_source(loop, listener);
    if (server != NULL) (void)ipc_server_close(&server);
    while (clients != NULL) close_client(loop, &clients);
    if (q.ready && !log_queue_failed(&q)) (void)log_queue_self(&q, "stopping");
    if (log_queue_stop(&q) != CORE_OK && result == PROCESS_EXIT_SUCCESS) result = PROCESS_EXIT_FAILURE;
    /* A failed join keeps the handle: leave borrowed path/loop alive until
     * process-wide exit rather than freeing resources a worker may still use. */
    if (q.worker != NULL) exit(PROCESS_EXIT_FAILURE);
    if (loop != NULL) (void)eventloop_close(&loop);
    (void)string_destroy(&path);
    return result;
}
