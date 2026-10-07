#include <compositorserv.h>

#include <gui/protocol.h>
#include <special/ipc.h>
#include <sys/eventloop.h>
#include <sys/memory.h>

#define COMPOSITOR_SERVICE_CLIENT_MAX ((usize)32)
#define COMPOSITOR_SERVICE_EVENT_MAX ((usize)64)

typedef struct CompositorClient {
    IpcConnectionHandle connection;
    EventSourceId source;
} CompositorClient;

struct CompositorServiceHandle {
    MemoryBlock handle_block;
    EventLoopHandle loop;
    IpcServerHandle server;
    EventSourceId server_source;
    CompositorClient clients[COMPOSITOR_SERVICE_CLIENT_MAX];
    u8 controller_token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE];
};

static void remove_client(CompositorServiceHandle service,
                          CompositorClient *client);

static CompositorClient *find_client(CompositorServiceHandle service,
                                     EventSourceId source)
{
    usize index;

    for (index = 0; index < COMPOSITOR_SERVICE_CLIENT_MAX; ++index) {
        if (service->clients[index].connection != NULL &&
            service->clients[index].source == source) {
            return &service->clients[index];
        }
    }
    return NULL;
}

/*
 * Message-specific handling is added in the next implementation stages.
 * Keeping the switch here establishes the compositorserv IPC dispatch
 * boundary without silently accepting unsupported requests.
 */
static ErrorCode dispatch(CompositorServiceHandle service,
                          CompositorClient *client,
                          IpcMessage *message)
{
    (void)service;
    (void)client;

    switch (message->type) {
    case GUI_SURFACE_CREATE:
    case GUI_SURFACE_DESTROY:
    case GUI_SURFACE_BUFFER_IMPORT:
    case GUI_SURFACE_BUFFER_DESTROY:
    case GUI_SURFACE_ATTACH:
    case GUI_SURFACE_DAMAGE:
    case GUI_SURFACE_SET_OPAQUE:
    case GUI_SURFACE_REQUEST_FRAME:
    case GUI_SURFACE_COMMIT:
    case GUI_COMPOSITOR_BIND_CONTROLLER:
    case GUI_SCENE_BIND_WINDOW:
    case GUI_SCENE_SET_GEOMETRY:
    case GUI_SCENE_SET_VISIBILITY:
    case GUI_SCENE_SET_STACK:
    case GUI_SCENE_SET_OUTPUT:
    case GUI_SCENE_REMOVE_WINDOW:
        return CORE_UNSUPPORTED;
    default:
        return CORE_UNSUPPORTED;
    }
}

static void remove_client(CompositorServiceHandle service,
                          CompositorClient *client)
{
    (void)eventloop_remove_source(service->loop, client->source);
    (void)ipc_connection_close(&client->connection);
    client->source = 0;
}

static ErrorCode accept_clients(CompositorServiceHandle service)
{
    while (true) {
        IpcConnectionHandle connection;
        usize index;
        ErrorCode result = ipc_server_accept(service->server, &connection);

        if (result == CORE_WOULD_BLOCK) return CORE_OK;
        if (result != CORE_OK) return result;

        for (index = 0; index < COMPOSITOR_SERVICE_CLIENT_MAX; ++index) {
            if (service->clients[index].connection == NULL) break;
        }
        if (index == COMPOSITOR_SERVICE_CLIENT_MAX) {
            (void)ipc_connection_close(&connection);
            continue;
        }

        {
            i32 fd;
            EventSourceId source;

            result = ipc_connection_get_fd(connection, &fd);
            if (result == CORE_OK) {
                result = eventloop_add_fd(
                    service->loop, fd,
                    EVENT_READABLE | EVENT_ERROR | EVENT_HANGUP,
                    &source);
            }
            if (result != CORE_OK) {
                (void)ipc_connection_close(&connection);
                return result;
            }

            service->clients[index].connection = connection;
            service->clients[index].source = source;
        }
    }
}

static ErrorCode service_client_event(CompositorServiceHandle service,
                                      CompositorClient *client,
                                      EventMask mask)
{
    if ((mask & (EVENT_ERROR | EVENT_HANGUP)) != 0) {
        remove_client(service, client);
        return CORE_OK;
    }

    if ((mask & EVENT_WRITABLE) != 0) {
        bool pending;
        ErrorCode result = ipc_connection_flush(client->connection, &pending);

        if (result != CORE_OK) {
            remove_client(service, client);
            return CORE_OK;
        }
        (void)eventloop_update_fd(
            service->loop, client->source,
            pending ? EVENT_READABLE | EVENT_WRITABLE | EVENT_ERROR | EVENT_HANGUP
                    : EVENT_READABLE | EVENT_ERROR | EVENT_HANGUP);
    }

    if ((mask & EVENT_READABLE) != 0) {
        while (client->connection != NULL) {
            IpcReceiveStatus status;
            IpcMessage message;
            ErrorCode result = ipc_connection_receive(client->connection,
                                                       &status, &message);

            if (result != CORE_OK) {
                remove_client(service, client);
                return CORE_OK;
            }
            if (status == IPC_RECEIVE_NO_MESSAGE) break;
            if (status == IPC_RECEIVE_PEER_CLOSED) {
                remove_client(service, client);
                break;
            }

            result = dispatch(service, client, &message);
            (void)ipc_message_destroy(&message);
            if (result != CORE_OK) {
                remove_client(service, client);
                return CORE_OK;
            }
        }
    }

    return CORE_OK;
}

ErrorCode compositor_service_create(
    const String *endpoint,
    const u8 controller_token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE],
    CompositorServiceHandle *out_service)
{
    CompositorServiceConfig config;

    if (endpoint == NULL || controller_token == NULL || out_service == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    config.endpoint = endpoint;
    config.controller_token = controller_token;
    return compositor_service_create_config(&config, out_service);
}

ErrorCode compositor_service_create_config(
    const CompositorServiceConfig *config,
    CompositorServiceHandle *out_service)
{
    MemoryBlock block;
    CompositorServiceHandle service;
    i32 fd;
    usize index;
    ErrorCode result;

    if (config == NULL || config->endpoint == NULL ||
        config->controller_token == NULL || out_service == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    result = memory_allocate(sizeof(*service), &block);
    if (result != CORE_OK) return result;

    service = (CompositorServiceHandle)block.data;
    service->handle_block = block;
    service->loop = NULL;
    service->server = NULL;
    service->server_source = 0;

    for (index = 0; index < COMPOSITOR_SERVICE_CLIENT_MAX; ++index) {
        service->clients[index].connection = NULL;
        service->clients[index].source = 0;
    }
    for (index = 0; index < COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE; ++index) {
        service->controller_token[index] = config->controller_token[index];
    }

    result = eventloop_create(&service->loop);
    if (result != CORE_OK) goto fail_block;

    result = ipc_server_create(config->endpoint, &service->server);
    if (result != CORE_OK) goto fail_loop;

    result = ipc_server_get_fd(service->server, &fd);
    if (result == CORE_OK) {
        result = eventloop_add_fd(service->loop, fd, EVENT_READABLE,
                                  &service->server_source);
    }
    if (result != CORE_OK) goto fail_server;

    *out_service = service;
    return CORE_OK;

fail_server:
    (void)ipc_server_close(&service->server);
fail_loop:
    (void)eventloop_close(&service->loop);
fail_block:
    (void)memory_free(&block);
    return result;
}

ErrorCode compositor_service_poll(CompositorServiceHandle service,
                                  DurationMs timeout_ms)
{
    Event events[COMPOSITOR_SERVICE_EVENT_MAX];
    usize count;
    usize index;
    ErrorCode result;

    if (service == NULL) return CORE_INVALID_ARGUMENT;

    result = eventloop_wait(service->loop, events, COMPOSITOR_SERVICE_EVENT_MAX,
                            timeout_ms, &count);
    if (result != CORE_OK) return result;

    for (index = 0; index < count; ++index) {
        if (events[index].source_id == service->server_source) {
            result = accept_clients(service);
        } else {
            CompositorClient *client =
                find_client(service, events[index].source_id);
            result = client == NULL ? CORE_NOT_FOUND :
                service_client_event(service, client, events[index].mask);
        }
        if (result != CORE_OK) return result;
    }

    return CORE_OK;
}

ErrorCode compositor_service_close(CompositorServiceHandle *service)
{
    CompositorServiceHandle handle;
    MemoryBlock block;
    usize index;
    ErrorCode result = CORE_OK;

    if (service == NULL || *service == NULL) return CORE_INVALID_ARGUMENT;

    handle = *service;
    for (index = 0; index < COMPOSITOR_SERVICE_CLIENT_MAX; ++index) {
        if (handle->clients[index].connection != NULL) {
            remove_client(handle, &handle->clients[index]);
        }
    }

    (void)eventloop_remove_source(handle->loop, handle->server_source);
    if (ipc_server_close(&handle->server) != CORE_OK) result = CORE_IO_ERROR;
    if (eventloop_close(&handle->loop) != CORE_OK) result = CORE_IO_ERROR;

    block = handle->handle_block;
    *service = NULL;
    if (memory_free(&block) != CORE_OK && result == CORE_OK) {
        result = CORE_IO_ERROR;
    }
    return result;
}
