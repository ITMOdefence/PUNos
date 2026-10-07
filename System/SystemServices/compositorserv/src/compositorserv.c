#include <compositorserv.h>

#include <gui/protocol.h>
#include <special/ipc.h>
#include <sys/eventloop.h>
#include <sys/memory.h>
#include <sys/random.h>
#include <punos/syscall/syscall.h>

#define COMPOSITOR_SERVICE_CLIENT_MAX ((usize)32)
#define COMPOSITOR_SERVICE_EVENT_MAX ((usize)64)
#define COMPOSITOR_SERVICE_SURFACE_MAX ((usize)64)
#define COMPOSITOR_SERVICE_BUFFER_MAX ((usize)64)
#define COMPOSITOR_SERVICE_RESPONSE_MAX ((usize)64)

typedef struct CompositorSurface {
    bool active;
    u64 surface_id;
    u8 bind_token[GUI_SURFACE_BIND_TOKEN_SIZE];
} CompositorSurface;

typedef struct CompositorBuffer {
    bool active;
    u64 buffer_id;
    u32 width;
    u32 height;
    u32 stride;
    GuiPixelFormat pixel_format;
    u64 byte_size;
    i32 fd;
    SharedMemoryMapping mapping;
} CompositorBuffer;

typedef struct CompositorClient {
    IpcConnectionHandle connection;
    EventSourceId source;
    CompositorSurface surfaces[COMPOSITOR_SERVICE_SURFACE_MAX];
    CompositorBuffer buffers[COMPOSITOR_SERVICE_BUFFER_MAX];
} CompositorClient;

struct CompositorServiceHandle {
    MemoryBlock handle_block;
    EventLoopHandle loop;
    IpcServerHandle server;
    EventSourceId server_source;
    CompositorClient clients[COMPOSITOR_SERVICE_CLIENT_MAX];
    u8 controller_token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE];
    u64 next_surface_id;
    u64 next_buffer_id;
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

static void clear_surface(CompositorSurface *surface)
{
    usize index;

    surface->active = false;
    surface->surface_id = 0;
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        surface->bind_token[index] = 0;
    }
}

static void clear_client_surfaces(CompositorClient *client)
{
    usize index;

    for (index = 0; index < COMPOSITOR_SERVICE_SURFACE_MAX; ++index) {
        clear_surface(&client->surfaces[index]);
    }
}

static CompositorSurface *find_surface(CompositorClient *client,
                                       u64 surface_id)
{
    usize index;

    for (index = 0; index < COMPOSITOR_SERVICE_SURFACE_MAX; ++index) {
        if (client->surfaces[index].active &&
            client->surfaces[index].surface_id == surface_id) {
            return &client->surfaces[index];
        }
    }
    return NULL;
}

static CompositorSurface *find_free_surface(CompositorClient *client)
{
    usize index;

    for (index = 0; index < COMPOSITOR_SERVICE_SURFACE_MAX; ++index) {
        if (!client->surfaces[index].active) return &client->surfaces[index];
    }
    return NULL;
}

static void clear_buffer(CompositorBuffer *buffer)
{
    if (buffer->mapping.data != NULL) {
        (void)memory_shared_unmap(&buffer->mapping);
    }
    if (buffer->fd >= 0) {
        (void)close(buffer->fd);
    }
    buffer->active = false;
    buffer->buffer_id = 0;
    buffer->width = 0;
    buffer->height = 0;
    buffer->stride = 0;
    buffer->pixel_format = 0;
    buffer->byte_size = 0;
    buffer->fd = -1;
    buffer->mapping.data = NULL;
    buffer->mapping.size = 0;
}

static void clear_client_buffers(CompositorClient *client)
{
    usize index;

    for (index = 0; index < COMPOSITOR_SERVICE_BUFFER_MAX; ++index) {
        clear_buffer(&client->buffers[index]);
    }
}

static CompositorBuffer *find_buffer(CompositorClient *client, u64 buffer_id)
{
    usize index;

    for (index = 0; index < COMPOSITOR_SERVICE_BUFFER_MAX; ++index) {
        if (client->buffers[index].active &&
            client->buffers[index].buffer_id == buffer_id) {
            return &client->buffers[index];
        }
    }
    return NULL;
}

static CompositorBuffer *find_free_buffer(CompositorClient *client)
{
    usize index;

    for (index = 0; index < COMPOSITOR_SERVICE_BUFFER_MAX; ++index) {
        if (!client->buffers[index].active) return &client->buffers[index];
    }
    return NULL;
}

static ErrorCode queue_payload(CompositorServiceHandle service,
                               CompositorClient *client, IpcMessageType type,
                               u8 *data, usize size)
{
    IpcMessage message = { type, 0, { data, size }, IPC_NO_DESCRIPTOR, 0 };
    bool pending;
    ErrorCode result = ipc_connection_send(client->connection, &message);

    if (result != CORE_OK) return result;
    result = ipc_connection_flush(client->connection, &pending);
    if (result != CORE_OK) return result;
    return eventloop_update_fd(
        service->loop, client->source,
        pending ? EVENT_READABLE | EVENT_WRITABLE | EVENT_ERROR | EVENT_HANGUP
                : EVENT_READABLE | EVENT_ERROR | EVENT_HANGUP);
}

static ErrorCode send_basic(CompositorServiceHandle service,
                            CompositorClient *client, IpcMessageType type,
                            u64 request_id, ErrorCode error)
{
    u8 payload[16];
    usize size;
    GuiBasicResult result = { error, request_id };
    ErrorCode code = gui_encode_basic_result(
        &result, (Bytes){ payload, sizeof(payload) }, &size);

    if (code != CORE_OK) return code;
    return queue_payload(service, client, type, payload, size);
}

static ErrorCode handle_surface_create(CompositorServiceHandle service,
                                       CompositorClient *client,
                                       const IpcMessage *message)
{
    GuiRequestPrefix request;
    GuiSurfaceCreateResult response;
    CompositorSurface *surface;
    u8 payload[COMPOSITOR_SERVICE_RESPONSE_MAX];
    usize size;
    usize index;
    ErrorCode result = gui_decode_versioned_request(
        message, GUI_SURFACE_CREATE, &request);

    if (result != CORE_OK) return result;

    response.result.request_id = request.request_id;
    response.surface_id = 0;
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        response.surface_bind_token[index] = 0;
    }

    surface = find_free_surface(client);
    if (surface == NULL) {
        response.result.error = CORE_OUT_OF_MEMORY;
    } else if (service->next_surface_id == 0) {
        response.result.error = CORE_OVERFLOW;
    } else {
        result = random_fill((Bytes){ surface->bind_token,
                                     GUI_SURFACE_BIND_TOKEN_SIZE });
        if (result != CORE_OK) {
            response.result.error = result;
        } else {
            surface->active = true;
            surface->surface_id = service->next_surface_id++;
            response.result.error = CORE_OK;
            response.surface_id = surface->surface_id;
            for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
                response.surface_bind_token[index] = surface->bind_token[index];
            }
        }
    }

    result = gui_encode_surface_create_result(
        &response, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    return queue_payload(service, client, GUI_SURFACE_CREATE_RESULT,
                         payload, size);
}

static ErrorCode handle_surface_destroy(CompositorServiceHandle service,
                                        CompositorClient *client,
                                        const IpcMessage *message)
{
    GuiSurfaceIdRequest request;
    CompositorSurface *surface;
    ErrorCode result = gui_decode_surface_id_request(
        message, GUI_SURFACE_DESTROY, &request);

    if (result != CORE_OK) return result;
    surface = find_surface(client, request.surface_id);
    if (surface == NULL) {
        return send_basic(service, client, GUI_SURFACE_DESTROY_RESULT,
                          request.prefix.request_id, CORE_NOT_FOUND);
    }

    clear_surface(surface);
    return send_basic(service, client, GUI_SURFACE_DESTROY_RESULT,
                      request.prefix.request_id, CORE_OK);
}

static ErrorCode send_buffer_import_result(CompositorServiceHandle service,
                                           CompositorClient *client,
                                           u64 request_id, ErrorCode error,
                                           u64 buffer_id)
{
    u8 payload[32];
    usize size;
    GuiBufferImportResult response;
    ErrorCode result;

    response.result.error = error;
    response.result.request_id = request_id;
    response.buffer_id = error == CORE_OK ? buffer_id : 0;
    result = gui_encode_buffer_import_result(
        &response, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    return queue_payload(service, client, GUI_SURFACE_BUFFER_IMPORT_RESULT,
                         payload, size);
}

static ErrorCode handle_buffer_import(CompositorServiceHandle service,
                                      CompositorClient *client,
                                      IpcMessage *message)
{
    GuiBufferImportRequest request;
    CompositorBuffer *buffer;
    SharedMemoryMapping mapping = { NULL, 0 };
    ErrorCode result = gui_decode_buffer_import(message, &request);

    if (result != CORE_OK) return result;

    buffer = find_free_buffer(client);
    if (buffer == NULL) {
        return send_buffer_import_result(service, client,
                                         request.prefix.request_id,
                                         CORE_OUT_OF_MEMORY, 0);
    }
    if (service->next_buffer_id == 0 || request.byte_size > (u64)USIZE_MAX) {
        return send_buffer_import_result(service, client,
                                         request.prefix.request_id,
                                         CORE_OVERFLOW, 0);
    }

    result = memory_shared_map_readonly(message->descriptor,
                                        (usize)request.byte_size, &mapping);
    if (result != CORE_OK) {
        return send_buffer_import_result(service, client,
                                         request.prefix.request_id, result, 0);
    }

    buffer->active = true;
    buffer->buffer_id = service->next_buffer_id++;
    buffer->width = request.width;
    buffer->height = request.height;
    buffer->stride = request.stride;
    buffer->pixel_format = request.pixel_format;
    buffer->byte_size = request.byte_size;
    buffer->fd = message->descriptor;
    buffer->mapping = mapping;

    /* Ownership of the received descriptor moves from IpcMessage to Buffer. */
    message->descriptor = IPC_NO_DESCRIPTOR;

    return send_buffer_import_result(service, client,
                                     request.prefix.request_id, CORE_OK,
                                     buffer->buffer_id);
}

static ErrorCode handle_buffer_destroy(CompositorServiceHandle service,
                                       CompositorClient *client,
                                       const IpcMessage *message)
{
    GuiBufferIdRequest request;
    CompositorBuffer *buffer;
    ErrorCode result = gui_decode_buffer_id_request(
        message, GUI_SURFACE_BUFFER_DESTROY, &request);

    if (result != CORE_OK) return result;
    buffer = find_buffer(client, request.buffer_id);
    if (buffer == NULL) {
        return send_basic(service, client, GUI_SURFACE_BUFFER_DESTROY_RESULT,
                          request.prefix.request_id, CORE_NOT_FOUND);
    }

    clear_buffer(buffer);
    return send_basic(service, client, GUI_SURFACE_BUFFER_DESTROY_RESULT,
                      request.prefix.request_id, CORE_OK);
}

static ErrorCode dispatch(CompositorServiceHandle service,
                          CompositorClient *client,
                          IpcMessage *message)
{
    switch (message->type) {
    case GUI_SURFACE_CREATE:
        return handle_surface_create(service, client, message);
    case GUI_SURFACE_DESTROY:
        return handle_surface_destroy(service, client, message);
    case GUI_SURFACE_BUFFER_IMPORT:
        return handle_buffer_import(service, client, message);
    case GUI_SURFACE_BUFFER_DESTROY:
        return handle_buffer_destroy(service, client, message);
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
    clear_client_surfaces(client);
    clear_client_buffers(client);
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

            clear_client_surfaces(&service->clients[index]);
            clear_client_buffers(&service->clients[index]);
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
        clear_client_surfaces(&service->clients[index]);
        clear_client_buffers(&service->clients[index]);
    }
    service->next_surface_id = 1;
    service->next_buffer_id = 1;
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
