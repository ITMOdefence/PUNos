#include <displayserv.h>

#include <gui/protocol.h>
#include <special/display.h>
#include <special/ipc.h>
#include <sys/eventloop.h>
#include <sys/memory.h>

#define DISPLAY_SERVICE_CLIENT_MAX ((usize)16)
#define DISPLAY_SERVICE_EVENT_MAX ((usize)32)
#define DISPLAY_SERVICE_RESPONSE_MAX ((usize)2048)
#define DISPLAY_SERVICE_ID ((u64)1)

typedef struct DisplayClient {
    IpcConnectionHandle connection;
    EventSourceId source;
    bool subscribed;
    bool presenter;
    u8 invalid_bind_attempts;
} DisplayClient;

typedef struct PendingPresent {
    DisplayFrameHandle frame;
    DisplayClient *client;
    u64 request_id;
    u64 present_id;
    u64 generation;
} PendingPresent;

struct DisplayServiceHandle {
    MemoryBlock handle_block;
    EventLoopHandle loop;
    DisplayBackendHandle backend;
    IpcServerHandle server;
    EventSourceId server_source;
    EventSourceId backend_source;
    DisplayClient clients[DISPLAY_SERVICE_CLIENT_MAX];
    PendingPresent presents[DISPLAY_PRESENT_QUEUE_DEPTH];
    u8 presenter_token[DISPLAY_SERVICE_PRESENTER_TOKEN_SIZE];
    u64 backend_output_id;
    u64 generation;
    u64 serial;
    u64 next_present_id;
    bool backend_lost;
};

static void remove_client(DisplayServiceHandle service, DisplayClient *client);

static DisplayClient *find_client(DisplayServiceHandle service,
                                  EventSourceId source)
{
    usize index;
    for (index = 0; index < DISPLAY_SERVICE_CLIENT_MAX; ++index) {
        if (service->clients[index].connection != NULL &&
            service->clients[index].source == source) {
            return &service->clients[index];
        }
    }
    return NULL;
}

static ErrorCode output_info(DisplayServiceHandle service,
                             GuiDisplayInfo *out_info)
{
    DisplayOutputInfoList outputs;
    DisplayOutputInfo *source;
    usize index;
    ErrorCode result = display_backend_list_outputs(service->backend, &outputs);

    if (result != CORE_OK) {
        return result;
    }
    if (outputs.count == 0) {
        (void)display_output_info_list_destroy(&outputs);
        return CORE_NOT_FOUND;
    }
    source = &outputs.items[0];
    if (source->name.length > GUI_DISPLAY_NAME_MAX ||
        source->mode_count > GUI_DISPLAY_MODE_MAX) {
        (void)display_output_info_list_destroy(&outputs);
        return CORE_OVERFLOW;
    }
    out_info->display_id = DISPLAY_SERVICE_ID;
    out_info->generation = service->generation;
    out_info->flags = source->connected ? GUI_DISPLAY_CONNECTED : 0;
    if (source->enabled) out_info->flags |= GUI_DISPLAY_ENABLED;
    out_info->flags |= GUI_DISPLAY_PRIMARY;
    out_info->physical_width_mm = source->physical_width_mm;
    out_info->physical_height_mm = source->physical_height_mm;
    out_info->current_mode.width_px = source->current_mode.width_px;
    out_info->current_mode.height_px = source->current_mode.height_px;
    out_info->current_mode.refresh_millihz = source->current_mode.refresh_millihz;
    if (!source->enabled) {
        out_info->current_mode.width_px = 0;
        out_info->current_mode.height_px = 0;
        out_info->current_mode.refresh_millihz = 0;
    }
    out_info->name_length = (u16)source->name.length;
    out_info->mode_count = (u16)source->mode_count;
    for (index = 0; index < source->name.length; ++index) {
        out_info->name[index] = (u8)source->name.data[index];
    }
    for (index = 0; index < source->mode_count; ++index) {
        out_info->modes[index].width_px = source->modes[index].width_px;
        out_info->modes[index].height_px = source->modes[index].height_px;
        out_info->modes[index].refresh_millihz =
            source->modes[index].refresh_millihz;
    }
    return display_output_info_list_destroy(&outputs);
}

static ErrorCode queue_payload(DisplayServiceHandle service,
                               DisplayClient *client, IpcMessageType type,
                               u8 *data, usize size)
{
    IpcMessage message = { type, 0, { data, size }, IPC_NO_DESCRIPTOR, 0 };
    bool pending;
    ErrorCode result = ipc_connection_send(client->connection, &message);

    if (result != CORE_OK) {
        return result;
    }
    result = ipc_connection_flush(client->connection, &pending);
    if (result != CORE_OK) {
        return result;
    }
    return eventloop_update_fd(service->loop, client->source,
        pending ? EVENT_READABLE | EVENT_WRITABLE | EVENT_ERROR | EVENT_HANGUP :
                  EVENT_READABLE | EVENT_ERROR | EVENT_HANGUP);
}

static ErrorCode send_basic(DisplayServiceHandle service, DisplayClient *client,
                            IpcMessageType type, u64 request_id,
                            ErrorCode error)
{
    u8 payload[12];
    usize size;
    GuiBasicResult result = { error, request_id };
    ErrorCode code = gui_encode_basic_result(&result,
                                              (Bytes){ payload, sizeof(payload) },
                                              &size);
    if (code != CORE_OK) return code;
    return queue_payload(service, client, type, payload, size);
}

static ErrorCode broadcast_display_changed(DisplayServiceHandle service)
{
    GuiDisplayInfo info;
    u8 payload[DISPLAY_SERVICE_RESPONSE_MAX];
    usize size;
    usize index;
    ErrorCode result = output_info(service, &info);

    if (result != CORE_OK) return result;
    result = gui_encode_display_info_event(service->serial, &info,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    for (index = 0; index < DISPLAY_SERVICE_CLIENT_MAX; ++index) {
        DisplayClient *client = &service->clients[index];
        if (client->connection != NULL && client->subscribed) {
            result = queue_payload(service, client, GUI_DISPLAY_CHANGED,
                                   payload, size);
            if (result != CORE_OK) {
                remove_client(service, client);
            }
        }
    }
    return CORE_OK;
}

static ErrorCode broadcast_backend_lost(DisplayServiceHandle service,
                                        ErrorCode error)
{
    GuiDisplayBackendLostEvent event;
    u8 payload[16];
    usize size;
    usize index;
    ErrorCode result;

    if (service->backend_lost) return CORE_OK;
    service->backend_lost = true;
    ++service->serial;
    event.serial = service->serial;
    event.error = error == CORE_OK ? CORE_IO_ERROR : error;
    result = gui_encode_display_backend_lost(&event,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    for (index = 0; index < DISPLAY_SERVICE_CLIENT_MAX; ++index) {
        DisplayClient *client = &service->clients[index];
        if (client->connection != NULL && client->subscribed) {
            result = queue_payload(service, client, GUI_DISPLAY_BACKEND_LOST,
                                   payload, size);
            if (result != CORE_OK) {
                remove_client(service, client);
            }
        }
    }
    return CORE_OK;
}

static ErrorCode handle_list(DisplayServiceHandle service,
                             DisplayClient *client, const IpcMessage *message)
{
    GuiRequestPrefix request;
    GuiDisplayInfo info;
    GuiDisplayListHeader header;
    u8 payload[DISPLAY_SERVICE_RESPONSE_MAX];
    usize header_size;
    usize info_size;
    ErrorCode result = gui_decode_versioned_request(message, GUI_DISPLAY_LIST,
                                                     &request);
    if (result != CORE_OK) return result;
    result = output_info(service, &info);
    header.result.error = result;
    header.result.request_id = request.request_id;
    header.snapshot_serial = result == CORE_OK ? service->serial : 0;
    header.display_count = result == CORE_OK ? 1 : 0;
    result = gui_encode_display_list_header(&header,
        (Bytes){ payload, sizeof(payload) }, &header_size);
    if (result != CORE_OK) return result;
    if (header.display_count != 0) {
        result = gui_encode_display_info(&info,
            (Bytes){ payload + header_size, sizeof(payload) - header_size },
            &info_size);
        if (result != CORE_OK) return result;
        header_size += info_size;
    }
    return queue_payload(service, client, GUI_DISPLAY_LIST_RESULT, payload,
                         header_size);
}

static ErrorCode handle_get_state(DisplayServiceHandle service,
                                  DisplayClient *client,
                                  const IpcMessage *message)
{
    GuiRequestPrefix request;
    GuiBasicResult result_prefix;
    GuiDisplayInfo info;
    u64 display_id;
    u8 payload[DISPLAY_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result = gui_decode_display_id_request(
        message, GUI_DISPLAY_GET_STATE, &request, &display_id);
    if (result != CORE_OK) return result;
    result = display_id == DISPLAY_SERVICE_ID ? output_info(service, &info) :
                                               CORE_NOT_FOUND;
    result_prefix.error = result;
    result_prefix.request_id = request.request_id;
    result = gui_encode_display_info_result(&result_prefix,
        result_prefix.error == CORE_OK ? &info : NULL,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    return queue_payload(service, client, GUI_DISPLAY_GET_STATE_RESULT,
                         payload, size);
}

static bool token_matches(const u8 *left, const u8 *right)
{
    u8 difference = 0;
    usize index;
    for (index = 0; index < DISPLAY_SERVICE_PRESENTER_TOKEN_SIZE; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0;
}

static ErrorCode handle_bind(DisplayServiceHandle service,
                             DisplayClient *client, const IpcMessage *message)
{
    GuiPresenterBindRequest request;
    usize index;
    ErrorCode result = gui_decode_presenter_bind(message, &request);
    if (result != CORE_OK) return result;
    if (!token_matches(request.presenter_token, service->presenter_token)) {
        ++client->invalid_bind_attempts;
        if (client->invalid_bind_attempts >= 3) {
            return CORE_PERMISSION_DENIED;
        }
        return send_basic(service, client, GUI_DISPLAY_BIND_PRESENTER_RESULT,
                          request.prefix.request_id, CORE_PERMISSION_DENIED);
    }
    for (index = 0; index < DISPLAY_SERVICE_CLIENT_MAX; ++index) {
        if (service->clients[index].presenter &&
            &service->clients[index] != client) {
            return send_basic(service, client,
                GUI_DISPLAY_BIND_PRESENTER_RESULT, request.prefix.request_id,
                CORE_ALREADY_EXISTS);
        }
    }
    client->presenter = true;
    client->invalid_bind_attempts = 0;
    return send_basic(service, client, GUI_DISPLAY_BIND_PRESENTER_RESULT,
                      request.prefix.request_id, CORE_OK);
}

static ErrorCode handle_subscribe(DisplayServiceHandle service,
                                  DisplayClient *client,
                                  const IpcMessage *message)
{
    GuiRequestPrefix request;
    ErrorCode result = gui_decode_versioned_request(
        message, GUI_DISPLAY_SUBSCRIBE, &request);
    if (result != CORE_OK) return result;
    client->subscribed = true;
    return send_basic(service, client, GUI_DISPLAY_SUBSCRIBE_RESULT,
                      request.request_id, CORE_OK);
}

static ErrorCode handle_configure(DisplayServiceHandle service,
                                  DisplayClient *client,
                                  const IpcMessage *message)
{
    GuiDisplayConfigureRequest request;
    GuiBasicResult result_prefix;
    GuiDisplayInfo info;
    DisplayOutputConfig config;
    u8 payload[DISPLAY_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result = gui_decode_display_configure(message, &request);
    if (result != CORE_OK) return result;
    if (!client->presenter) {
        result = CORE_PERMISSION_DENIED;
    } else if (request.display_id != DISPLAY_SERVICE_ID) {
        result = CORE_NOT_FOUND;
    } else if (request.generation != service->generation) {
        result = CORE_INVALID_STATE;
    } else {
        config.enabled = request.enabled;
        config.orientation = request.orientation;
        config.mode.width_px = request.mode.width_px;
        config.mode.height_px = request.mode.height_px;
        config.mode.refresh_millihz = request.mode.refresh_millihz;
        result = display_output_configure(service->backend,
                                          service->backend_output_id, &config);
        if (result == CORE_OK) {
            ++service->generation;
            ++service->serial;
            result = output_info(service, &info);
        }
    }
    result_prefix.error = result;
    result_prefix.request_id = request.prefix.request_id;
    result = gui_encode_display_info_result(&result_prefix,
        result_prefix.error == CORE_OK ? &info : NULL,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    result = queue_payload(service, client, GUI_DISPLAY_CONFIGURE_RESULT,
                           payload, size);
    if (result != CORE_OK || result_prefix.error != CORE_OK) return result;
    return broadcast_display_changed(service);
}

static ErrorCode handle_present(DisplayServiceHandle service,
                                DisplayClient *client, IpcMessage *message)
{
    GuiDisplayPresentRequest request;
    DisplayFrameDescriptor descriptor;
    DisplayDamage damage;
    DisplayRect rects[GUI_MAX_DAMAGE_RECTS];
    DisplayFrameHandle frame;
    usize slot;
    usize index;
    ErrorCode result = gui_decode_display_present(message, &request);

    if (result != CORE_OK) return result;
    if (!client->presenter) {
        return send_basic(service, client, GUI_DISPLAY_PRESENT_RESULT,
                          request.prefix.request_id, CORE_PERMISSION_DENIED);
    }
    if (request.display_id != DISPLAY_SERVICE_ID) {
        return send_basic(service, client, GUI_DISPLAY_PRESENT_RESULT,
                          request.prefix.request_id, CORE_NOT_FOUND);
    }
    if (request.generation != service->generation) {
        return send_basic(service, client, GUI_DISPLAY_PRESENT_RESULT,
                          request.prefix.request_id, CORE_INVALID_STATE);
    }
    for (slot = 0; slot < DISPLAY_PRESENT_QUEUE_DEPTH; ++slot) {
        if (service->presents[slot].frame == NULL) break;
    }
    if (slot == DISPLAY_PRESENT_QUEUE_DEPTH) {
        return send_basic(service, client, GUI_DISPLAY_PRESENT_RESULT,
                          request.prefix.request_id, CORE_WOULD_BLOCK);
    }
    descriptor.width = request.width;
    descriptor.height = request.height;
    descriptor.stride = request.stride;
    descriptor.format = request.pixel_format;
    descriptor.byte_size = request.byte_size;
    result = display_frame_import(service->backend, &descriptor,
                                  message->descriptor, &frame);
    if (result != CORE_OK) {
        return send_basic(service, client, GUI_DISPLAY_PRESENT_RESULT,
                          request.prefix.request_id, result);
    }
    message->descriptor = IPC_NO_DESCRIPTOR;
    for (index = 0; index < request.damage_count; ++index) {
        rects[index].x = request.damage[index].x;
        rects[index].y = request.damage[index].y;
        rects[index].width = request.damage[index].width;
        rects[index].height = request.damage[index].height;
    }
    damage.rects = request.damage_count == 0 ? NULL : rects;
    damage.count = request.damage_count;
    damage.full = (request.flags & GUI_FULL_DAMAGE) != 0;
    if (service->next_present_id == 0) ++service->next_present_id;
    result = display_output_present(service->backend,
        service->backend_output_id, frame, &damage, service->next_present_id);
    if (result != CORE_OK) {
        (void)display_frame_destroy(&frame);
        return send_basic(service, client, GUI_DISPLAY_PRESENT_RESULT,
                          request.prefix.request_id, result);
    }
    service->presents[slot].frame = frame;
    service->presents[slot].client = client;
    service->presents[slot].request_id = request.prefix.request_id;
    service->presents[slot].present_id = service->next_present_id++;
    service->presents[slot].generation = service->generation;
    return send_basic(service, client, GUI_DISPLAY_PRESENT_RESULT,
                      request.prefix.request_id, CORE_OK);
}

static ErrorCode dispatch(DisplayServiceHandle service, DisplayClient *client,
                          IpcMessage *message)
{
    switch (message->type) {
    case GUI_DISPLAY_LIST: return handle_list(service, client, message);
    case GUI_DISPLAY_GET_STATE: return handle_get_state(service, client, message);
    case GUI_DISPLAY_SUBSCRIBE: return handle_subscribe(service, client, message);
    case GUI_DISPLAY_BIND_PRESENTER: return handle_bind(service, client, message);
    case GUI_DISPLAY_CONFIGURE: return handle_configure(service, client, message);
    case GUI_DISPLAY_PRESENT: return handle_present(service, client, message);
    default: return CORE_UNSUPPORTED;
    }
}

static void remove_client(DisplayServiceHandle service, DisplayClient *client)
{
    usize index;
    for (index = 0; index < DISPLAY_PRESENT_QUEUE_DEPTH; ++index) {
        if (service->presents[index].client == client) {
            service->presents[index].client = NULL;
        }
    }
    (void)eventloop_remove_source(service->loop, client->source);
    (void)ipc_connection_close(&client->connection);
    client->source = 0;
    client->subscribed = false;
    client->presenter = false;
    client->invalid_bind_attempts = 0;
}

static ErrorCode backend_event(DisplayServiceHandle service,
                               const DisplayBackendEvent *event)
{
    usize index;
    PendingPresent *pending = NULL;
    u8 payload[32];
    usize size;
    ErrorCode result = CORE_OK;

    if (event->type == DISPLAY_BACKEND_EVENT_OUTPUT_ADDED ||
        event->type == DISPLAY_BACKEND_EVENT_OUTPUT_CHANGED) {
        ++service->generation;
        ++service->serial;
        return broadcast_display_changed(service);
    }

    for (index = 0; index < DISPLAY_PRESENT_QUEUE_DEPTH; ++index) {
        if (service->presents[index].frame != NULL &&
            service->presents[index].present_id == event->present_id) {
            pending = &service->presents[index];
            break;
        }
    }
    if (pending == NULL) return CORE_NOT_FOUND;
    if (pending->client != NULL) {
        if (event->type == DISPLAY_BACKEND_EVENT_PRESENT_COMPLETE) {
            MonotonicTime now;
            GuiDisplayPresentedEvent presented;
            result = time_monotonic_now(&now);
            if (result == CORE_OK) {
                presented.request_id = pending->request_id;
                presented.display_id = DISPLAY_SERVICE_ID;
                presented.generation = pending->generation;
                presented.presented_at_ms = now;
                result = gui_encode_display_presented(&presented,
                    (Bytes){ payload, sizeof(payload) }, &size);
            }
            if (result == CORE_OK) {
                result = queue_payload(service, pending->client,
                    GUI_DISPLAY_PRESENTED, payload, size);
            }
        } else {
            GuiDisplayPresentFailedEvent failed;
            failed.request_id = pending->request_id;
            failed.display_id = DISPLAY_SERVICE_ID;
            failed.generation = pending->generation;
            failed.error = event->error == CORE_OK ? CORE_IO_ERROR : event->error;
            result = gui_encode_display_present_failed(&failed,
                (Bytes){ payload, sizeof(payload) }, &size);
            if (result == CORE_OK) {
                result = queue_payload(service, pending->client,
                    GUI_DISPLAY_PRESENT_FAILED, payload, size);
            }
        }
    }
    if (display_frame_destroy(&pending->frame) != CORE_OK && result == CORE_OK) {
        result = CORE_IO_ERROR;
    }
    pending->client = NULL;
    pending->request_id = 0;
    pending->present_id = 0;
    pending->generation = 0;
    return result;
}

static ErrorCode handle_backend_lost(DisplayServiceHandle service,
                                     ErrorCode error)
{
    usize index;
    ErrorCode result = CORE_OK;

    if (service->backend_lost) return CORE_OK;
    for (index = 0; index < DISPLAY_PRESENT_QUEUE_DEPTH; ++index) {
        PendingPresent *pending = &service->presents[index];
        if (pending->frame != NULL) {
            DisplayBackendEvent failed;
            ErrorCode event_result;
            failed.type = DISPLAY_BACKEND_EVENT_PRESENT_FAILED;
            failed.error = error == CORE_OK ? CORE_IO_ERROR : error;
            failed.backend_output_id = service->backend_output_id;
            failed.present_id = pending->present_id;
            event_result = backend_event(service, &failed);
            if (result == CORE_OK && event_result != CORE_OK) {
                result = event_result;
            }
        }
    }
    if (broadcast_backend_lost(service, error) != CORE_OK && result == CORE_OK) {
        result = CORE_IO_ERROR;
    }
    return result == CORE_OK ? CORE_IO_ERROR : result;
}

static ErrorCode accept_clients(DisplayServiceHandle service)
{
    while (true) {
        IpcConnectionHandle connection;
        usize index;
        ErrorCode result = ipc_server_accept(service->server, &connection);
        if (result == CORE_WOULD_BLOCK) return CORE_OK;
        if (result != CORE_OK) return result;
        for (index = 0; index < DISPLAY_SERVICE_CLIENT_MAX; ++index) {
            if (service->clients[index].connection == NULL) break;
        }
        if (index == DISPLAY_SERVICE_CLIENT_MAX) {
            (void)ipc_connection_close(&connection);
            continue;
        }
        {
            i32 fd;
            EventSourceId source;
            result = ipc_connection_get_fd(connection, &fd);
            if (result == CORE_OK) {
                result = eventloop_add_fd(service->loop, fd,
                    EVENT_READABLE | EVENT_ERROR | EVENT_HANGUP, &source);
            }
            if (result != CORE_OK) {
                (void)ipc_connection_close(&connection);
                return result;
            }
            service->clients[index].connection = connection;
            service->clients[index].source = source;
            service->clients[index].subscribed = false;
            service->clients[index].presenter = false;
            service->clients[index].invalid_bind_attempts = 0;
        }
    }
}

static ErrorCode service_client_event(DisplayServiceHandle service,
                                      DisplayClient *client, EventMask mask)
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
        (void)eventloop_update_fd(service->loop, client->source,
            pending ? EVENT_READABLE | EVENT_WRITABLE | EVENT_ERROR | EVENT_HANGUP :
                      EVENT_READABLE | EVENT_ERROR | EVENT_HANGUP);
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

ErrorCode display_service_create(const String *endpoint,
                                 const u8 presenter_token[32],
                                 DisplayServiceHandle *out_service)
{
    String backend_name;
    DisplayServiceConfig config;
    ErrorCode result;

    if (endpoint == NULL || presenter_token == NULL || out_service == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = string_from_c_str("software", &backend_name);
    if (result != CORE_OK) return result;
    config.endpoint = endpoint;
    config.backend_name = &backend_name;
    config.device_path = NULL;
    config.presenter_token = presenter_token;
    result = display_service_create_config(&config, out_service);
    (void)string_destroy(&backend_name);
    return result;
}

ErrorCode display_service_create_config(const DisplayServiceConfig *config,
                                        DisplayServiceHandle *out_service)
{
    MemoryBlock block;
    DisplayServiceHandle service;
    DisplayBackendConfig backend_config;
    i32 fd;
    usize index;
    ErrorCode result;

    if (config == NULL || config->endpoint == NULL ||
        config->backend_name == NULL || config->presenter_token == NULL ||
        out_service == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = memory_allocate(sizeof(*service), &block);
    if (result != CORE_OK) return result;
    service = (DisplayServiceHandle)block.data;
    service->handle_block = block;
    service->generation = 1;
    service->serial = 1;
    service->next_present_id = 1;
    for (index = 0; index < 32; ++index) {
        service->presenter_token[index] = config->presenter_token[index];
    }
    backend_config.backend_name = config->backend_name;
    backend_config.device_path = config->device_path;
    result = display_backend_open(&backend_config, &service->backend);
    if (result != CORE_OK) goto fail_block;
    {
        DisplayOutputInfoList outputs;
        result = display_backend_list_outputs(service->backend, &outputs);
        if (result != CORE_OK) goto fail_backend;
        if (outputs.count == 0) {
            (void)display_output_info_list_destroy(&outputs);
            result = CORE_NOT_FOUND;
            goto fail_backend;
        }
        service->backend_output_id = outputs.items[0].backend_output_id;
        (void)display_output_info_list_destroy(&outputs);
    }
    result = eventloop_create(&service->loop);
    if (result != CORE_OK) goto fail_backend;
    result = ipc_server_create(config->endpoint, &service->server);
    if (result != CORE_OK) goto fail_loop;
    result = ipc_server_get_fd(service->server, &fd);
    if (result == CORE_OK) {
        result = eventloop_add_fd(service->loop, fd, EVENT_READABLE,
                                  &service->server_source);
    }
    if (result != CORE_OK) goto fail_server;
    result = display_backend_get_fd(service->backend, &fd);
    if (result != CORE_OK) goto fail_server_source;
    result = eventloop_add_fd(service->loop, fd,
        EVENT_READABLE | EVENT_ERROR | EVENT_HANGUP, &service->backend_source);
    if (result != CORE_OK) goto fail_server_source;
    *out_service = service;
    return CORE_OK;

fail_server_source:
    (void)eventloop_remove_source(service->loop, service->server_source);
fail_server:
    (void)ipc_server_close(&service->server);
fail_loop:
    (void)eventloop_close(&service->loop);
fail_backend:
    (void)display_backend_close(&service->backend);
fail_block:
    (void)memory_free(&block);
    return result;
}

ErrorCode display_service_poll(DisplayServiceHandle service,
                               DurationMs timeout_ms)
{
    Event events[DISPLAY_SERVICE_EVENT_MAX];
    usize count;
    usize index;
    ErrorCode result;

    if (service == NULL) return CORE_INVALID_ARGUMENT;
    result = eventloop_wait(service->loop, events, DISPLAY_SERVICE_EVENT_MAX,
                            timeout_ms, &count);
    if (result != CORE_OK) return result;
    for (index = 0; index < count; ++index) {
        if (events[index].source_id == service->server_source) {
            result = accept_clients(service);
        } else if (events[index].source_id == service->backend_source) {
            DisplayEventStatus status;
            DisplayBackendEvent event;
            do {
                result = display_backend_next_event(service->backend, &status,
                                                     &event);
                if (result == CORE_OK && status == DISPLAY_EVENT_READY) {
                    result = event.type == DISPLAY_BACKEND_EVENT_LOST ?
                        handle_backend_lost(service, event.error) :
                        backend_event(service, &event);
                } else if (result == CORE_OK &&
                           status == DISPLAY_EVENT_BACKEND_LOST) {
                    result = handle_backend_lost(service, CORE_IO_ERROR);
                }
            } while (result == CORE_OK && status == DISPLAY_EVENT_READY);
        } else {
            DisplayClient *client = find_client(service, events[index].source_id);
            result = client == NULL ? CORE_NOT_FOUND :
                service_client_event(service, client, events[index].mask);
        }
        if (result != CORE_OK) return result;
    }
    return CORE_OK;
}

ErrorCode display_service_close(DisplayServiceHandle *service)
{
    DisplayServiceHandle handle;
    MemoryBlock block;
    usize index;
    ErrorCode result = CORE_OK;

    if (service == NULL || *service == NULL) return CORE_INVALID_ARGUMENT;
    handle = *service;
    for (index = 0; index < DISPLAY_SERVICE_CLIENT_MAX; ++index) {
        if (handle->clients[index].connection != NULL) {
            remove_client(handle, &handle->clients[index]);
        }
    }
    for (index = 0; index < DISPLAY_PRESENT_QUEUE_DEPTH; ++index) {
        if (handle->presents[index].frame != NULL) {
            /* A queued frame is released by draining its completion first. */
            DisplayEventStatus status;
            DisplayBackendEvent event;
            while (handle->presents[index].frame != NULL &&
                   display_backend_next_event(handle->backend, &status,
                                               &event) == CORE_OK &&
                   status == DISPLAY_EVENT_READY) {
                (void)backend_event(handle, &event);
            }
        }
    }
    (void)eventloop_remove_source(handle->loop, handle->backend_source);
    (void)eventloop_remove_source(handle->loop, handle->server_source);
    if (ipc_server_close(&handle->server) != CORE_OK) result = CORE_IO_ERROR;
    if (display_backend_close(&handle->backend) != CORE_OK) result = CORE_IO_ERROR;
    if (eventloop_close(&handle->loop) != CORE_OK) result = CORE_IO_ERROR;
    block = handle->handle_block;
    if (memory_free(&block) != CORE_OK) result = CORE_IO_ERROR;
    *service = NULL;
    return result;
}
