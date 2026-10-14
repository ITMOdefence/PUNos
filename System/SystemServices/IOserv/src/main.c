#include <ioserv/protocol.h>
#include <special/input.h>
#include <special/ipc.h>
#include <sys/eventloop.h>
#include <sys/memory.h>
#include <sys/process.h>
#include <sys/string.h>
#include <punos/syscall/syscall.h>
#include "../../common/logger.h"
#include "../../common/wire.h"
#include "state.h"

#ifndef IO_ENDPOINT
#define IO_ENDPOINT "/run/punos/ioserv.sock"
#endif

typedef struct Client {
    MemoryBlock block;
    struct Client *next;
    IpcConnectionHandle connection;
    EventSourceId source;
    InputDeviceClass mask;
    bool buffered;
} Client;

typedef struct Device {
    MemoryBlock block;
    struct Device *next;
    InputDeviceHandle handle;
    InputDeviceInfo info;
    EventSourceId source;
    u64 id;
    u64 sequence;
    IoDeviceState state;
} Device;

typedef struct IoService {
    EventLoopHandle loop;
    IpcServerHandle server;
    EventSourceId listener;
    Client *clients;
    Device *devices;
    u64 next_id;
    ServiceLogger logger;
} IoService;

static volatile i32 stopping;
static void stop_signal(i32 number) { (void)number; stopping = 1; }

static bool begins(const char *text, const char *prefix)
{
    while (*prefix) if (*text++ != *prefix++) return false;
    return true;
}

static void close_client(IoService *service, Client **link)
{
    Client *client = *link;
    bool pending;
    (void)ipc_connection_flush(client->connection, &pending);
    (void)eventloop_remove_source(service->loop, client->source);
    (void)ipc_connection_close(&client->connection);
    *link = client->next;
    MemoryBlock block = client->block;
    (void)memory_free(&block);
}

static void close_device(IoService *service, Device **link)
{
    Device *device = *link;
    if (device->source) (void)eventloop_remove_source(service->loop, device->source);
    (void)input_device_close(&device->handle);
    *link = device->next;
    MemoryBlock block = device->block;
    (void)memory_free(&block);
}

static ErrorCode queue(Client *client, u32 type, u8 *bytes, usize size)
{
    IpcMessage message = { type, { bytes, size } };
    return ipc_connection_send(client->connection, &message);
}

static ErrorCode flush_client(IoService *service, Client *client)
{
    bool pending;
    ErrorCode error = ipc_connection_flush(client->connection, &pending);
    if (error != CORE_OK) return error;
    return eventloop_update_fd(service->loop, client->source,
                               EVENT_READABLE | (pending ? EVENT_WRITABLE : 0));
}

static void broadcast(IoService *service, Device *device, u32 type,
                      u8 *bytes, usize size)
{
    Client **link = &service->clients;
    while (*link) {
        Client *client = *link;
        ErrorCode error = CORE_OK;
        if (client->mask & device->info.classes) {
            error = queue(client, type, bytes, size);
            if (error == CORE_OK) error = flush_client(service, client);
        }
        if (error != CORE_OK) {
            (void)service_logger_record(&service->logger, "slow or failed client", error);
            close_client(service, link);
        } else link = &client->next;
    }
}

static ErrorCode accept_clients(IoService *service)
{
    for (usize i = 0; i < 64 && !stopping; ++i) {
        IpcConnectionHandle connection;
        ErrorCode error = ipc_server_accept(service->server, &connection);
        if (error == CORE_WOULD_BLOCK) return CORE_OK;
        if (error != CORE_OK) return error;
        MemoryBlock block;
        error = memory_allocate(sizeof(Client), &block);
        if (error != CORE_OK) { (void)ipc_connection_close(&connection); return error; }
        Client *client = (Client *)block.data;
        *client = (Client){ .block = block, .connection = connection };
        i32 fd;
        error = ipc_connection_get_fd(connection, &fd);
        if (error == CORE_OK)
            error = eventloop_add_fd(service->loop, fd, EVENT_READABLE, &client->source);
        if (error != CORE_OK) {
            (void)ipc_connection_close(&connection); (void)memory_free(&block);
            return error;
        }
        client->next = service->clients;
        service->clients = client;
    }
    return CORE_OK;
}

static ErrorCode request(IoService *service, Client *client, const IpcMessage *message)
{
    u32 result_type;
    InputDeviceClass mask = 0;
    ErrorCode error = io_request_error(message, &result_type, &mask);
    if (error == CORE_OK && message->type == IO_SUBSCRIBE && client->mask)
        error = CORE_INVALID_STATE;
    if (error == CORE_OK && message->type == IO_UNSUBSCRIBE && !client->mask)
        error = CORE_INVALID_STATE;
    if (error != CORE_OK)
        (void)service_logger_record(&service->logger, "rejected IPC request", error);
    u8 bytes[IO_DEVICE_EVENT_SIZE];
    wire_write(bytes, 4, error);
    ErrorCode sent = queue(client, result_type, bytes, 4);
    if (sent != CORE_OK) return sent;
    if (error == CORE_OK && message->type == IO_SUBSCRIBE) {
        client->mask = mask;
        for (Device *device = service->devices; device; device = device->next) {
            if (!(mask & device->info.classes)) continue;
            io_encode_device(bytes, device->id, &device->info);
            sent = queue(client, IO_DEVICE_ADDED, bytes, IO_DEVICE_EVENT_SIZE);
            if (sent != CORE_OK) return sent;
        }
    } else if (error == CORE_OK && message->type == IO_UNSUBSCRIBE)
        client->mask = 0;
    return CORE_OK;
}

static ErrorCode serve_client(IoService *service, Client *client)
{
    client->buffered = true;
    for (usize i = 0; i < 64 && !stopping; ++i) {
        IpcReceiveStatus status;
        IpcMessage message = { 0 };
        ErrorCode error = ipc_connection_receive(client->connection, &status, &message);
        if (error != CORE_OK) {
            (void)service_logger_record(&service->logger, "IPC receive failed", error);
            return error;
        }
        if (status == IPC_RECEIVE_STATUS_PEER_CLOSED) return CORE_NOT_FOUND;
        if (status == IPC_RECEIVE_STATUS_NO_MESSAGE) { client->buffered = false; break; }
        error = request(service, client, &message);
        (void)ipc_message_destroy(&message);
        if (error != CORE_OK) return error;
    }
    return flush_client(service, client);
}

static ErrorCode open_device(IoService *service, const char *path_text)
{
    String path = { 0 };
    ErrorCode error = string_from_c_str(path_text, &path);
    if (error != CORE_OK) return error;
    InputDeviceConfig config = { &path };
    InputDeviceHandle handle = NULL;
    error = input_device_open(&config, &handle);
    (void)string_destroy(&path);
    if (error != CORE_OK) return error;
    MemoryBlock block;
    error = memory_allocate(sizeof(Device), &block);
    if (error != CORE_OK) { (void)input_device_close(&handle); return error; }
    Device *device = (Device *)block.data;
    *device = (Device){ .block = block, .handle = handle,
                        .id = ++service->next_id, .sequence = 0 };
    error = input_device_get_info(handle, &device->info);
    i32 fd;
    if (error == CORE_OK) error = input_device_get_fd(handle, &fd);
    if (error == CORE_OK)
        error = eventloop_add_fd(service->loop, fd, EVENT_READABLE, &device->source);
    if (error != CORE_OK) {
        if (device->source) (void)eventloop_remove_source(service->loop, device->source);
        (void)input_device_close(&handle);
        (void)memory_free(&block);
        return error;
    }
    device->next = service->devices;
    service->devices = device;
    u8 bytes[IO_DEVICE_EVENT_SIZE];
    io_encode_device(bytes, device->id, &device->info);
    broadcast(service, device, IO_DEVICE_ADDED, bytes, sizeof(bytes));
    return CORE_OK;
}

static bool read_device(IoService *service, Device *device)
{
    while (!stopping) {
        InputReadStatus status;
        InputFrame frame = { 0 };
        ErrorCode error = input_device_read_frame(device->handle, &status, &frame);
        if (error != CORE_OK) {
            (void)service_logger_record(&service->logger, "input read failed", error);
            return true;
        }
        if (status == INPUT_READ_STATUS_NO_FRAME) return false;
        if (status == INPUT_READ_STATUS_DEVICE_LOST) {
            u8 bytes[IO_DEVICE_EVENT_SIZE];
            io_encode_device(bytes, device->id, &device->info);
            broadcast(service, device, IO_DEVICE_REMOVED, bytes, sizeof(bytes));
            (void)service_logger_record(&service->logger, "input device removed", CORE_OK);
            return true;
        }
        bool sync_lost = frame.count == 1 && frame.events[0].kind == INPUT_EVENT_KIND_SYNC_LOST;
        io_state_apply(&device->state, &frame);
        if (sync_lost) (void)service_logger_record(&service->logger, "input synchronization lost", CORE_OK);
        u8 bytes[IO_FRAME_HEADER_SIZE + IO_EVENT_SIZE * INPUT_FRAME_MAX_EVENTS];
        usize size;
        error = io_encode_frame(bytes, sizeof(bytes), device->id, ++device->sequence,
                                &frame, &size);
        if (error == CORE_OK) broadcast(service, device, IO_INPUT_FRAME, bytes, size);
        else (void)service_logger_record(&service->logger, "input frame invalid", error);
        (void)input_frame_destroy(&frame);
    }
    return false;
}

int main(int argc, char **argv, char **envp)
{
    (void)envp;
    if (argc < 2 || argc > 65) return PROCESS_EXIT_INVALID_ARGUMENT;
    for (int i = 1; i < argc; ++i)
        if (!begins(argv[i], "--device=") || !argv[i][9]) return PROCESS_EXIT_INVALID_ARGUMENT;
    IoService service = { 0 };
    int result = PROCESS_EXIT_INITIALIZATION_FAILED;
    ErrorCode error;
    String endpoint = { (char *)IO_ENDPOINT, sizeof(IO_ENDPOINT)-1 };
    struct sigaction action = { .sa_handler = stop_signal };
    if (sigaction(SIGTERM, &action, NULL) != 0 || sigaction(SIGINT, &action, NULL) != 0)
        goto cleanup;
    error = eventloop_create(&service.loop);
    if (error != CORE_OK) goto cleanup;
    error = service_logger_open(&service.logger, service.loop, "IOserv");
    if (error != CORE_OK) goto cleanup;
    for (usize tries = 0; !service.logger.connected && tries < 50 && !stopping; ++tries) {
        Event events[4]; usize count;
        error = eventloop_wait(service.loop, events, 4, 100, &count);
        if (error != CORE_OK) goto cleanup;
        for (usize i = 0; i < count; ++i)
            if (events[i].source_id == service.logger.source &&
                service_logger_event(&service.logger) != CORE_OK) goto cleanup;
    }
    if (!service.logger.connected || stopping) goto cleanup;
    error = ipc_server_create(&endpoint, &service.server);
    if (error == CORE_OK) {
        i32 fd;
        error = ipc_server_get_fd(service.server, &fd);
        if (error == CORE_OK)
            error = eventloop_add_fd(service.loop, fd, EVENT_READABLE, &service.listener);
    }
    if (error != CORE_OK) {
        (void)service_logger_record(&service.logger, "input endpoint failed", error);
        goto cleanup;
    }
    for (int i = 1; i < argc; ++i) {
        error = open_device(&service, argv[i] + 9);
        if (error == CORE_OK)
            (void)service_logger_record(&service.logger, "input device opened", CORE_OK);
        else (void)service_logger_record(&service.logger, "input device open failed", error);
    }
    (void)service_logger_record(&service.logger, "started", CORE_OK);
    result = PROCESS_EXIT_SUCCESS;
    while (!stopping) {
        Event events[32]; usize count;
        bool buffered = false;
        for (Client *c = service.clients; c; c = c->next) if (c->buffered) buffered = true;
        error = eventloop_wait(service.loop, events, 32, buffered ? 0 : 100, &count);
        if (error == CORE_INTERRUPTED) continue;
        if (error != CORE_OK) { result = PROCESS_EXIT_FAILURE; break; }
        for (usize i = 0; i < count && !stopping; ++i) {
            EventSourceId source = events[i].source_id;
            if (source == service.listener) {
                if (accept_clients(&service) != CORE_OK) { result = PROCESS_EXIT_FAILURE; stopping = 1; }
            } else if (source == service.logger.source) (void)service_logger_event(&service.logger);
            else {
                Device **device = &service.devices;
                while (*device && (*device)->source != source) device = &(*device)->next;
                if (*device) {
                    if (read_device(&service, *device)) close_device(&service, device);
                } else {
                    Client **client = &service.clients;
                    while (*client && (*client)->source != source) client = &(*client)->next;
                    if (*client && serve_client(&service, *client) != CORE_OK)
                        close_client(&service, client);
                }
            }
        }
        Client **link = &service.clients;
        while (*link) {
            Client *client = *link;
            if (client->buffered && serve_client(&service, client) != CORE_OK)
                close_client(&service, link);
            else link = &client->next;
        }
    }
cleanup:
    if (service.listener) (void)eventloop_remove_source(service.loop, service.listener);
    while (service.clients) close_client(&service, &service.clients);
    while (service.devices) close_device(&service, &service.devices);
    if (service.server) (void)ipc_server_close(&service.server);
    if (service.logger.connected) {
        (void)service_logger_record(&service.logger, "stopped", CORE_OK);
        service_logger_drain(&service.logger);
    }
    if (service.logger.connection) service_logger_close(&service.logger);
    if (service.loop) (void)eventloop_close(&service.loop);
    return result;
}
