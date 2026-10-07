#include <windowserv.h>

#include <special/ipc.h>
#include <sys/eventloop.h>
#include <sys/memory.h>

#define WINDOW_SERVICE_CLIENT_MAX ((usize)32)
#define WINDOW_SERVICE_WINDOW_MAX ((usize)512)
#define WINDOW_SERVICE_CLIENT_WINDOW_MAX ((usize)64)
#define WINDOW_SERVICE_EVENT_MAX ((usize)64)
#define WINDOW_SERVICE_RESPONSE_MAX ((usize)128)
#define LINUX_KEY_HOME ((u16)102)
#define LINUX_KEY_VOLUMEDOWN ((u16)114)
#define LINUX_KEY_VOLUMEUP ((u16)115)
#define LINUX_KEY_POWER ((u16)116)
#define LINUX_BTN_LEFT ((u16)272)
#define LINUX_BTN_TOUCH ((u16)330)
#define LINUX_REL_X ((u16)0)
#define LINUX_REL_Y ((u16)1)
#define LINUX_ABS_X ((u16)0)
#define LINUX_ABS_Y ((u16)1)
#define LINUX_ABS_MT_POSITION_X ((u16)53)
#define LINUX_ABS_MT_POSITION_Y ((u16)54)

typedef enum WindowCompositorState {
    WINDOW_COMPOSITOR_DISABLED = 0,
    WINDOW_COMPOSITOR_CONNECTING = 1,
    WINDOW_COMPOSITOR_BINDING = 2,
    WINDOW_COMPOSITOR_READY = 3,
    WINDOW_COMPOSITOR_SUSPENDED = 4
} WindowCompositorState;

typedef enum WindowIoState {
    WINDOW_IO_DISABLED = 0,
    WINDOW_IO_CONNECTING = 1,
    WINDOW_IO_SUBSCRIBING = 2,
    WINDOW_IO_READY = 3,
    WINDOW_IO_SUSPENDED = 4
} WindowIoState;

typedef struct WindowClient {
    IpcConnectionHandle connection;
    EventSourceId source;
    bool is_shell;
    u64 next_event_serial;
} WindowClient;

typedef struct WindowRecord {
    bool active;
    bool scene_bound;
    u64 window_id;
    WindowClient *owner;
    u64 surface_id;
    u8 surface_bind_token[GUI_SURFACE_BIND_TOKEN_SIZE];
    GuiWindowRole role;
    u64 parent_id;
    GuiWindowFlags flags;
    u16 title_length;
    u8 title[GUI_WINDOW_TITLE_MAX];
    GuiWindowLifecycle lifecycle;
    u64 pending_configure_serial;
    GuiWindowConfigureEvent confirmed_configure;
} WindowRecord;

struct WindowServiceHandle {
    MemoryBlock handle_block;
    EventLoopHandle loop;
    IpcServerHandle server;
    EventSourceId server_source;
    WindowClient clients[WINDOW_SERVICE_CLIENT_MAX];
    WindowRecord windows[WINDOW_SERVICE_WINDOW_MAX];
    WindowClient *shell_client;
    u8 shell_token[WINDOW_SERVICE_SHELL_TOKEN_SIZE];
    u64 next_window_id;
    u64 next_configure_serial;
    u32 viewport_width;
    u32 viewport_height;
    u32 scale;
    GuiDisplayOrientation orientation;
    GuiInsets safe_insets;
    u64 active_window_id;
    bool touch_active;
    u32 touch_id;
    u64 touch_window_id;
    IpcConnectionHandle compositor_connection;
    EventSourceId compositor_source;
    WindowCompositorState compositor_state;
    u8 controller_token[WINDOW_SERVICE_CONTROLLER_TOKEN_SIZE];
    u64 next_scene_request_id;
    IpcConnectionHandle io_connection;
    EventSourceId io_source;
    WindowIoState io_state;
    u32 io_touch_x;
    u32 io_touch_y;
    i32 io_pointer_x;
    i32 io_pointer_y;
    bool io_touch_down;
    u64 next_input_serial;
};

static void remove_client(WindowServiceHandle service, WindowClient *client);
static ErrorCode send_configure(WindowServiceHandle service,
                                WindowRecord *window);

static bool token_equal(const u8 *left, const u8 *right, usize size)
{
    usize index;
    u8 difference = 0;

    for (index = 0; index < size; ++index) {
        difference |= (u8)(left[index] ^ right[index]);
    }
    return difference == 0;
}

static WindowClient *find_client(WindowServiceHandle service,
                                 EventSourceId source)
{
    usize index;

    for (index = 0; index < WINDOW_SERVICE_CLIENT_MAX; ++index) {
        if (service->clients[index].connection != NULL &&
            service->clients[index].source == source) {
            return &service->clients[index];
        }
    }
    return NULL;
}

static WindowClient *find_free_client(WindowServiceHandle service)
{
    usize index;

    for (index = 0; index < WINDOW_SERVICE_CLIENT_MAX; ++index) {
        if (service->clients[index].connection == NULL) {
            return &service->clients[index];
        }
    }
    return NULL;
}

static WindowRecord *find_window(WindowServiceHandle service, u64 window_id)
{
    usize index;

    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        if (service->windows[index].active &&
            service->windows[index].window_id == window_id) {
            return &service->windows[index];
        }
    }
    return NULL;
}

static WindowRecord *find_free_window(WindowServiceHandle service)
{
    usize index;

    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        if (!service->windows[index].active) return &service->windows[index];
    }
    return NULL;
}

static usize client_window_count(WindowServiceHandle service,
                                 WindowClient *client)
{
    usize index;
    usize count = 0;

    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        if (service->windows[index].active &&
            service->windows[index].owner == client) {
            ++count;
        }
    }
    return count;
}

static GuiSceneLayer scene_layer_for_role(GuiWindowRole role)
{
    if (role == GUI_WINDOW_ROLE_DIALOG || role == GUI_WINDOW_ROLE_TRANSIENT) {
        return GUI_SCENE_LAYER_DIALOG;
    }
    if (role == GUI_WINDOW_ROLE_SYSTEM_BAR ||
        role == GUI_WINDOW_ROLE_SYSTEM_OVERLAY ||
        role == GUI_WINDOW_ROLE_IME) {
        return GUI_SCENE_LAYER_SYSTEM;
    }
    if (role == GUI_WINDOW_ROLE_LOCK) return GUI_SCENE_LAYER_LOCK;
    return GUI_SCENE_LAYER_APP;
}

static bool role_is_shell_only(GuiWindowRole role)
{
    return role == GUI_WINDOW_ROLE_HOME ||
           role == GUI_WINDOW_ROLE_SYSTEM_BAR ||
           role == GUI_WINDOW_ROLE_SYSTEM_OVERLAY ||
           role == GUI_WINDOW_ROLE_LOCK ||
           role == GUI_WINDOW_ROLE_IME;
}

static bool role_can_activate(GuiWindowRole role)
{
    return role == GUI_WINDOW_ROLE_APP || role == GUI_WINDOW_ROLE_HOME;
}

static bool window_is_visible(const WindowRecord *window)
{
    if (!window->active ||
        window->lifecycle == GUI_WINDOW_LIFECYCLE_HIDDEN ||
        window->lifecycle == GUI_WINDOW_LIFECYCLE_CLOSING ||
        window->lifecycle == GUI_WINDOW_LIFECYCLE_SUSPENDED) {
        return false;
    }
    if (window->role == GUI_WINDOW_ROLE_APP ||
        window->role == GUI_WINDOW_ROLE_HOME) {
        return window->lifecycle == GUI_WINDOW_LIFECYCLE_FOREGROUND;
    }
    if (window->role == GUI_WINDOW_ROLE_DIALOG ||
        window->role == GUI_WINDOW_ROLE_TRANSIENT) {
        return window->lifecycle == GUI_WINDOW_LIFECYCLE_FOREGROUND;
    }
    return true;
}

static ErrorCode update_client_interest(WindowServiceHandle service,
                                        WindowClient *client, bool writable)
{
    return eventloop_update_fd(
        service->loop, client->source,
        EVENT_READABLE | EVENT_ERROR | EVENT_HANGUP |
        (writable ? EVENT_WRITABLE : 0));
}

static ErrorCode queue_payload(WindowServiceHandle service,
                               WindowClient *client, IpcMessageType type,
                               u8 *data, usize size)
{
    IpcMessage message = { type, 0, { data, size }, IPC_NO_DESCRIPTOR, 0 };
    bool pending;
    ErrorCode result;

    if (client == NULL || client->connection == NULL) {
        return CORE_INVALID_STATE;
    }
    result = ipc_connection_send(client->connection, &message);
    if (result != CORE_OK) return result;
    result = ipc_connection_flush(client->connection, &pending);
    if (result != CORE_OK) return result;
    return update_client_interest(service, client, pending);
}

static ErrorCode send_basic(WindowServiceHandle service, WindowClient *client,
                            IpcMessageType type, u64 request_id,
                            ErrorCode error)
{
    GuiBasicResult response = { error, request_id };
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result = gui_encode_basic_result(
        &response, (Bytes){ payload, sizeof(payload) }, &size);

    if (result != CORE_OK) return result;
    return queue_payload(service, client, type, payload, size);
}

static ErrorCode send_operation(WindowServiceHandle service,
                                WindowClient *client, u64 request_id,
                                ErrorCode error)
{
    return send_basic(service, client, GUI_WINDOW_OPERATION_RESULT,
                      request_id, error);
}

static u64 next_scene_request_id(WindowServiceHandle service)
{
    u64 value = service->next_scene_request_id++;

    if (value == 0) value = service->next_scene_request_id++;
    return value;
}

static ErrorCode compositor_update_interest(WindowServiceHandle service,
                                            bool writable)
{
    if (service->compositor_connection == NULL) return CORE_OK;
    return eventloop_update_fd(
        service->loop, service->compositor_source,
        EVENT_READABLE | EVENT_ERROR | EVENT_HANGUP |
        (writable ? EVENT_WRITABLE : 0));
}

static ErrorCode compositor_queue(WindowServiceHandle service,
                                  IpcMessageType type, u8 *data, usize size)
{
    IpcMessage message = { type, 0, { data, size }, IPC_NO_DESCRIPTOR, 0 };
    bool pending;
    ErrorCode result;

    if (service->compositor_connection == NULL ||
        service->compositor_state != WINDOW_COMPOSITOR_READY) {
        return CORE_OK;
    }
    result = ipc_connection_send(service->compositor_connection, &message);
    if (result != CORE_OK) return result;
    result = ipc_connection_flush(service->compositor_connection, &pending);
    if (result != CORE_OK) return result;
    return compositor_update_interest(service, pending);
}

static ErrorCode scene_bind_window(WindowServiceHandle service,
                                   WindowRecord *window)
{
    GuiSceneBindRequest request;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    usize index;
    ErrorCode result;

    if (window->scene_bound) return CORE_OK;
    if (service->compositor_connection == NULL ||
        service->compositor_state != WINDOW_COMPOSITOR_READY) {
        return CORE_OK;
    }
    request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    request.prefix.request_id = next_scene_request_id(service);
    request.window_id = window->window_id;
    request.surface_id = window->surface_id;
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        request.surface_bind_token[index] = window->surface_bind_token[index];
    }
    result = gui_encode_scene_bind(&request,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    result = compositor_queue(service, GUI_SCENE_BIND_WINDOW, payload, size);
    if (result == CORE_OK) window->scene_bound = true;
    return result;
}

static ErrorCode scene_set_geometry(WindowServiceHandle service,
                                    WindowRecord *window)
{
    GuiSceneGeometryRequest request;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result;

    request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    request.prefix.request_id = next_scene_request_id(service);
    request.window_id = window->window_id;
    request.geometry.x = 0;
    request.geometry.y = 0;
    request.geometry.width = service->viewport_width;
    request.geometry.height = service->viewport_height;
    request.clip_x = 0;
    request.clip_y = 0;
    request.clip_width = 0;
    request.clip_height = 0;
    result = gui_encode_scene_geometry(&request,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    return compositor_queue(service, GUI_SCENE_SET_GEOMETRY, payload, size);
}

static ErrorCode scene_set_stack(WindowServiceHandle service,
                                 WindowRecord *window)
{
    GuiSceneStackRequest request;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result;

    request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    request.prefix.request_id = next_scene_request_id(service);
    request.window_id = window->window_id;
    request.layer = scene_layer_for_role(window->role);
    request.above_window_id = 0;
    result = gui_encode_scene_stack(&request,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    return compositor_queue(service, GUI_SCENE_SET_STACK, payload, size);
}

static ErrorCode scene_set_visibility(WindowServiceHandle service,
                                      WindowRecord *window)
{
    GuiSceneVisibilityRequest request;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result;

    request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    request.prefix.request_id = next_scene_request_id(service);
    request.window_id = window->window_id;
    request.visibility = window_is_visible(window) ? GUI_SCENE_VISIBLE :
                         GUI_SCENE_HIDDEN;
    result = gui_encode_scene_visibility(&request,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    return compositor_queue(service, GUI_SCENE_SET_VISIBILITY, payload, size);
}

static ErrorCode scene_remove_window(WindowServiceHandle service,
                                     WindowRecord *window)
{
    GuiSceneWindowRequest request;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result;

    if (!window->scene_bound) return CORE_OK;
    request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    request.prefix.request_id = next_scene_request_id(service);
    request.window_id = window->window_id;
    result = gui_encode_scene_window(&request,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    window->scene_bound = false;
    return compositor_queue(service, GUI_SCENE_REMOVE_WINDOW, payload, size);
}

static ErrorCode apply_scene_policy(WindowServiceHandle service,
                                    WindowRecord *window)
{
    ErrorCode result = scene_bind_window(service, window);

    if (result == CORE_OK) result = scene_set_geometry(service, window);
    if (result == CORE_OK) result = scene_set_stack(service, window);
    if (result == CORE_OK) result = scene_set_visibility(service, window);
    return result;
}

static ErrorCode send_lifecycle(WindowServiceHandle service,
                                WindowRecord *window,
                                GuiWindowLifecycleReason reason)
{
    GuiWindowLifecycleEvent event;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result;

    if (window->owner == NULL || window->owner->connection == NULL) {
        return CORE_OK;
    }
    if (window->owner->next_event_serial == 0) return CORE_OVERFLOW;
    event.serial = window->owner->next_event_serial++;
    event.window_id = window->window_id;
    event.lifecycle = window->lifecycle;
    event.reason = reason;
    result = gui_encode_window_lifecycle(
        &event, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    return queue_payload(service, window->owner,
                         GUI_WINDOW_LIFECYCLE_CHANGED, payload, size);
}

static ErrorCode send_configure(WindowServiceHandle service,
                                WindowRecord *window)
{
    GuiWindowConfigureEvent event;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result;

    if (window->owner == NULL || window->owner->connection == NULL) {
        return CORE_OK;
    }
    if (service->next_configure_serial == 0) return CORE_OVERFLOW;
    event.window_id = window->window_id;
    event.configure_serial = service->next_configure_serial++;
    event.width = service->viewport_width;
    event.height = service->viewport_height;
    event.scale = service->scale;
    event.orientation = service->orientation;
    event.safe_insets = service->safe_insets;
    result = gui_encode_window_configure(
        &event, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    window->pending_configure_serial = event.configure_serial;
    return queue_payload(service, window->owner, GUI_WINDOW_CONFIGURE,
                         payload, size);
}

static ErrorCode send_visibility(WindowServiceHandle service,
                                 WindowRecord *window)
{
    GuiWindowVisibilityEvent event;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result;

    if (window->owner == NULL || window->owner->connection == NULL) {
        return CORE_OK;
    }
    if (window->owner->next_event_serial == 0) return CORE_OVERFLOW;
    event.serial = window->owner->next_event_serial++;
    event.window_id = window->window_id;
    event.visible = window_is_visible(window);
    result = gui_encode_window_visibility(
        &event, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    return queue_payload(service, window->owner,
                         GUI_WINDOW_VISIBILITY_CHANGED, payload, size);
}

static ErrorCode publish_window_update(WindowServiceHandle service,
                                       WindowRecord *window,
                                       GuiWindowLifecycleReason reason)
{
    ErrorCode result = apply_scene_policy(service, window);

    if (result == CORE_OK) result = send_lifecycle(service, window, reason);
    if (result == CORE_OK) result = send_visibility(service, window);
    if (result == CORE_OK &&
        window->lifecycle != GUI_WINDOW_LIFECYCLE_HIDDEN &&
        window->lifecycle != GUI_WINDOW_LIFECYCLE_CLOSING) {
        result = send_configure(service, window);
    }
    return result;
}

static void clear_window(WindowRecord *window)
{
    usize index;

    window->active = false;
    window->scene_bound = false;
    window->window_id = 0;
    window->owner = NULL;
    window->surface_id = 0;
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        window->surface_bind_token[index] = 0;
    }
    window->role = 0;
    window->parent_id = 0;
    window->flags = 0;
    window->title_length = 0;
    for (index = 0; index < GUI_WINDOW_TITLE_MAX; ++index) {
        window->title[index] = 0;
    }
    window->lifecycle = 0;
    window->pending_configure_serial = 0;
    window->confirmed_configure = (GuiWindowConfigureEvent){ 0 };
}

static ErrorCode destroy_window(WindowServiceHandle service,
                                WindowRecord *window)
{
    u64 window_id = window->window_id;
    usize index;
    ErrorCode first_error;

    if (service->active_window_id == window_id) service->active_window_id = 0;
    if (service->touch_window_id == window_id) {
        service->touch_active = false;
        service->touch_id = 0;
        service->touch_window_id = 0;
    }
    first_error = scene_remove_window(service, window);
    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        if (service->windows[index].active &&
            service->windows[index].parent_id == window_id) {
            service->windows[index].parent_id = 0;
        }
    }
    clear_window(window);
    return first_error;
}

static ErrorCode cancel_active_touch(WindowServiceHandle service)
{
    WindowRecord *window;
    GuiWindowTouchEvent event;

    if (!service->touch_active) return CORE_OK;
    window = find_window(service, service->touch_window_id);
    if (window == NULL || window->owner == NULL ||
        window->owner->connection == NULL) {
        service->touch_active = false;
        service->touch_window_id = 0;
        service->touch_id = 0;
        return CORE_OK;
    }
    event.window_id = window->window_id;
    event.input_serial = service->next_configure_serial++;
    event.timestamp_ms = 0;
    event.touch_id = service->touch_id;
    event.phase = GUI_TOUCH_CANCEL;
    event.x = 0;
    event.y = 0;
    return window_service_route_touch(service, &event);
}

static ErrorCode handle_window_create(WindowServiceHandle service,
                                      WindowClient *client,
                                      const IpcMessage *message)
{
    GuiWindowCreateRequest request;
    GuiWindowCreateResult response;
    WindowRecord *window = NULL;
    WindowRecord *parent;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    usize index;
    ErrorCode result = gui_decode_window_create(message, &request);

    if (result != CORE_OK) return result;

    response.result.request_id = request.prefix.request_id;
    response.window_id = 0;
    if (role_is_shell_only(request.role) && !client->is_shell) {
        response.result.error = CORE_PERMISSION_DENIED;
    } else if (client_window_count(service, client) >=
               WINDOW_SERVICE_CLIENT_WINDOW_MAX) {
        response.result.error = CORE_OUT_OF_MEMORY;
    } else if (request.parent_id != 0 &&
               ((parent = find_window(service, request.parent_id)) == NULL ||
                parent->owner != client)) {
        response.result.error = CORE_PERMISSION_DENIED;
    } else if (service->next_window_id == 0) {
        response.result.error = CORE_OVERFLOW;
    } else {
        window = find_free_window(service);
        if (window == NULL) {
            response.result.error = CORE_OUT_OF_MEMORY;
        } else {
            window->active = true;
            window->scene_bound = false;
            window->window_id = service->next_window_id++;
            window->owner = client;
            window->surface_id = request.surface_id;
            for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
                window->surface_bind_token[index] =
                    request.surface_bind_token[index];
            }
            window->role = request.role;
            window->parent_id = request.parent_id;
            window->flags = request.flags;
            window->title_length = request.title_length;
            for (index = 0; index < request.title_length; ++index) {
                window->title[index] = request.title[index];
            }
            window->lifecycle = GUI_WINDOW_LIFECYCLE_HIDDEN;
            window->pending_configure_serial = 0;
            window->confirmed_configure = (GuiWindowConfigureEvent){ 0 };
            response.result.error = CORE_OK;
            response.window_id = window->window_id;
        }
    }

    result = gui_encode_window_create_result(
        &response, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    result = queue_payload(service, client, GUI_WINDOW_CREATE_RESULT,
                           payload, size);
    if (result == CORE_OK && window != NULL) {
        result = scene_bind_window(service, window);
    }
    return result;
}

static ErrorCode handle_window_destroy(WindowServiceHandle service,
                                       WindowClient *client,
                                       const IpcMessage *message)
{
    GuiWindowIdRequest request;
    WindowRecord *window;
    ErrorCode result = gui_decode_window_id_request(
        message, GUI_WINDOW_DESTROY, &request);

    if (result != CORE_OK) return result;
    window = find_window(service, request.window_id);
    if (window == NULL) {
        return send_basic(service, client, GUI_WINDOW_DESTROY_RESULT,
                          request.prefix.request_id, CORE_NOT_FOUND);
    }
    if (window->owner != client) {
        return send_basic(service, client, GUI_WINDOW_DESTROY_RESULT,
                          request.prefix.request_id, CORE_PERMISSION_DENIED);
    }
    result = destroy_window(service, window);
    if (result != CORE_OK) return result;
    return send_basic(service, client, GUI_WINDOW_DESTROY_RESULT,
                      request.prefix.request_id, CORE_OK);
}

static ErrorCode handle_window_show(WindowServiceHandle service,
                                    WindowClient *client,
                                    const IpcMessage *message)
{
    GuiWindowIdRequest request;
    WindowRecord *window;
    ErrorCode result = gui_decode_window_id_request(
        message, GUI_WINDOW_SHOW, &request);

    if (result != CORE_OK) return result;
    window = find_window(service, request.window_id);
    if (window == NULL) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_NOT_FOUND);
    }
    if (window->owner != client) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_PERMISSION_DENIED);
    }
    if (window->lifecycle == GUI_WINDOW_LIFECYCLE_CLOSING) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_INVALID_STATE);
    }
    if (window->lifecycle == GUI_WINDOW_LIFECYCLE_HIDDEN) {
        window->lifecycle = role_can_activate(window->role) ?
            GUI_WINDOW_LIFECYCLE_BACKGROUND : GUI_WINDOW_LIFECYCLE_FOREGROUND;
    }
    result = send_operation(service, client, request.prefix.request_id,
                            CORE_OK);
    if (result == CORE_OK) {
        result = publish_window_update(service, window,
                                       GUI_WINDOW_REASON_CLIENT_REQUEST);
    }
    return result;
}

static ErrorCode handle_window_hide(WindowServiceHandle service,
                                    WindowClient *client,
                                    const IpcMessage *message)
{
    GuiWindowIdRequest request;
    WindowRecord *window;
    ErrorCode result = gui_decode_window_id_request(
        message, GUI_WINDOW_HIDE, &request);

    if (result != CORE_OK) return result;
    window = find_window(service, request.window_id);
    if (window == NULL) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_NOT_FOUND);
    }
    if (window->owner != client) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_PERMISSION_DENIED);
    }
    window->lifecycle = GUI_WINDOW_LIFECYCLE_HIDDEN;
    if (service->active_window_id == window->window_id) {
        service->active_window_id = 0;
    }
    result = send_operation(service, client, request.prefix.request_id,
                            CORE_OK);
    if (result == CORE_OK) {
        result = publish_window_update(service, window,
                                       GUI_WINDOW_REASON_CLIENT_REQUEST);
    }
    return result;
}

static ErrorCode handle_set_title(WindowServiceHandle service,
                                  WindowClient *client,
                                  const IpcMessage *message)
{
    GuiWindowSetTitleRequest request;
    WindowRecord *window;
    usize index;
    ErrorCode result = gui_decode_window_set_title(message, &request);

    if (result != CORE_OK) return result;
    window = find_window(service, request.window_id);
    if (window == NULL) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_NOT_FOUND);
    }
    if (window->owner != client) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_PERMISSION_DENIED);
    }
    window->title_length = request.title_length;
    for (index = 0; index < request.title_length; ++index) {
        window->title[index] = request.title[index];
    }
    return send_operation(service, client, request.prefix.request_id, CORE_OK);
}

static ErrorCode handle_ack_configure(WindowServiceHandle service,
                                      WindowClient *client,
                                      const IpcMessage *message)
{
    GuiWindowAckConfigureRequest request;
    WindowRecord *window;
    ErrorCode result = gui_decode_window_ack_configure(message, &request);

    if (result != CORE_OK) return result;
    window = find_window(service, request.window_id);
    if (window == NULL) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_NOT_FOUND);
    }
    if (window->owner != client) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_PERMISSION_DENIED);
    }
    if (window->pending_configure_serial != request.configure_serial) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_INVALID_STATE);
    }
    window->confirmed_configure.window_id = window->window_id;
    window->confirmed_configure.configure_serial = request.configure_serial;
    window->confirmed_configure.width = service->viewport_width;
    window->confirmed_configure.height = service->viewport_height;
    window->confirmed_configure.scale = service->scale;
    window->confirmed_configure.orientation = service->orientation;
    window->confirmed_configure.safe_insets = service->safe_insets;
    return send_operation(service, client, request.prefix.request_id, CORE_OK);
}

static ErrorCode handle_set_modal(WindowServiceHandle service,
                                  WindowClient *client,
                                  const IpcMessage *message)
{
    GuiWindowModalRequest request;
    WindowRecord *window;
    WindowRecord *parent;
    ErrorCode result = gui_decode_window_modal(message, &request);

    if (result != CORE_OK) return result;
    window = find_window(service, request.window_id);
    if (window == NULL) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_NOT_FOUND);
    }
    if (window->owner != client) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_PERMISSION_DENIED);
    }
    if (request.parent_id != 0) {
        parent = find_window(service, request.parent_id);
        if (parent == NULL || parent->owner != client) {
            return send_operation(service, client, request.prefix.request_id,
                                  CORE_PERMISSION_DENIED);
        }
    }
    window->parent_id = request.parent_id;
    return send_operation(service, client, request.prefix.request_id, CORE_OK);
}

static ErrorCode handle_surface_rebind(WindowServiceHandle service,
                                       WindowClient *client,
                                       const IpcMessage *message)
{
    GuiWindowSurfaceRebindRequest request;
    WindowRecord *window;
    usize index;
    ErrorCode result = gui_decode_window_surface_rebind(message, &request);

    if (result != CORE_OK) return result;
    window = find_window(service, request.window_id);
    if (window == NULL) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_NOT_FOUND);
    }
    if (window->owner != client) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_PERMISSION_DENIED);
    }
    (void)scene_remove_window(service, window);
    window->surface_id = request.surface_id;
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        window->surface_bind_token[index] = request.surface_bind_token[index];
    }
    result = scene_bind_window(service, window);
    if (result != CORE_OK) return result;
    return send_operation(service, client, request.prefix.request_id, CORE_OK);
}

static ErrorCode handle_shell_bind(WindowServiceHandle service,
                                   WindowClient *client,
                                   const IpcMessage *message)
{
    GuiShellBindRequest request;
    ErrorCode error = CORE_OK;
    ErrorCode result = gui_decode_shell_bind(message, &request);

    if (result != CORE_OK) return result;
    if (!token_equal(request.shell_token, service->shell_token,
                     WINDOW_SERVICE_SHELL_TOKEN_SIZE)) {
        error = CORE_PERMISSION_DENIED;
    } else if (service->shell_client != NULL && service->shell_client != client) {
        error = CORE_ALREADY_EXISTS;
    } else {
        client->is_shell = true;
        service->shell_client = client;
    }
    return send_basic(service, client, GUI_WINDOW_BIND_SHELL_RESULT,
                      request.prefix.request_id, error);
}

static ErrorCode activate_window(WindowServiceHandle service,
                                 WindowRecord *target)
{
    WindowRecord *previous;
    ErrorCode result = cancel_active_touch(service);

    if (result != CORE_OK) return result;
    previous = find_window(service, service->active_window_id);
    if (previous != NULL && previous != target &&
        previous->lifecycle == GUI_WINDOW_LIFECYCLE_FOREGROUND &&
        role_can_activate(previous->role)) {
        previous->lifecycle = GUI_WINDOW_LIFECYCLE_BACKGROUND;
        result = publish_window_update(service, previous,
                                       GUI_WINDOW_REASON_SHELL_ACTIVATION);
        if (result != CORE_OK) return result;
    }
    target->lifecycle = GUI_WINDOW_LIFECYCLE_FOREGROUND;
    service->active_window_id = target->window_id;
    return publish_window_update(service, target,
                                 GUI_WINDOW_REASON_SHELL_ACTIVATION);
}

static ErrorCode handle_shell_activate(WindowServiceHandle service,
                                       WindowClient *client,
                                       const IpcMessage *message)
{
    GuiShellActivateRequest request;
    WindowRecord *window;
    ErrorCode result = gui_decode_shell_activate(message, &request);

    if (result != CORE_OK) return result;
    if (!client->is_shell) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_PERMISSION_DENIED);
    }
    window = find_window(service, request.window_id);
    if (window == NULL) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_NOT_FOUND);
    }
    if (!role_can_activate(window->role) ||
        window->lifecycle == GUI_WINDOW_LIFECYCLE_HIDDEN ||
        window->lifecycle == GUI_WINDOW_LIFECYCLE_CLOSING) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_INVALID_STATE);
    }
    result = activate_window(service, window);
    if (result != CORE_OK) return result;
    return send_operation(service, client, request.prefix.request_id, CORE_OK);
}

static ErrorCode send_configure_to_live_windows(WindowServiceHandle service)
{
    usize index;
    ErrorCode first_error = CORE_OK;

    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        WindowRecord *window = &service->windows[index];
        if (!window->active ||
            window->lifecycle == GUI_WINDOW_LIFECYCLE_HIDDEN ||
            window->lifecycle == GUI_WINDOW_LIFECYCLE_CLOSING) {
            continue;
        }
        {
            ErrorCode result = send_configure(service, window);
            if (result != CORE_OK && first_error == CORE_OK) {
                first_error = result;
            }
        }
    }
    return first_error;
}

static ErrorCode handle_shell_insets(WindowServiceHandle service,
                                     WindowClient *client,
                                     const IpcMessage *message)
{
    GuiShellInsetsRequest request;
    ErrorCode result = gui_decode_shell_insets(message, &request);

    if (result != CORE_OK) return result;
    if (!client->is_shell) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_PERMISSION_DENIED);
    }
    service->safe_insets = request.insets;
    result = send_operation(service, client, request.prefix.request_id,
                            CORE_OK);
    if (result == CORE_OK) result = send_configure_to_live_windows(service);
    return result;
}

static ErrorCode handle_shell_navigate(WindowServiceHandle service,
                                       WindowClient *client,
                                       const IpcMessage *message)
{
    GuiShellNavigateRequest request;
    WindowRecord *target;
    GuiWindowNavigationEvent event;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result = gui_decode_shell_navigate(message, &request);

    if (result != CORE_OK) return result;
    if (!client->is_shell) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_PERMISSION_DENIED);
    }
    target = find_window(service, request.target_id != 0 ?
                         request.target_id : service->active_window_id);
    if (target == NULL) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_NOT_FOUND);
    }
    result = send_operation(service, client, request.prefix.request_id,
                            CORE_OK);
    if (result != CORE_OK) return result;
    if (target->owner == NULL || target->owner->connection == NULL) {
        return CORE_OK;
    }
    if (target->owner->next_event_serial == 0) return CORE_OVERFLOW;
    event.serial = target->owner->next_event_serial++;
    event.window_id = target->window_id;
    event.action = request.action;
    result = gui_encode_window_navigation(&event,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    return queue_payload(service, target->owner,
                         GUI_WINDOW_NAVIGATION_REQUESTED, payload, size);
}

static ErrorCode handle_close_response(WindowServiceHandle service,
                                       WindowClient *client,
                                       const IpcMessage *message)
{
    GuiWindowCloseResponseRequest request;
    WindowRecord *window;
    ErrorCode result = gui_decode_window_close_response(message, &request);

    if (result != CORE_OK) return result;
    window = find_window(service, request.window_id);
    if (window == NULL) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_NOT_FOUND);
    }
    if (window->owner != client) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_PERMISSION_DENIED);
    }
    (void)request.close_serial;
    (void)request.response;
    return send_operation(service, client, request.prefix.request_id, CORE_OK);
}

static ErrorCode handle_client_message(WindowServiceHandle service,
                                       WindowClient *client,
                                       IpcMessage *message)
{
    if (message->type == GUI_WINDOW_CREATE) {
        return handle_window_create(service, client, message);
    }
    if (message->type == GUI_WINDOW_DESTROY) {
        return handle_window_destroy(service, client, message);
    }
    if (message->type == GUI_WINDOW_SHOW) {
        return handle_window_show(service, client, message);
    }
    if (message->type == GUI_WINDOW_HIDE) {
        return handle_window_hide(service, client, message);
    }
    if (message->type == GUI_WINDOW_SET_TITLE) {
        return handle_set_title(service, client, message);
    }
    if (message->type == GUI_WINDOW_ACK_CONFIGURE) {
        return handle_ack_configure(service, client, message);
    }
    if (message->type == GUI_WINDOW_SET_MODAL) {
        return handle_set_modal(service, client, message);
    }
    if (message->type == GUI_WINDOW_SURFACE_REBIND) {
        return handle_surface_rebind(service, client, message);
    }
    if (message->type == GUI_WINDOW_BIND_SHELL) {
        return handle_shell_bind(service, client, message);
    }
    if (message->type == GUI_SHELL_ACTIVATE) {
        return handle_shell_activate(service, client, message);
    }
    if (message->type == GUI_SHELL_SET_INSETS) {
        return handle_shell_insets(service, client, message);
    }
    if (message->type == GUI_SHELL_NAVIGATE) {
        return handle_shell_navigate(service, client, message);
    }
    if (message->type == GUI_WINDOW_RESPOND_CLOSE) {
        return handle_close_response(service, client, message);
    }
    return CORE_INVALID_ARGUMENT;
}

static ErrorCode compositor_send_bind(WindowServiceHandle service)
{
    GuiControllerBindRequest request;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    usize index;
    IpcMessage message;
    bool pending;
    ErrorCode result;

    if (service->compositor_connection == NULL) return CORE_INVALID_STATE;
    request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    request.prefix.request_id = next_scene_request_id(service);
    for (index = 0; index < WINDOW_SERVICE_CONTROLLER_TOKEN_SIZE; ++index) {
        request.controller_token[index] = service->controller_token[index];
    }
    result = gui_encode_controller_bind(&request,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    message.type = GUI_COMPOSITOR_BIND_CONTROLLER;
    message.reserved = 0;
    message.payload = (Bytes){ payload, size };
    message.descriptor = IPC_NO_DESCRIPTOR;
    message.descriptor_reserved = 0;
    result = ipc_connection_send(service->compositor_connection, &message);
    if (result != CORE_OK) return result;
    result = ipc_connection_flush(service->compositor_connection, &pending);
    if (result != CORE_OK) return result;
    service->compositor_state = WINDOW_COMPOSITOR_BINDING;
    return compositor_update_interest(service, pending);
}

static void compositor_suspend(WindowServiceHandle service)
{
    usize index;

    if (service->compositor_connection != NULL) {
        (void)eventloop_remove_source(service->loop,
                                      service->compositor_source);
        (void)ipc_connection_close(&service->compositor_connection);
    }
    service->compositor_source = 0;
    service->compositor_state = WINDOW_COMPOSITOR_SUSPENDED;
    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        service->windows[index].scene_bound = false;
    }
}

static ErrorCode compositor_handle_message(WindowServiceHandle service,
                                           IpcMessage *message)
{
    if (message->type == GUI_COMPOSITOR_BIND_CONTROLLER_RESULT) {
        GuiBasicResult result;
        ErrorCode code = gui_decode_basic_result(
            message, GUI_COMPOSITOR_BIND_CONTROLLER_RESULT, &result);
        usize index;
        if (code != CORE_OK) return code;
        service->compositor_state = result.error == CORE_OK ?
            WINDOW_COMPOSITOR_READY : WINDOW_COMPOSITOR_SUSPENDED;
        if (service->compositor_state == WINDOW_COMPOSITOR_READY) {
            for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
                if (service->windows[index].active) {
                    code = apply_scene_policy(service, &service->windows[index]);
                    if (code != CORE_OK) return code;
                }
            }
        }
        return CORE_OK;
    }
    if (message->type == GUI_SCENE_RESULT) {
        GuiBasicResult result;
        return gui_decode_basic_result(message, GUI_SCENE_RESULT, &result);
    }
    if (message->type == GUI_SCENE_BINDING_LOST) {
        GuiSceneBindingLostEvent event;
        WindowRecord *window;
        ErrorCode result = gui_decode_scene_binding_lost(message, &event);
        if (result != CORE_OK) return result;
        window = find_window(service, event.window_id);
        if (window != NULL && window->surface_id == event.surface_id) {
            window->scene_bound = false;
            window->lifecycle = GUI_WINDOW_LIFECYCLE_SUSPENDED;
            (void)send_lifecycle(service, window,
                                  GUI_WINDOW_REASON_DISPLAY_LOSS);
        }
        return CORE_OK;
    }
    return CORE_INVALID_ARGUMENT;
}

static ErrorCode handle_compositor_event(WindowServiceHandle service,
                                         EventMask mask)
{
    if ((mask & (EVENT_ERROR | EVENT_HANGUP)) != 0) {
        compositor_suspend(service);
        return CORE_OK;
    }
    if ((mask & EVENT_WRITABLE) != 0) {
        if (service->compositor_state == WINDOW_COMPOSITOR_CONNECTING) {
            IpcConnectionState state;
            ErrorCode result = ipc_connection_finish_connect(
                service->compositor_connection, &state);
            if (result != CORE_OK) {
                compositor_suspend(service);
                return CORE_OK;
            }
            if (state == IPC_CONNECTION_OPEN) {
                result = compositor_send_bind(service);
                if (result != CORE_OK) compositor_suspend(service);
            }
        } else {
            bool pending;
            ErrorCode result = ipc_connection_flush(
                service->compositor_connection, &pending);
            if (result != CORE_OK) {
                compositor_suspend(service);
                return CORE_OK;
            }
            (void)compositor_update_interest(service, pending);
        }
    }
    if ((mask & EVENT_READABLE) != 0 &&
        service->compositor_connection != NULL) {
        while (true) {
            IpcReceiveStatus status;
            IpcMessage message;
            ErrorCode result = ipc_connection_receive(
                service->compositor_connection, &status, &message);
            if (result != CORE_OK) {
                compositor_suspend(service);
                return CORE_OK;
            }
            if (status == IPC_RECEIVE_NO_MESSAGE) break;
            if (status == IPC_RECEIVE_PEER_CLOSED) {
                compositor_suspend(service);
                break;
            }
            result = compositor_handle_message(service, &message);
            (void)ipc_message_destroy(&message);
            if (result != CORE_OK) {
                compositor_suspend(service);
                return CORE_OK;
            }
        }
    }
    return CORE_OK;
}

static ErrorCode compositor_begin_connect(WindowServiceHandle service,
                                          const String *endpoint)
{
    IpcConnectionHandle connection = NULL;
    i32 fd;
    ErrorCode result = ipc_connection_begin_connect(endpoint, &connection);

    if (result != CORE_OK) return result;
    result = ipc_connection_get_fd(connection, &fd);
    if (result != CORE_OK) goto fail;
    result = eventloop_add_fd(service->loop, fd,
        EVENT_READABLE | EVENT_WRITABLE | EVENT_ERROR | EVENT_HANGUP,
        &service->compositor_source);
    if (result != CORE_OK) goto fail;
    service->compositor_connection = connection;
    service->compositor_state = WINDOW_COMPOSITOR_CONNECTING;
    return CORE_OK;

fail:
    (void)ipc_connection_close(&connection);
    return result;
}

static ErrorCode io_update_interest(WindowServiceHandle service, bool writable)
{
    if (service->io_connection == NULL) return CORE_OK;
    return eventloop_update_fd(
        service->loop, service->io_source,
        EVENT_READABLE | EVENT_ERROR | EVENT_HANGUP |
        (writable ? EVENT_WRITABLE : 0));
}

static ErrorCode io_send_subscribe(WindowServiceHandle service)
{
    GuiIoSubscribeRequest request;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    IpcMessage message;
    bool pending;
    ErrorCode result;

    if (service->io_connection == NULL) return CORE_INVALID_STATE;
    request.protocol_version = GUI_PROTOCOL_VERSION;
    request.device_class_mask = GUI_INPUT_DEVICE_KEYBOARD |
                                GUI_INPUT_DEVICE_POINTER |
                                GUI_INPUT_DEVICE_TOUCH;
    result = gui_encode_io_subscribe(&request,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    message.type = GUI_IO_SUBSCRIBE;
    message.reserved = 0;
    message.payload = (Bytes){ payload, size };
    message.descriptor = IPC_NO_DESCRIPTOR;
    message.descriptor_reserved = 0;
    result = ipc_connection_send(service->io_connection, &message);
    if (result != CORE_OK) return result;
    result = ipc_connection_flush(service->io_connection, &pending);
    if (result != CORE_OK) return result;
    service->io_state = WINDOW_IO_SUBSCRIBING;
    return io_update_interest(service, pending);
}

static void io_suspend(WindowServiceHandle service)
{
    if (service->io_connection != NULL) {
        (void)eventloop_remove_source(service->loop, service->io_source);
        (void)ipc_connection_close(&service->io_connection);
    }
    service->io_source = 0;
    service->io_state = WINDOW_IO_SUSPENDED;
    (void)cancel_active_touch(service);
    service->io_touch_down = false;
}

static u64 next_input_serial(WindowServiceHandle service)
{
    u64 value = service->next_input_serial++;

    if (value == 0) value = service->next_input_serial++;
    return value;
}

static bool key_is_shell_reserved(u16 code)
{
    return code == LINUX_KEY_HOME || code == LINUX_KEY_POWER ||
           code == LINUX_KEY_VOLUMEUP || code == LINUX_KEY_VOLUMEDOWN;
}

static ErrorCode route_io_key(WindowServiceHandle service, u16 code, i32 value)
{
    GuiWindowKeyEvent event;

    if (key_is_shell_reserved(code)) return CORE_OK;
    if (value != 0 && value != 1 && value != 2) return CORE_OK;
    event.window_id = 0;
    event.input_serial = next_input_serial(service);
    event.timestamp_ms = 0;
    event.key_code = code;
    event.phase = value == 0 ? GUI_KEY_UP :
                  value == 2 ? GUI_KEY_REPEAT : GUI_KEY_DOWN;
    event.modifiers = 0;
    return window_service_route_key(service, &event);
}

static ErrorCode route_io_touch_contact(WindowServiceHandle service,
                                        bool down)
{
    GuiWindowTouchEvent event;

    if (down == service->io_touch_down) return CORE_OK;
    event.window_id = 0;
    event.input_serial = next_input_serial(service);
    event.timestamp_ms = 0;
    event.touch_id = 0;
    event.phase = down ? GUI_TOUCH_DOWN : GUI_TOUCH_UP;
    event.x = (i32)(service->io_touch_x << 8);
    event.y = (i32)(service->io_touch_y << 8);
    service->io_touch_down = down;
    return window_service_route_touch(service, &event);
}

static ErrorCode route_io_touch_move(WindowServiceHandle service)
{
    GuiWindowTouchEvent event;

    if (!service->io_touch_down) return CORE_OK;
    event.window_id = 0;
    event.input_serial = next_input_serial(service);
    event.timestamp_ms = 0;
    event.touch_id = 0;
    event.phase = GUI_TOUCH_MOVE;
    event.x = (i32)(service->io_touch_x << 8);
    event.y = (i32)(service->io_touch_y << 8);
    return window_service_route_touch(service, &event);
}

static ErrorCode route_io_pointer_button(WindowServiceHandle service,
                                         bool down)
{
    GuiWindowPointerEvent event;

    event.window_id = 0;
    event.input_serial = next_input_serial(service);
    event.timestamp_ms = 0;
    event.pointer_id = 0;
    event.phase = down ? GUI_POINTER_DOWN : GUI_POINTER_UP;
    event.buttons = down ? 1 : 0;
    event.modifiers = 0;
    event.x = service->io_pointer_x << 8;
    event.y = service->io_pointer_y << 8;
    return window_service_route_pointer(service, &event);
}

static ErrorCode route_io_pointer_move(WindowServiceHandle service,
                                       i32 dx, i32 dy)
{
    GuiWindowPointerEvent event;

    service->io_pointer_x += dx;
    service->io_pointer_y += dy;
    if (service->io_pointer_x < 0) service->io_pointer_x = 0;
    if (service->io_pointer_y < 0) service->io_pointer_y = 0;
    event.window_id = 0;
    event.input_serial = next_input_serial(service);
    event.timestamp_ms = 0;
    event.pointer_id = 0;
    event.phase = GUI_POINTER_MOVE;
    event.buttons = 0;
    event.modifiers = 0;
    event.x = service->io_pointer_x << 8;
    event.y = service->io_pointer_y << 8;
    return window_service_route_pointer(service, &event);
}

static ErrorCode handle_io_frame(WindowServiceHandle service,
                                 const GuiIoInputFrameEvent *frame)
{
    usize index;
    bool touch_moved = false;
    ErrorCode first_error = CORE_OK;

    for (index = 0; index < frame->event_count; ++index) {
        const GuiIoInputEvent *event = &frame->events[index];
        ErrorCode result = CORE_OK;

        if (event->kind == GUI_IO_INPUT_KEY) {
            if (event->code == LINUX_BTN_TOUCH) {
                result = route_io_touch_contact(service, event->value != 0);
            } else if (event->code == LINUX_BTN_LEFT) {
                result = route_io_pointer_button(service, event->value != 0);
            } else {
                result = route_io_key(service, event->code, event->value);
            }
        } else if (event->kind == GUI_IO_INPUT_ABSOLUTE_AXIS) {
            if (event->code == LINUX_ABS_X ||
                event->code == LINUX_ABS_MT_POSITION_X) {
                service->io_touch_x = (u32)event->value;
                touch_moved = true;
            } else if (event->code == LINUX_ABS_Y ||
                       event->code == LINUX_ABS_MT_POSITION_Y) {
                service->io_touch_y = (u32)event->value;
                touch_moved = true;
            }
        } else if (event->kind == GUI_IO_INPUT_RELATIVE_AXIS) {
            if (event->code == LINUX_REL_X) {
                result = route_io_pointer_move(service, event->value, 0);
            } else if (event->code == LINUX_REL_Y) {
                result = route_io_pointer_move(service, 0, event->value);
            }
        } else if (event->kind == GUI_IO_INPUT_SYNC_LOST) {
            result = cancel_active_touch(service);
            service->io_touch_down = false;
        }
        if (result != CORE_OK && result != CORE_NOT_FOUND &&
            result != CORE_PERMISSION_DENIED && first_error == CORE_OK) {
            first_error = result;
        }
    }
    if (touch_moved && service->io_touch_down) {
        ErrorCode result = route_io_touch_move(service);
        if (result != CORE_OK && result != CORE_NOT_FOUND &&
            result != CORE_PERMISSION_DENIED && first_error == CORE_OK) {
            first_error = result;
        }
    }
    return first_error;
}

static ErrorCode io_handle_message(WindowServiceHandle service,
                                   IpcMessage *message)
{
    if (message->type == GUI_IO_SUBSCRIBE_RESULT) {
        GuiIoResult result;
        ErrorCode code = gui_decode_io_result(
            message, GUI_IO_SUBSCRIBE_RESULT, &result);
        if (code != CORE_OK) return code;
        service->io_state = result.error == CORE_OK ?
            WINDOW_IO_READY : WINDOW_IO_SUSPENDED;
        return CORE_OK;
    }
    if (message->type == GUI_IO_INPUT_FRAME) {
        GuiIoInputFrameEvent frame;
        ErrorCode result = gui_decode_io_input_frame(message, &frame);
        if (result != CORE_OK) return result;
        return handle_io_frame(service, &frame);
    }
    if (message->type == GUI_IO_DEVICE_ADDED ||
        message->type == GUI_IO_DEVICE_REMOVED) {
        GuiIoDeviceEvent event;
        return gui_decode_io_device_event(message, message->type, &event);
    }
    return CORE_INVALID_ARGUMENT;
}

static ErrorCode handle_io_event(WindowServiceHandle service, EventMask mask)
{
    if ((mask & (EVENT_ERROR | EVENT_HANGUP)) != 0) {
        io_suspend(service);
        return CORE_OK;
    }
    if ((mask & EVENT_WRITABLE) != 0) {
        if (service->io_state == WINDOW_IO_CONNECTING) {
            IpcConnectionState state;
            ErrorCode result = ipc_connection_finish_connect(
                service->io_connection, &state);
            if (result != CORE_OK) {
                io_suspend(service);
                return CORE_OK;
            }
            if (state == IPC_CONNECTION_OPEN) {
                result = io_send_subscribe(service);
                if (result != CORE_OK) io_suspend(service);
            }
        } else {
            bool pending;
            ErrorCode result = ipc_connection_flush(service->io_connection,
                                                     &pending);
            if (result != CORE_OK) {
                io_suspend(service);
                return CORE_OK;
            }
            (void)io_update_interest(service, pending);
        }
    }
    if ((mask & EVENT_READABLE) != 0 && service->io_connection != NULL) {
        while (true) {
            IpcReceiveStatus status;
            IpcMessage message;
            ErrorCode result = ipc_connection_receive(service->io_connection,
                                                       &status, &message);
            if (result != CORE_OK) {
                io_suspend(service);
                return CORE_OK;
            }
            if (status == IPC_RECEIVE_NO_MESSAGE) break;
            if (status == IPC_RECEIVE_PEER_CLOSED) {
                io_suspend(service);
                break;
            }
            result = io_handle_message(service, &message);
            (void)ipc_message_destroy(&message);
            if (result != CORE_OK) {
                io_suspend(service);
                return CORE_OK;
            }
        }
    }
    return CORE_OK;
}

static ErrorCode io_begin_connect(WindowServiceHandle service,
                                  const String *endpoint)
{
    IpcConnectionHandle connection = NULL;
    i32 fd;
    ErrorCode result = ipc_connection_begin_connect(endpoint, &connection);

    if (result != CORE_OK) return result;
    result = ipc_connection_get_fd(connection, &fd);
    if (result != CORE_OK) goto fail;
    result = eventloop_add_fd(service->loop, fd,
        EVENT_READABLE | EVENT_WRITABLE | EVENT_ERROR | EVENT_HANGUP,
        &service->io_source);
    if (result != CORE_OK) goto fail;
    service->io_connection = connection;
    service->io_state = WINDOW_IO_CONNECTING;
    return CORE_OK;

fail:
    (void)ipc_connection_close(&connection);
    return result;
}

static ErrorCode accept_clients(WindowServiceHandle service)
{
    while (true) {
        IpcConnectionHandle connection = NULL;
        WindowClient *client;
        i32 fd;
        ErrorCode result = ipc_server_accept(service->server, &connection);

        if (result == CORE_WOULD_BLOCK) return CORE_OK;
        if (result != CORE_OK) return result;
        client = find_free_client(service);
        if (client == NULL) {
            (void)ipc_connection_close(&connection);
            continue;
        }
        result = ipc_connection_get_fd(connection, &fd);
        if (result != CORE_OK) {
            (void)ipc_connection_close(&connection);
            return result;
        }
        result = eventloop_add_fd(service->loop, fd,
            EVENT_READABLE | EVENT_ERROR | EVENT_HANGUP, &client->source);
        if (result != CORE_OK) {
            (void)ipc_connection_close(&connection);
            return result;
        }
        client->connection = connection;
        client->is_shell = false;
        client->next_event_serial = 1;
    }
}

static ErrorCode handle_client_event(WindowServiceHandle service,
                                     WindowClient *client, EventMask mask)
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
        (void)update_client_interest(service, client, pending);
    }
    if ((mask & EVENT_READABLE) != 0 && client->connection != NULL) {
        while (true) {
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
            result = handle_client_message(service, client, &message);
            (void)ipc_message_destroy(&message);
            if (result != CORE_OK) {
                remove_client(service, client);
                return CORE_OK;
            }
        }
    }
    return CORE_OK;
}

static void remove_client(WindowServiceHandle service, WindowClient *client)
{
    usize index;

    if (client == NULL || client->connection == NULL) return;
    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        if (service->windows[index].active &&
            service->windows[index].owner == client) {
            (void)destroy_window(service, &service->windows[index]);
        }
    }
    if (service->shell_client == client) service->shell_client = NULL;
    (void)eventloop_remove_source(service->loop, client->source);
    (void)ipc_connection_close(&client->connection);
    client->source = 0;
    client->is_shell = false;
    client->next_event_serial = 0;
}

ErrorCode window_service_create(
    const String *endpoint,
    const u8 shell_token[WINDOW_SERVICE_SHELL_TOKEN_SIZE],
    WindowServiceHandle *out_service)
{
    WindowServiceConfig config;

    config.endpoint = endpoint;
    config.compositor_endpoint = NULL;
    config.io_endpoint = NULL;
    config.controller_token = NULL;
    config.shell_token = shell_token;
    config.viewport_width = 1080;
    config.viewport_height = 1920;
    config.scale = GUI_SCALE_1X;
    config.orientation = GUI_ORIENTATION_NORMAL;
    config.safe_insets = (GuiInsets){ 0, 0, 0, 0 };
    return window_service_create_config(&config, out_service);
}

ErrorCode window_service_create_config(
    const WindowServiceConfig *config,
    WindowServiceHandle *out_service)
{
    MemoryBlock block;
    WindowServiceHandle service;
    i32 fd;
    usize index;
    ErrorCode result;

    if (config == NULL || config->endpoint == NULL ||
        config->shell_token == NULL || out_service == NULL ||
        config->orientation > GUI_ORIENTATION_LEFT) {
        return CORE_INVALID_ARGUMENT;
    }
    result = memory_allocate(sizeof(struct WindowServiceHandle), &block);
    if (result != CORE_OK) return result;
    service = (WindowServiceHandle)block.data;
    service->handle_block = block;
    service->next_window_id = 1;
    service->next_configure_serial = 1;
    service->next_scene_request_id = 1;
    service->next_input_serial = 1;
    service->viewport_width = config->viewport_width != 0 ?
        config->viewport_width : 1080;
    service->viewport_height = config->viewport_height != 0 ?
        config->viewport_height : 1920;
    service->scale = config->scale != 0 ? config->scale : GUI_SCALE_1X;
    service->orientation = config->orientation;
    service->safe_insets = config->safe_insets;
    for (index = 0; index < WINDOW_SERVICE_SHELL_TOKEN_SIZE; ++index) {
        service->shell_token[index] = config->shell_token[index];
    }
    if (config->controller_token != NULL) {
        for (index = 0; index < WINDOW_SERVICE_CONTROLLER_TOKEN_SIZE; ++index) {
            service->controller_token[index] = config->controller_token[index];
        }
    }

    result = eventloop_create(&service->loop);
    if (result != CORE_OK) goto fail;
    result = ipc_server_create(config->endpoint, &service->server);
    if (result != CORE_OK) goto fail;
    result = ipc_server_get_fd(service->server, &fd);
    if (result != CORE_OK) goto fail;
    result = eventloop_add_fd(service->loop, fd,
        EVENT_READABLE | EVENT_ERROR | EVENT_HANGUP,
        &service->server_source);
    if (result != CORE_OK) goto fail;
    service->compositor_state = WINDOW_COMPOSITOR_DISABLED;
    if (config->compositor_endpoint != NULL &&
        config->controller_token != NULL) {
        result = compositor_begin_connect(service, config->compositor_endpoint);
        if (result != CORE_OK) goto fail;
    }
    service->io_state = WINDOW_IO_DISABLED;
    if (config->io_endpoint != NULL) {
        result = io_begin_connect(service, config->io_endpoint);
        if (result != CORE_OK) goto fail;
    }

    *out_service = service;
    return CORE_OK;

fail:
    (void)window_service_close(&service);
    return result;
}

ErrorCode window_service_poll(WindowServiceHandle service,
                              DurationMs timeout_ms)
{
    Event events[WINDOW_SERVICE_EVENT_MAX];
    usize count;
    usize index;
    ErrorCode result;

    if (service == NULL) return CORE_INVALID_ARGUMENT;
    result = eventloop_wait(service->loop, events, WINDOW_SERVICE_EVENT_MAX,
                            timeout_ms, &count);
    if (result != CORE_OK) return result;
    for (index = 0; index < count; ++index) {
        WindowClient *client;
        if (events[index].source_id == service->server_source) {
            result = accept_clients(service);
        } else if (events[index].source_id == service->compositor_source &&
                   service->compositor_source != 0) {
            result = handle_compositor_event(service, events[index].mask);
        } else if (events[index].source_id == service->io_source &&
                   service->io_source != 0) {
            result = handle_io_event(service, events[index].mask);
        } else {
            client = find_client(service, events[index].source_id);
            result = client == NULL ? CORE_NOT_FOUND :
                handle_client_event(service, client, events[index].mask);
        }
        if (result != CORE_OK) return result;
    }
    return CORE_OK;
}

ErrorCode window_service_close(WindowServiceHandle *service)
{
    WindowServiceHandle handle;
    usize index;
    MemoryBlock block;

    if (service == NULL || *service == NULL) return CORE_INVALID_ARGUMENT;
    handle = *service;
    for (index = 0; index < WINDOW_SERVICE_CLIENT_MAX; ++index) {
        remove_client(handle, &handle->clients[index]);
    }
    if (handle->compositor_connection != NULL) {
        (void)eventloop_remove_source(handle->loop, handle->compositor_source);
        (void)ipc_connection_close(&handle->compositor_connection);
    }
    if (handle->io_connection != NULL) {
        (void)eventloop_remove_source(handle->loop, handle->io_source);
        (void)ipc_connection_close(&handle->io_connection);
    }
    if (handle->server != NULL) {
        (void)eventloop_remove_source(handle->loop, handle->server_source);
        (void)ipc_server_close(&handle->server);
    }
    if (handle->loop != NULL) {
        (void)eventloop_close(&handle->loop);
    }
    block = handle->handle_block;
    *service = NULL;
    return memory_free(&block);
}

ErrorCode window_service_set_display_metrics(
    WindowServiceHandle service, u32 width, u32 height, u32 scale,
    GuiDisplayOrientation orientation, GuiInsets safe_insets)
{
    usize index;
    ErrorCode first_error = CORE_OK;

    if (service == NULL || width == 0 || height == 0 || scale == 0 ||
        orientation > GUI_ORIENTATION_LEFT) {
        return CORE_INVALID_ARGUMENT;
    }
    service->viewport_width = width;
    service->viewport_height = height;
    service->scale = scale;
    service->orientation = orientation;
    service->safe_insets = safe_insets;
    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        WindowRecord *window = &service->windows[index];
        if (!window->active ||
            window->lifecycle == GUI_WINDOW_LIFECYCLE_HIDDEN ||
            window->lifecycle == GUI_WINDOW_LIFECYCLE_CLOSING) {
            continue;
        }
        {
            ErrorCode result = apply_scene_policy(service, window);
            if (result == CORE_OK) result = send_configure(service, window);
            if (result != CORE_OK && first_error == CORE_OK) {
                first_error = result;
            }
        }
    }
    return first_error;
}

static WindowRecord *foreground_window(WindowServiceHandle service)
{
    WindowRecord *window = find_window(service, service->active_window_id);

    if (window == NULL ||
        window->lifecycle != GUI_WINDOW_LIFECYCLE_FOREGROUND) {
        return NULL;
    }
    return window;
}

ErrorCode window_service_route_touch(WindowServiceHandle service,
                                     const GuiWindowTouchEvent *event)
{
    WindowRecord *window;
    GuiWindowTouchEvent routed;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result;

    if (service == NULL || event == NULL || event->input_serial == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    if (event->phase == GUI_TOUCH_DOWN) {
        window = foreground_window(service);
        if (window == NULL) return CORE_NOT_FOUND;
        service->touch_active = true;
        service->touch_id = event->touch_id;
        service->touch_window_id = window->window_id;
    } else {
        if (!service->touch_active || event->touch_id != service->touch_id) {
            return CORE_INVALID_STATE;
        }
        window = find_window(service, service->touch_window_id);
        if (window == NULL) return CORE_NOT_FOUND;
    }
    routed = *event;
    routed.window_id = window->window_id;
    result = gui_encode_window_touch(
        &routed, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    result = queue_payload(service, window->owner, GUI_WINDOW_TOUCH,
                           payload, size);
    if (routed.phase == GUI_TOUCH_UP || routed.phase == GUI_TOUCH_CANCEL) {
        service->touch_active = false;
        service->touch_id = 0;
        service->touch_window_id = 0;
    }
    return result;
}

ErrorCode window_service_route_pointer(WindowServiceHandle service,
                                       const GuiWindowPointerEvent *event)
{
    WindowRecord *window;
    GuiWindowPointerEvent routed;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result;

    if (service == NULL || event == NULL || event->input_serial == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    window = foreground_window(service);
    if (window == NULL) return CORE_NOT_FOUND;
    routed = *event;
    routed.window_id = window->window_id;
    result = gui_encode_window_pointer(
        &routed, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    return queue_payload(service, window->owner, GUI_WINDOW_POINTER,
                         payload, size);
}

ErrorCode window_service_route_key(WindowServiceHandle service,
                                   const GuiWindowKeyEvent *event)
{
    WindowRecord *window;
    GuiWindowKeyEvent routed;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result;

    if (service == NULL || event == NULL || event->input_serial == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    window = foreground_window(service);
    if (window == NULL) return CORE_NOT_FOUND;
    if ((window->flags & GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD) == 0) {
        return CORE_PERMISSION_DENIED;
    }
    routed = *event;
    routed.window_id = window->window_id;
    result = gui_encode_window_key(
        &routed, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    return queue_payload(service, window->owner, GUI_WINDOW_KEY,
                         payload, size);
}
