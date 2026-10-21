#include <windowserv.h>

#include <special/ipc.h>
#include <sys/eventloop.h>
#include <sys/memory.h>
#include <sys/time.h>

#define WINDOW_SERVICE_CLIENT_MAX ((usize)32)
#define WINDOW_SERVICE_WINDOW_MAX ((usize)512)
#define WINDOW_SERVICE_CLIENT_WINDOW_MAX ((usize)64)
#define WINDOW_SERVICE_EVENT_MAX ((usize)64)
#define WINDOW_SERVICE_RESPONSE_MAX ((usize)128)
#define WINDOW_SERVICE_RECONNECT_INTERVAL_MS ((DurationMs)250)
#define WINDOW_SERVICE_RECONNECT_BACKOFF_MAX_TICKS ((u32)16)
#define LINUX_KEY_HOME ((u16)102)
#define LINUX_KEY_VOLUMEDOWN ((u16)114)
#define LINUX_KEY_VOLUMEUP ((u16)115)
#define LINUX_KEY_POWER ((u16)116)
#define LINUX_KEY_LEFTCTRL ((u16)29)
#define LINUX_KEY_LEFTSHIFT ((u16)42)
#define LINUX_KEY_RIGHTSHIFT ((u16)54)
#define LINUX_KEY_LEFTALT ((u16)56)
#define LINUX_KEY_CAPSLOCK ((u16)58)
#define LINUX_KEY_NUMLOCK ((u16)69)
#define LINUX_KEY_RIGHTCTRL ((u16)97)
#define LINUX_KEY_RIGHTALT ((u16)100)
#define LINUX_KEY_LEFTMETA ((u16)125)
#define LINUX_KEY_RIGHTMETA ((u16)126)
#define LINUX_BTN_LEFT ((u16)272)
#define LINUX_BTN_RIGHT ((u16)273)
#define LINUX_BTN_MIDDLE ((u16)274)
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

typedef enum WindowSceneStep {
    WINDOW_SCENE_STEP_NONE = 0,
    WINDOW_SCENE_STEP_BIND = 1,
    WINDOW_SCENE_STEP_GEOMETRY = 2,
    WINDOW_SCENE_STEP_STACK = 3,
    WINDOW_SCENE_STEP_OUTPUT = 4,
    WINDOW_SCENE_STEP_VISIBILITY = 5,
    WINDOW_SCENE_STEP_REMOVE = 6
} WindowSceneStep;

typedef struct WindowClient {
    IpcConnectionHandle connection;
    EventSourceId source;
    bool is_shell;
    u64 next_event_serial;
} WindowClient;

typedef struct WindowRecord {
    bool active;
    bool scene_bound;
    bool scene_operation_pending;
    bool create_pending;
    bool create_failed_pending_remove;
    bool destroy_pending;
    bool rebind_pending;
    bool rebind_failed_pending_remove;
    bool scene_failed_pending_remove;
    bool scene_policy_dirty;
    u64 window_id;
    WindowClient *owner;
    WindowClient *create_client;
    WindowClient *destroy_client;
    WindowClient *rebind_client;
    u64 surface_id;
    u8 surface_bind_token[GUI_SURFACE_BIND_TOKEN_SIZE];
    u64 rebind_surface_id;
    u8 rebind_surface_bind_token[GUI_SURFACE_BIND_TOKEN_SIZE];
    GuiWindowRole role;
    u64 parent_id;
    u64 modal_parent_id;
    GuiWindowFlags flags;
    u16 title_length;
    u8 title[GUI_WINDOW_TITLE_MAX];
    GuiWindowLifecycle lifecycle;
    GuiWindowLifecycle lifecycle_before_suspend;
    GuiWindowLifecycle lifecycle_before_close;
    WindowSceneStep pending_scene_step;
    u64 pending_scene_request_id;
    u64 create_request_id;
    ErrorCode create_failure_error;
    u64 destroy_request_id;
    IpcMessageType destroy_result_type;
    u64 rebind_request_id;
    ErrorCode rebind_failure_error;
    u64 pending_close_serial;
    u64 pending_configure_serial;
    GuiWindowConfigureEvent confirmed_configure;
} WindowRecord;

struct WindowServiceHandle {
    MemoryBlock handle_block;
    EventLoopHandle loop;
    IpcServerHandle server;
    EventSourceId server_source;
    EventSourceId reconnect_source;
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
    bool has_compositor_endpoint;
    String compositor_endpoint;
    u8 controller_token[WINDOW_SERVICE_CONTROLLER_TOKEN_SIZE];
    u64 next_scene_request_id;
    IpcConnectionHandle io_connection;
    EventSourceId io_source;
    WindowIoState io_state;
    bool has_io_endpoint;
    String io_endpoint;
    u32 io_touch_x;
    u32 io_touch_y;
    i32 io_pointer_x;
    i32 io_pointer_y;
    bool io_touch_down;
    u64 io_touch_device_id;
    u32 io_pointer_buttons;
    GuiModifierMask io_modifiers;
    u32 compositor_reconnect_delay_ticks;
    u32 compositor_reconnect_wait_ticks;
    u32 io_reconnect_delay_ticks;
    u32 io_reconnect_wait_ticks;
    u64 next_input_serial;
};

static void remove_client(WindowServiceHandle service, WindowClient *client);
static ErrorCode send_configure(WindowServiceHandle service,
                                WindowRecord *window);
static ErrorCode apply_scene_policy(WindowServiceHandle service,
                                    WindowRecord *window);
static ErrorCode cancel_active_touch(WindowServiceHandle service);
static void compositor_suspend(WindowServiceHandle service);
static void io_suspend(WindowServiceHandle service);
static u64 next_input_serial(WindowServiceHandle service);
static WindowRecord *input_target_window(WindowServiceHandle service,
                                         bool keyboard);
static ErrorCode complete_destroy(WindowServiceHandle service,
                                  WindowRecord *window, ErrorCode error);
static ErrorCode cancel_touch_if_input_target_changed(
    WindowServiceHandle service);
static ErrorCode publish_dependent_visibility(WindowServiceHandle service,
                                              u64 old_active_id,
                                              u64 new_active_id);

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

static WindowRecord *find_scene_request_window(WindowServiceHandle service,
                                               u64 request_id)
{
    usize index;

    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        if (service->windows[index].active &&
            service->windows[index].scene_operation_pending &&
            service->windows[index].pending_scene_request_id == request_id) {
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

static WindowRecord *find_home_window(WindowServiceHandle service)
{
    usize index;
    WindowRecord *best = NULL;

    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        WindowRecord *window = &service->windows[index];
        if (window->active && window->role == GUI_WINDOW_ROLE_HOME &&
            window->lifecycle != GUI_WINDOW_LIFECYCLE_HIDDEN &&
            window->lifecycle != GUI_WINDOW_LIFECYCLE_CLOSING) {
            if (best == NULL || window->window_id > best->window_id) {
                best = window;
            }
        }
    }
    return best;
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

static bool window_chain_contains(WindowServiceHandle service, u64 window_id,
                                  u64 ancestor_id)
{
    usize depth;

    if (ancestor_id == 0) return false;
    for (depth = 0; window_id != 0 && depth < WINDOW_SERVICE_WINDOW_MAX;
         ++depth) {
        WindowRecord *window;
        if (window_id == ancestor_id) return true;
        window = find_window(service, window_id);
        if (window == NULL) return false;
        window_id = window->parent_id;
    }
    return false;
}

static bool window_is_visible_in_service(WindowServiceHandle service,
                                         const WindowRecord *window)
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
        if (window->parent_id == 0 && window->modal_parent_id == 0) {
            return window->lifecycle == GUI_WINDOW_LIFECYCLE_FOREGROUND;
        }
        return window->lifecycle == GUI_WINDOW_LIFECYCLE_FOREGROUND &&
               (window_chain_contains(service, window->parent_id,
                                      service->active_window_id) ||
                window_chain_contains(service, window->modal_parent_id,
                                      service->active_window_id));
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

static ErrorCode send_create_result(WindowServiceHandle service,
                                    WindowClient *client, u64 request_id,
                                    ErrorCode error, u64 window_id)
{
    GuiWindowCreateResult response;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result;

    if (client == NULL || client->connection == NULL) return CORE_OK;
    response.result.error = error;
    response.result.request_id = request_id;
    response.window_id = error == CORE_OK ? window_id : 0;
    result = gui_encode_window_create_result(
        &response, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    return queue_payload(service, client, GUI_WINDOW_CREATE_RESULT,
                         payload, size);
}

static ErrorCode send_service_event(WindowServiceHandle service,
                                    WindowRecord *window,
                                    IpcMessageType type,
                                    GuiWindowLifecycleReason reason)
{
    GuiWindowServiceEvent event;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result;

    if (window->owner == NULL || window->owner->connection == NULL) {
        return CORE_OK;
    }
    if (window->owner->next_event_serial == 0) return CORE_OVERFLOW;
    event.serial = window->owner->next_event_serial++;
    event.window_id = window->window_id;
    event.reason = reason;
    result = gui_encode_window_service_event(
        &event, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    (void)queue_payload(service, window->owner, type, payload, size);
    return CORE_OK;
}

static ErrorCode send_close_request(WindowServiceHandle service,
                                    WindowRecord *window)
{
    GuiWindowCloseEvent event;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result;

    if (window->owner == NULL || window->owner->connection == NULL) {
        return CORE_OK;
    }
    if (window->owner->next_event_serial == 0) return CORE_OVERFLOW;
    event.close_serial = window->owner->next_event_serial++;
    event.window_id = window->window_id;
    window->pending_close_serial = event.close_serial;
    result = gui_encode_window_close_event(
        &event, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    (void)queue_payload(service, window->owner, GUI_WINDOW_CLOSE_REQUESTED,
                        payload, size);
    return CORE_OK;
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
    if (result != CORE_OK) {
        compositor_suspend(service);
        return CORE_OK;
    }
    result = ipc_connection_flush(service->compositor_connection, &pending);
    if (result != CORE_OK) {
        compositor_suspend(service);
        return CORE_OK;
    }
    return compositor_update_interest(service, pending);
}

static u32 inset_limit(u32 value, u32 limit)
{
    return value > limit ? limit : value;
}

static GuiSceneRect scene_geometry_for_window(WindowServiceHandle service,
                                              const WindowRecord *window)
{
    GuiSceneRect geometry;
    u32 width = service->viewport_width;
    u32 height = service->viewport_height;
    u32 inset;

    geometry.x = 0;
    geometry.y = 0;
    geometry.width = width;
    geometry.height = height;
    if (window->role == GUI_WINDOW_ROLE_SYSTEM_BAR) {
        if (service->safe_insets.top != 0) {
            inset = inset_limit(service->safe_insets.top, height);
            geometry.height = inset;
        } else if (service->safe_insets.bottom != 0) {
            inset = inset_limit(service->safe_insets.bottom, height);
            geometry.y = height - inset;
            geometry.height = inset;
        } else if (service->safe_insets.left != 0) {
            inset = inset_limit(service->safe_insets.left, width);
            geometry.width = inset;
        } else if (service->safe_insets.right != 0) {
            inset = inset_limit(service->safe_insets.right, width);
            geometry.x = width - inset;
            geometry.width = inset;
        }
    } else if (window->role == GUI_WINDOW_ROLE_IME &&
               service->safe_insets.bottom != 0) {
        inset = inset_limit(service->safe_insets.bottom, height);
        geometry.y = height - inset;
        geometry.height = inset;
    }
    return geometry;
}

static ErrorCode scene_queue_step(WindowServiceHandle service,
                                  WindowRecord *window,
                                  WindowSceneStep step)
{
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size = 0;
    IpcMessageType type = 0;
    u64 request_id;
    ErrorCode result = CORE_OK;

    if (window->scene_operation_pending) return CORE_OK;
    if (service->compositor_connection == NULL ||
        service->compositor_state != WINDOW_COMPOSITOR_READY) {
        return CORE_OK;
    }
    request_id = next_scene_request_id(service);

    if (step == WINDOW_SCENE_STEP_BIND) {
        GuiSceneBindRequest request;
        usize index;
        request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
        request.prefix.request_id = request_id;
        request.window_id = window->window_id;
        request.surface_id = window->surface_id;
        for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
            request.surface_bind_token[index] =
                window->surface_bind_token[index];
        }
        result = gui_encode_scene_bind(&request,
            (Bytes){ payload, sizeof(payload) }, &size);
        type = GUI_SCENE_BIND_WINDOW;
    } else if (step == WINDOW_SCENE_STEP_GEOMETRY) {
        GuiSceneGeometryRequest request;
        request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
        request.prefix.request_id = request_id;
        request.window_id = window->window_id;
        request.geometry = scene_geometry_for_window(service, window);
        request.clip_x = 0;
        request.clip_y = 0;
        request.clip_width = 0;
        request.clip_height = 0;
        result = gui_encode_scene_geometry(&request,
            (Bytes){ payload, sizeof(payload) }, &size);
        type = GUI_SCENE_SET_GEOMETRY;
    } else if (step == WINDOW_SCENE_STEP_STACK) {
        GuiSceneStackRequest request;
        request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
        request.prefix.request_id = request_id;
        request.window_id = window->window_id;
        request.layer = scene_layer_for_role(window->role);
        request.above_window_id = 0;
        result = gui_encode_scene_stack(&request,
            (Bytes){ payload, sizeof(payload) }, &size);
        type = GUI_SCENE_SET_STACK;
    } else if (step == WINDOW_SCENE_STEP_OUTPUT) {
        GuiSceneOutputRequest request;
        request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
        request.prefix.request_id = request_id;
        request.window_id = window->window_id;
        request.display_id = 0;
        result = gui_encode_scene_output(&request,
            (Bytes){ payload, sizeof(payload) }, &size);
        type = GUI_SCENE_SET_OUTPUT;
    } else if (step == WINDOW_SCENE_STEP_VISIBILITY) {
        GuiSceneVisibilityRequest request;
        request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
        request.prefix.request_id = request_id;
        request.window_id = window->window_id;
        request.visibility = window_is_visible_in_service(service, window) ?
                             GUI_SCENE_VISIBLE : GUI_SCENE_HIDDEN;
        result = gui_encode_scene_visibility(&request,
            (Bytes){ payload, sizeof(payload) }, &size);
        type = GUI_SCENE_SET_VISIBILITY;
    } else if (step == WINDOW_SCENE_STEP_REMOVE) {
        GuiSceneWindowRequest request;
        request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
        request.prefix.request_id = request_id;
        request.window_id = window->window_id;
        result = gui_encode_scene_window(&request,
            (Bytes){ payload, sizeof(payload) }, &size);
        type = GUI_SCENE_REMOVE_WINDOW;
    } else {
        return CORE_INVALID_ARGUMENT;
    }

    if (result != CORE_OK) return result;
    result = compositor_queue(service, type, payload, size);
    if (result != CORE_OK) return result;
    if (service->compositor_connection == NULL ||
        service->compositor_state != WINDOW_COMPOSITOR_READY) {
        window->scene_policy_dirty = true;
        return CORE_OK;
    }
    window->scene_operation_pending = true;
    window->pending_scene_step = step;
    window->pending_scene_request_id = request_id;
    return CORE_OK;
}

static ErrorCode apply_scene_policy(WindowServiceHandle service,
                                    WindowRecord *window)
{
    if (!window->active) return CORE_OK;
    if (window->scene_operation_pending) {
        window->scene_policy_dirty = true;
        return CORE_OK;
    }
    if (service->compositor_state != WINDOW_COMPOSITOR_READY ||
        service->compositor_connection == NULL) {
        window->scene_policy_dirty = true;
        return CORE_OK;
    }
    window->scene_policy_dirty = false;
    return scene_queue_step(
        service, window,
        window->scene_bound ? WINDOW_SCENE_STEP_GEOMETRY :
                              WINDOW_SCENE_STEP_BIND);
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
    (void)queue_payload(service, window->owner,
                        GUI_WINDOW_LIFECYCLE_CHANGED, payload, size);
    return CORE_OK;
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
    (void)queue_payload(service, window->owner, GUI_WINDOW_CONFIGURE,
                        payload, size);
    return CORE_OK;
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
    event.visible = window_is_visible_in_service(service, window);
    result = gui_encode_window_visibility(
        &event, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    (void)queue_payload(service, window->owner,
                        GUI_WINDOW_VISIBILITY_CHANGED, payload, size);
    return CORE_OK;
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

static ErrorCode publish_window_visibility_policy(WindowServiceHandle service,
                                                  WindowRecord *window)
{
    ErrorCode result = apply_scene_policy(service, window);

    if (result == CORE_OK) result = send_visibility(service, window);
    return result;
}

static ErrorCode publish_dependent_visibility(WindowServiceHandle service,
                                              u64 old_active_id,
                                              u64 new_active_id)
{
    usize index;
    ErrorCode first_error = CORE_OK;

    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        WindowRecord *window = &service->windows[index];
        if (!window->active ||
            (window->role != GUI_WINDOW_ROLE_DIALOG &&
             window->role != GUI_WINDOW_ROLE_TRANSIENT)) {
            continue;
        }
        if (!window_chain_contains(service, window->parent_id, old_active_id) &&
            !window_chain_contains(service, window->parent_id, new_active_id) &&
            !window_chain_contains(service, window->modal_parent_id,
                                   old_active_id) &&
            !window_chain_contains(service, window->modal_parent_id,
                                   new_active_id)) {
            continue;
        }
        {
            ErrorCode result = publish_window_visibility_policy(service, window);
            if (result != CORE_OK && first_error == CORE_OK) {
                first_error = result;
            }
        }
    }
    return first_error;
}

static void clear_window(WindowRecord *window)
{
    usize index;

    window->active = false;
    window->scene_bound = false;
    window->scene_operation_pending = false;
    window->create_pending = false;
    window->create_failed_pending_remove = false;
    window->destroy_pending = false;
    window->rebind_pending = false;
    window->rebind_failed_pending_remove = false;
    window->scene_failed_pending_remove = false;
    window->scene_policy_dirty = false;
    window->window_id = 0;
    window->owner = NULL;
    window->create_client = NULL;
    window->destroy_client = NULL;
    window->rebind_client = NULL;
    window->surface_id = 0;
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        window->surface_bind_token[index] = 0;
        window->rebind_surface_bind_token[index] = 0;
    }
    window->rebind_surface_id = 0;
    window->role = 0;
    window->parent_id = 0;
    window->modal_parent_id = 0;
    window->flags = 0;
    window->title_length = 0;
    for (index = 0; index < GUI_WINDOW_TITLE_MAX; ++index) {
        window->title[index] = 0;
    }
    window->lifecycle = 0;
    window->lifecycle_before_suspend = 0;
    window->lifecycle_before_close = 0;
    window->pending_scene_step = WINDOW_SCENE_STEP_NONE;
    window->pending_scene_request_id = 0;
    window->create_request_id = 0;
    window->create_failure_error = CORE_OK;
    window->destroy_request_id = 0;
    window->destroy_result_type = 0;
    window->rebind_request_id = 0;
    window->rebind_failure_error = CORE_OK;
    window->pending_close_serial = 0;
    window->pending_configure_serial = 0;
    window->confirmed_configure = (GuiWindowConfigureEvent){ 0 };
}

static ErrorCode complete_destroy(WindowServiceHandle service,
                                  WindowRecord *window, ErrorCode error)
{
    WindowClient *client = window->destroy_client;
    u64 request_id = window->destroy_request_id;
    IpcMessageType result_type = window->destroy_result_type;

    clear_window(window);
    if (result_type == 0 || client == NULL || client->connection == NULL) {
        return CORE_OK;
    }
    (void)send_basic(service, client, result_type, request_id, error);
    return CORE_OK;
}

static ErrorCode fail_destroy(WindowServiceHandle service,
                              WindowRecord *window, ErrorCode error)
{
    WindowClient *client = window->destroy_client;
    u64 request_id = window->destroy_request_id;
    IpcMessageType result_type = window->destroy_result_type;
    ErrorCode result = CORE_OK;

    window->destroy_pending = false;
    window->destroy_client = NULL;
    window->destroy_request_id = 0;
    window->destroy_result_type = 0;
    window->lifecycle = window->lifecycle_before_close != 0 ?
        window->lifecycle_before_close : GUI_WINDOW_LIFECYCLE_BACKGROUND;
    window->lifecycle_before_close = 0;
    window->pending_close_serial = 0;
    if (window->owner != NULL && window->owner->connection != NULL) {
        result = publish_window_update(service, window,
                                       GUI_WINDOW_REASON_CLIENT_REQUEST);
    }
    if (result_type != 0 && client != NULL &&
        client->connection != NULL) {
        (void)send_basic(service, client, result_type, request_id, error);
    }
    return result;
}

static ErrorCode destroy_window(WindowServiceHandle service,
                                WindowRecord *window,
                                WindowClient *result_client,
                                u64 request_id,
                                IpcMessageType result_type)
{
    u64 window_id = window->window_id;
    usize index;
    ErrorCode result;

    if (window->destroy_pending) {
        return CORE_INVALID_STATE;
    }
    if (service->touch_window_id == window_id) {
        result = cancel_active_touch(service);
        if (result != CORE_OK) return result;
    }
    if (service->active_window_id == window_id) service->active_window_id = 0;
    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        if (service->windows[index].active &&
            service->windows[index].parent_id == window_id) {
            (void)destroy_window(service, &service->windows[index],
                                 NULL, 0, 0);
        } else if (service->windows[index].active &&
                   service->windows[index].modal_parent_id == window_id) {
            service->windows[index].modal_parent_id = 0;
        }
    }
    if (window->lifecycle_before_close == 0 &&
        window->lifecycle != GUI_WINDOW_LIFECYCLE_CLOSING) {
        window->lifecycle_before_close = window->lifecycle;
    }
    window->lifecycle = GUI_WINDOW_LIFECYCLE_CLOSING;
    window->destroy_pending = true;
    window->destroy_client = result_client;
    window->destroy_request_id = request_id;
    window->destroy_result_type = result_type;
    window->create_client = NULL;
    window->create_request_id = 0;
    window->create_pending = false;
    if (result_type == 0 && window->owner != NULL &&
        window->owner->connection != NULL) {
        (void)send_lifecycle(service, window,
                             GUI_WINDOW_REASON_CLIENT_REQUEST);
        (void)send_visibility(service, window);
    }

    if (window->scene_operation_pending) {
        return CORE_OK;
    }
    if (window->scene_bound) {
        return scene_queue_step(service, window, WINDOW_SCENE_STEP_REMOVE);
    }
    return complete_destroy(service, window, CORE_OK);
}

static ErrorCode cancel_active_touch(WindowServiceHandle service)
{
    WindowRecord *window;
    GuiWindowTouchEvent event;
    MonotonicTime now;
    ErrorCode result;

    if (!service->touch_active) return CORE_OK;
    window = find_window(service, service->touch_window_id);
    if (window == NULL || window->owner == NULL ||
        window->owner->connection == NULL) {
        service->touch_active = false;
        service->touch_window_id = 0;
        service->touch_id = 0;
        service->io_touch_down = false;
        return CORE_OK;
    }
    result = time_monotonic_now(&now);
    if (result != CORE_OK) return result;
    event.window_id = window->window_id;
    event.input_serial = next_input_serial(service);
    event.timestamp_ms = now;
    event.touch_id = service->touch_id;
    event.phase = GUI_TOUCH_CANCEL;
    event.x = 0;
    event.y = 0;
    result = window_service_route_touch(service, &event);
    service->touch_active = false;
    service->touch_window_id = 0;
    service->touch_id = 0;
    service->io_touch_down = false;
    return result == CORE_INVALID_ARGUMENT ? result : CORE_OK;
}

static ErrorCode handle_window_create(WindowServiceHandle service,
                                      WindowClient *client,
                                      const IpcMessage *message)
{
    GuiWindowCreateRequest request;
    WindowRecord *window = NULL;
    WindowRecord *parent;
    usize index;
    ErrorCode error = CORE_OK;
    ErrorCode result = gui_decode_window_create(message, &request);

    if (result != CORE_OK) return result;

    if (role_is_shell_only(request.role) && !client->is_shell) {
        error = CORE_PERMISSION_DENIED;
    } else if (client_window_count(service, client) >=
               WINDOW_SERVICE_CLIENT_WINDOW_MAX) {
        error = CORE_OUT_OF_MEMORY;
    } else if (request.parent_id != 0) {
        parent = find_window(service, request.parent_id);
        if (parent == NULL || parent->owner != client) {
            error = CORE_PERMISSION_DENIED;
        } else if (request.role == GUI_WINDOW_ROLE_DIALOG &&
                   parent->role != GUI_WINDOW_ROLE_APP &&
                   parent->role != GUI_WINDOW_ROLE_DIALOG) {
            error = CORE_INVALID_ARGUMENT;
        }
    }

    if (error == CORE_OK) {
        if (service->next_window_id == 0) {
            error = CORE_OVERFLOW;
        } else if (service->has_compositor_endpoint &&
                   service->compositor_state != WINDOW_COMPOSITOR_READY) {
            error = CORE_INVALID_STATE;
        } else {
            window = find_free_window(service);
            if (window == NULL) {
                error = CORE_OUT_OF_MEMORY;
            } else {
                window->active = true;
                window->scene_bound = false;
                window->scene_operation_pending = false;
                window->create_pending = false;
                window->create_failed_pending_remove = false;
                window->destroy_pending = false;
                window->rebind_pending = false;
                window->rebind_failed_pending_remove = false;
                window->scene_failed_pending_remove = false;
                window->scene_policy_dirty = false;
                window->window_id = service->next_window_id++;
                window->owner = client;
                window->create_client = NULL;
                window->destroy_client = NULL;
                window->rebind_client = NULL;
                window->surface_id = request.surface_id;
                for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
                    window->surface_bind_token[index] =
                        request.surface_bind_token[index];
                    window->rebind_surface_bind_token[index] = 0;
                }
                window->rebind_surface_id = 0;
                window->role = request.role;
                window->parent_id = request.parent_id;
                window->modal_parent_id =
                    (request.role == GUI_WINDOW_ROLE_DIALOG ||
                     request.role == GUI_WINDOW_ROLE_TRANSIENT) ?
                    request.parent_id : 0;
                window->flags = request.flags;
                window->title_length = request.title_length;
                for (index = 0; index < request.title_length; ++index) {
                    window->title[index] = request.title[index];
                }
                window->lifecycle = GUI_WINDOW_LIFECYCLE_HIDDEN;
                window->pending_configure_serial = 0;
                window->confirmed_configure = (GuiWindowConfigureEvent){ 0 };
                window->create_failure_error = CORE_OK;
                window->destroy_request_id = 0;
                window->destroy_result_type = 0;
                window->rebind_request_id = 0;
                window->rebind_failure_error = CORE_OK;
            }
        }
    }

    if (error == CORE_OK && window == NULL) error = CORE_OUT_OF_MEMORY;
    if (error != CORE_OK || !service->has_compositor_endpoint) {
        return send_create_result(service, client, request.prefix.request_id,
                                  error, window == NULL ? 0 : window->window_id);
    }
    window->create_pending = true;
    window->create_client = client;
    window->create_request_id = request.prefix.request_id;
    result = apply_scene_policy(service, window);
    if (result != CORE_OK) return result;
    return CORE_OK;
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
    result = destroy_window(service, window, client, request.prefix.request_id,
                            GUI_WINDOW_DESTROY_RESULT);
    if (result != CORE_OK) {
        return send_basic(service, client, GUI_WINDOW_DESTROY_RESULT,
                          request.prefix.request_id, result);
    }
    return CORE_OK;
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
    result = cancel_touch_if_input_target_changed(service);
    if (result != CORE_OK) return result;
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
    if (service->touch_window_id == window->window_id) {
        result = cancel_active_touch(service);
        if (result != CORE_OK) return result;
    }
    window->lifecycle = GUI_WINDOW_LIFECYCLE_HIDDEN;
    if (service->active_window_id == window->window_id) {
        service->active_window_id = 0;
    }
    result = cancel_touch_if_input_target_changed(service);
    if (result != CORE_OK) return result;
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
    if (window->role != GUI_WINDOW_ROLE_DIALOG &&
        window->role != GUI_WINDOW_ROLE_TRANSIENT) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_INVALID_ARGUMENT);
    }
    if (request.parent_id != 0) {
        parent = find_window(service, request.parent_id);
        if (parent == NULL || parent->owner != client) {
            return send_operation(service, client, request.prefix.request_id,
                                  CORE_PERMISSION_DENIED);
        }
        if (window->role == GUI_WINDOW_ROLE_DIALOG &&
            parent->role != GUI_WINDOW_ROLE_APP &&
            parent->role != GUI_WINDOW_ROLE_DIALOG) {
            return send_operation(service, client, request.prefix.request_id,
                                  CORE_INVALID_ARGUMENT);
        }
    }
    window->modal_parent_id = request.parent_id;
    result = cancel_touch_if_input_target_changed(service);
    if (result != CORE_OK) return result;
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
    if (window->destroy_pending || window->create_pending ||
        window->rebind_pending || window->scene_operation_pending) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_INVALID_STATE);
    }
    if (service->has_compositor_endpoint &&
        service->compositor_state != WINDOW_COMPOSITOR_READY) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_INVALID_STATE);
    }
    if (!service->has_compositor_endpoint) {
        window->surface_id = request.surface_id;
        for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
            window->surface_bind_token[index] =
                request.surface_bind_token[index];
        }
        return send_operation(service, client, request.prefix.request_id,
                              CORE_OK);
    }
    window->rebind_pending = true;
    window->rebind_client = client;
    window->rebind_request_id = request.prefix.request_id;
    window->rebind_surface_id = request.surface_id;
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        window->rebind_surface_bind_token[index] =
            request.surface_bind_token[index];
    }
    if (window->scene_bound) {
        return scene_queue_step(service, window, WINDOW_SCENE_STEP_REMOVE);
    }
    if (window->lifecycle == GUI_WINDOW_LIFECYCLE_SUSPENDED &&
        window->lifecycle_before_suspend != 0) {
        window->lifecycle = window->lifecycle_before_suspend;
        window->lifecycle_before_suspend = 0;
    }
    window->surface_id = window->rebind_surface_id;
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        window->surface_bind_token[index] =
            window->rebind_surface_bind_token[index];
    }
    result = apply_scene_policy(service, window);
    if (result != CORE_OK) {
        window->rebind_pending = false;
        window->rebind_client = NULL;
        window->rebind_request_id = 0;
        return send_operation(service, client, request.prefix.request_id,
                              result);
    }
    return CORE_OK;
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
    u64 old_active_id;
    ErrorCode result = cancel_active_touch(service);

    if (result != CORE_OK) return result;
    old_active_id = service->active_window_id;
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
    result = publish_window_update(service, target,
                                   GUI_WINDOW_REASON_SHELL_ACTIVATION);
    if (result == CORE_OK) {
        result = publish_dependent_visibility(service, old_active_id,
                                              target->window_id);
    }
    return result;
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

static ErrorCode request_window_close(WindowServiceHandle service,
                                      WindowRecord *window,
                                      GuiWindowLifecycleReason reason)
{
    ErrorCode result;

    if (window == NULL || window->lifecycle == GUI_WINDOW_LIFECYCLE_CLOSING) {
        return CORE_INVALID_STATE;
    }
    result = cancel_active_touch(service);
    if (result != CORE_OK) return result;
    window->lifecycle_before_close = window->lifecycle;
    window->lifecycle = GUI_WINDOW_LIFECYCLE_CLOSING;
    result = apply_scene_policy(service, window);
    if (result == CORE_OK) result = send_lifecycle(service, window, reason);
    if (result == CORE_OK) result = send_visibility(service, window);
    if (result == CORE_OK) result = send_close_request(service, window);
    return result;
}

static ErrorCode send_navigation_event(WindowServiceHandle service,
                                       WindowRecord *target,
                                       GuiNavigationAction action)
{
    GuiWindowNavigationEvent event;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result;

    if (target->owner == NULL || target->owner->connection == NULL) {
        return CORE_OK;
    }
    if (target->owner->next_event_serial == 0) return CORE_OVERFLOW;
    event.serial = target->owner->next_event_serial++;
    event.window_id = target->window_id;
    event.action = action;
    result = gui_encode_window_navigation(
        &event, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    (void)queue_payload(service, target->owner,
                        GUI_WINDOW_NAVIGATION_REQUESTED, payload, size);
    return CORE_OK;
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
    ErrorCode result = gui_decode_shell_navigate(message, &request);

    if (result != CORE_OK) return result;
    if (!client->is_shell) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_PERMISSION_DENIED);
    }

    if (request.action == GUI_NAVIGATION_HOME) {
        target = find_home_window(service);
        if (target == NULL) {
            return send_operation(service, client, request.prefix.request_id,
                                  CORE_NOT_FOUND);
        }
        result = activate_window(service, target);
        if (result != CORE_OK) return result;
        return send_operation(service, client, request.prefix.request_id,
                              CORE_OK);
    }

    if (request.action == GUI_NAVIGATION_OVERVIEW) {
        target = find_home_window(service);
        if (target == NULL) {
            return send_operation(service, client, request.prefix.request_id,
                                  CORE_NOT_FOUND);
        }
        result = activate_window(service, target);
        if (result != CORE_OK) return result;
        result = send_operation(service, client, request.prefix.request_id,
                                CORE_OK);
        if (result != CORE_OK) return result;
        return send_navigation_event(service, target, request.action);
    }

    target = request.target_id != 0 ?
        find_window(service, request.target_id) :
        input_target_window(service, false);
    if (target == NULL) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_NOT_FOUND);
    }

    if (request.action == GUI_NAVIGATION_BACK &&
        (target->flags & GUI_WINDOW_FLAG_HANDLES_BACK) == 0) {
        WindowRecord *parent = target->parent_id != 0 ?
            find_window(service, target->parent_id) : NULL;
        WindowRecord *home = find_home_window(service);
        if (target->role == GUI_WINDOW_ROLE_HOME) {
            result = CORE_OK;
        } else if (target->role == GUI_WINDOW_ROLE_DIALOG ||
                   target->role == GUI_WINDOW_ROLE_TRANSIENT) {
            result = request_window_close(
                service, target, GUI_WINDOW_REASON_CLIENT_REQUEST);
        } else if (parent != NULL && role_can_activate(parent->role)) {
            result = activate_window(service, parent);
        } else if (home != NULL && target != home) {
            result = activate_window(service, home);
        } else {
            result = request_window_close(
                service, target, GUI_WINDOW_REASON_CLIENT_REQUEST);
        }
        if (result != CORE_OK) return result;
        return send_operation(service, client, request.prefix.request_id,
                              CORE_OK);
    }

    result = send_operation(service, client, request.prefix.request_id,
                            CORE_OK);
    if (result != CORE_OK) return result;
    return send_navigation_event(service, target, request.action);
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
    if (window->lifecycle != GUI_WINDOW_LIFECYCLE_CLOSING ||
        window->pending_close_serial == 0 ||
        window->pending_close_serial != request.close_serial) {
        return send_operation(service, client, request.prefix.request_id,
                              CORE_INVALID_STATE);
    }
    if (request.response == GUI_CLOSE_ALLOW) {
        result = destroy_window(service, window, client,
                                request.prefix.request_id,
                                GUI_WINDOW_OPERATION_RESULT);
        if (result != CORE_OK) {
            return send_operation(service, client, request.prefix.request_id,
                                  result);
        }
        return CORE_OK;
    }
    if (request.response == GUI_CLOSE_DENY) {
        window->pending_close_serial = 0;
        window->lifecycle = window->lifecycle_before_close != 0 ?
            window->lifecycle_before_close : GUI_WINDOW_LIFECYCLE_BACKGROUND;
        window->lifecycle_before_close = 0;
        result = publish_window_update(service, window,
                                       GUI_WINDOW_REASON_CLIENT_REQUEST);
        if (result != CORE_OK) return result;
        return send_operation(service, client, request.prefix.request_id,
                              CORE_OK);
    }
    if (request.response == GUI_CLOSE_DEFER) {
        window->pending_close_serial = 0;
        window->lifecycle = window->lifecycle_before_close != 0 ?
            window->lifecycle_before_close : GUI_WINDOW_LIFECYCLE_BACKGROUND;
        window->lifecycle_before_close = 0;
        result = publish_window_update(service, window,
                                       GUI_WINDOW_REASON_CLIENT_REQUEST);
        if (result != CORE_OK) return result;
        return send_operation(service, client, request.prefix.request_id,
                              CORE_OK);
    }
    return send_operation(service, client, request.prefix.request_id,
                          CORE_INVALID_ARGUMENT);
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

static void reconnect_reset_backoff(u32 *delay_ticks, u32 *wait_ticks)
{
    *delay_ticks = 1;
    *wait_ticks = 0;
}

static void reconnect_schedule_retry(u32 *delay_ticks, u32 *wait_ticks)
{
    u32 delay = *delay_ticks == 0 ? 1 : *delay_ticks;

    if (delay < WINDOW_SERVICE_RECONNECT_BACKOFF_MAX_TICKS) {
        delay <<= 1;
        if (delay > WINDOW_SERVICE_RECONNECT_BACKOFF_MAX_TICKS) {
            delay = WINDOW_SERVICE_RECONNECT_BACKOFF_MAX_TICKS;
        }
    }
    *delay_ticks = delay;
    *wait_ticks = delay;
}

static void reconnect_tick(WindowServiceHandle service)
{
    if (service->compositor_reconnect_wait_ticks != 0) {
        --service->compositor_reconnect_wait_ticks;
    }
    if (service->io_reconnect_wait_ticks != 0) {
        --service->io_reconnect_wait_ticks;
    }
}

static void compositor_suspend(WindowServiceHandle service)
{
    WindowCompositorState previous_state = service->compositor_state;
    usize index;

    if (service->compositor_connection != NULL) {
        (void)eventloop_remove_source(service->loop,
                                      service->compositor_source);
        (void)ipc_connection_close(&service->compositor_connection);
    }
    service->compositor_source = 0;
    service->compositor_state = WINDOW_COMPOSITOR_SUSPENDED;
    if (service->has_compositor_endpoint) {
        if (previous_state == WINDOW_COMPOSITOR_READY) {
            reconnect_reset_backoff(&service->compositor_reconnect_delay_ticks,
                                    &service->compositor_reconnect_wait_ticks);
        } else {
            reconnect_schedule_retry(
                &service->compositor_reconnect_delay_ticks,
                &service->compositor_reconnect_wait_ticks);
        }
    }
    (void)cancel_active_touch(service);
    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        WindowRecord *window = &service->windows[index];
        if (!window->active) continue;
        window->scene_bound = false;
        window->scene_operation_pending = false;
        window->pending_scene_step = WINDOW_SCENE_STEP_NONE;
        window->pending_scene_request_id = 0;
        window->scene_policy_dirty = true;
        if (window->create_pending) {
            (void)send_create_result(service, window->create_client,
                                     window->create_request_id,
                                     CORE_INVALID_STATE, 0);
            clear_window(window);
            continue;
        }
        if (window->destroy_pending) {
            (void)complete_destroy(service, window, CORE_OK);
            continue;
        }
        if (window->rebind_pending) {
            if (window->rebind_client != NULL &&
                window->rebind_client->connection != NULL) {
                (void)send_operation(service, window->rebind_client,
                                     window->rebind_request_id,
                                     CORE_INVALID_STATE);
            }
            window->rebind_pending = false;
            window->rebind_failed_pending_remove = false;
            window->rebind_client = NULL;
            window->rebind_request_id = 0;
            window->rebind_surface_id = 0;
        }
        if (window->lifecycle != GUI_WINDOW_LIFECYCLE_HIDDEN &&
            window->lifecycle != GUI_WINDOW_LIFECYCLE_CLOSING &&
            window->lifecycle != GUI_WINDOW_LIFECYCLE_SUSPENDED) {
            window->lifecycle_before_suspend = window->lifecycle;
            window->lifecycle = GUI_WINDOW_LIFECYCLE_SUSPENDED;
            (void)send_service_event(
                service, window, GUI_WINDOW_RENDERING_SUSPENDED,
                GUI_WINDOW_REASON_DISPLAY_LOSS);
            (void)send_lifecycle(service, window,
                                  GUI_WINDOW_REASON_DISPLAY_LOSS);
            (void)send_visibility(service, window);
        }
    }
}

static ErrorCode restore_windows_after_compositor_bind(
    WindowServiceHandle service)
{
    usize index;
    ErrorCode first_error = CORE_OK;

    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        WindowRecord *window = &service->windows[index];
        if (!window->active) continue;
        if (window->lifecycle == GUI_WINDOW_LIFECYCLE_SUSPENDED &&
            window->lifecycle_before_suspend != 0) {
            ErrorCode result;
            window->lifecycle = window->lifecycle_before_suspend;
            window->lifecycle_before_suspend = 0;
            result = send_lifecycle(service, window,
                                    GUI_WINDOW_REASON_RECOVERY);
            if (result == CORE_OK) result = send_visibility(service, window);
            if (result == CORE_OK) result = send_configure(service, window);
            if (result != CORE_OK && first_error == CORE_OK) {
                first_error = result;
            }
        }
        {
            ErrorCode result = apply_scene_policy(service, window);
            if (result != CORE_OK && first_error == CORE_OK) {
                first_error = result;
            }
        }
    }
    return first_error;
}

static ErrorCode scene_fail_window(WindowServiceHandle service,
                                   WindowRecord *window,
                                   ErrorCode error)
{
    ErrorCode result = CORE_OK;

    if (window->create_pending) {
        if (window->scene_bound && !window->scene_operation_pending) {
            window->create_failed_pending_remove = true;
            window->create_failure_error = error;
            return scene_queue_step(service, window,
                                    WINDOW_SCENE_STEP_REMOVE);
        }
        (void)send_create_result(service, window->create_client,
                                 window->create_request_id, error, 0);
        clear_window(window);
        return CORE_OK;
    }
    if (window->rebind_pending) {
        if (window->scene_bound && !window->scene_operation_pending) {
            window->rebind_failed_pending_remove = true;
            window->rebind_failure_error = error;
            return scene_queue_step(service, window,
                                    WINDOW_SCENE_STEP_REMOVE);
        }
        if (window->rebind_client != NULL &&
            window->rebind_client->connection != NULL) {
            (void)send_operation(service, window->rebind_client,
                                 window->rebind_request_id, error);
        }
        window->rebind_pending = false;
        window->rebind_client = NULL;
        window->rebind_request_id = 0;
    }
    if (window->lifecycle != GUI_WINDOW_LIFECYCLE_HIDDEN &&
        window->lifecycle != GUI_WINDOW_LIFECYCLE_CLOSING &&
        window->lifecycle != GUI_WINDOW_LIFECYCLE_SUSPENDED) {
        window->lifecycle_before_suspend = window->lifecycle;
        window->lifecycle = GUI_WINDOW_LIFECYCLE_SUSPENDED;
        result = send_service_event(
            service, window, GUI_WINDOW_RENDERING_SUSPENDED,
            GUI_WINDOW_REASON_DISPLAY_LOSS);
        if (result == CORE_OK) {
            result = send_lifecycle(service, window,
                                    GUI_WINDOW_REASON_DISPLAY_LOSS);
        }
        if (result == CORE_OK) result = send_visibility(service, window);
    }
    if (window->scene_bound && !window->scene_operation_pending) {
        window->scene_failed_pending_remove = true;
        if (result == CORE_OK) {
            result = scene_queue_step(service, window,
                                      WINDOW_SCENE_STEP_REMOVE);
        }
        return result;
    }
    window->scene_bound = false;
    window->scene_operation_pending = false;
    window->pending_scene_step = WINDOW_SCENE_STEP_NONE;
    window->pending_scene_request_id = 0;
    if (result != CORE_OK) return result;
    return CORE_OK;
}

static ErrorCode scene_handle_result(WindowServiceHandle service,
                                     const GuiBasicResult *result)
{
    WindowRecord *window = find_scene_request_window(
        service, result->request_id);
    WindowSceneStep completed;
    WindowSceneStep next = WINDOW_SCENE_STEP_NONE;

    if (window == NULL) return CORE_OK;
    completed = window->pending_scene_step;
    window->scene_operation_pending = false;
    window->pending_scene_step = WINDOW_SCENE_STEP_NONE;
    window->pending_scene_request_id = 0;

    if (result->error != CORE_OK) {
        if (window->destroy_pending) {
            if (completed != WINDOW_SCENE_STEP_REMOVE && window->scene_bound) {
                return scene_queue_step(service, window,
                                        WINDOW_SCENE_STEP_REMOVE);
            }
            if (completed == WINDOW_SCENE_STEP_REMOVE) {
                window->scene_bound = true;
                return fail_destroy(service, window, result->error);
            }
            return fail_destroy(service, window, result->error);
        }
        if (completed == WINDOW_SCENE_STEP_REMOVE) {
            if (window->create_failed_pending_remove) {
                return scene_queue_step(service, window,
                                        WINDOW_SCENE_STEP_REMOVE);
            }
            if (window->rebind_failed_pending_remove) {
                return scene_queue_step(service, window,
                                        WINDOW_SCENE_STEP_REMOVE);
            }
            if (window->scene_failed_pending_remove) {
                return scene_queue_step(service, window,
                                        WINDOW_SCENE_STEP_REMOVE);
            }
        }
        return scene_fail_window(service, window, result->error);
    }

    if (completed == WINDOW_SCENE_STEP_BIND) {
        window->scene_bound = true;
        next = WINDOW_SCENE_STEP_GEOMETRY;
    } else if (completed == WINDOW_SCENE_STEP_GEOMETRY) {
        next = WINDOW_SCENE_STEP_STACK;
    } else if (completed == WINDOW_SCENE_STEP_STACK) {
        next = WINDOW_SCENE_STEP_OUTPUT;
    } else if (completed == WINDOW_SCENE_STEP_OUTPUT) {
        next = WINDOW_SCENE_STEP_VISIBILITY;
    } else if (completed == WINDOW_SCENE_STEP_VISIBILITY) {
        if (window->create_pending) {
            (void)send_create_result(
                service, window->create_client, window->create_request_id,
                CORE_OK, window->window_id);
            window->create_pending = false;
            window->create_client = NULL;
            window->create_request_id = 0;
        }
        if (window->rebind_pending) {
            if (window->rebind_client != NULL &&
                window->rebind_client->connection != NULL) {
                (void)send_operation(service, window->rebind_client,
                                     window->rebind_request_id, CORE_OK);
            }
            window->rebind_pending = false;
            window->rebind_client = NULL;
            window->rebind_request_id = 0;
            window->rebind_surface_id = 0;
        }
        next = WINDOW_SCENE_STEP_NONE;
    } else if (completed == WINDOW_SCENE_STEP_REMOVE) {
        window->scene_bound = false;
        if (window->create_failed_pending_remove) {
            (void)send_create_result(
                service, window->create_client, window->create_request_id,
                window->create_failure_error, 0);
            clear_window(window);
            return CORE_OK;
        }
        if (window->destroy_pending) {
            return complete_destroy(service, window, CORE_OK);
        }
        if (window->rebind_failed_pending_remove) {
            if (window->rebind_client != NULL &&
                window->rebind_client->connection != NULL) {
                (void)send_operation(service, window->rebind_client,
                                     window->rebind_request_id,
                                     window->rebind_failure_error);
            }
            window->rebind_failed_pending_remove = false;
            window->rebind_pending = false;
            window->rebind_client = NULL;
            window->rebind_request_id = 0;
        } else if (window->scene_failed_pending_remove) {
            window->scene_failed_pending_remove = false;
        } else if (window->rebind_pending) {
            usize index;
            if (window->lifecycle == GUI_WINDOW_LIFECYCLE_SUSPENDED &&
                window->lifecycle_before_suspend != 0) {
                window->lifecycle = window->lifecycle_before_suspend;
                window->lifecycle_before_suspend = 0;
            }
            window->surface_id = window->rebind_surface_id;
            for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
                window->surface_bind_token[index] =
                    window->rebind_surface_bind_token[index];
            }
            next = WINDOW_SCENE_STEP_BIND;
        } else if (window->active) {
            next = WINDOW_SCENE_STEP_BIND;
        }
    }

    if (window->destroy_pending && window->scene_bound &&
        next != WINDOW_SCENE_STEP_REMOVE) {
        next = WINDOW_SCENE_STEP_REMOVE;
    }
    if (next != WINDOW_SCENE_STEP_NONE && window->active) {
        return scene_queue_step(service, window, next);
    }
    if (window->active && window->scene_policy_dirty &&
        !window->destroy_pending && !window->create_failed_pending_remove &&
        !window->rebind_failed_pending_remove &&
        !window->scene_failed_pending_remove) {
        return apply_scene_policy(service, window);
    }
    return CORE_OK;
}

static ErrorCode compositor_handle_message(WindowServiceHandle service,
                                           IpcMessage *message)
{
    if (message->type == GUI_COMPOSITOR_BIND_CONTROLLER_RESULT) {
        GuiBasicResult result;
        ErrorCode code = gui_decode_basic_result(
            message, GUI_COMPOSITOR_BIND_CONTROLLER_RESULT, &result);
        if (code != CORE_OK) return code;
        if (result.error != CORE_OK) {
            compositor_suspend(service);
            return CORE_OK;
        }
        service->compositor_state = WINDOW_COMPOSITOR_READY;
        reconnect_reset_backoff(&service->compositor_reconnect_delay_ticks,
                                &service->compositor_reconnect_wait_ticks);
        return restore_windows_after_compositor_bind(service);
    }
    if (message->type == GUI_SCENE_RESULT) {
        GuiBasicResult result;
        ErrorCode code = gui_decode_basic_result(
            message, GUI_SCENE_RESULT, &result);
        if (code != CORE_OK) return code;
        return scene_handle_result(service, &result);
    }
    if (message->type == GUI_SCENE_BINDING_LOST) {
        GuiSceneBindingLostEvent event;
        WindowRecord *window;
        ErrorCode result = gui_decode_scene_binding_lost(message, &event);
        if (result != CORE_OK) return result;
        window = find_window(service, event.window_id);
        if (window != NULL && window->surface_id == event.surface_id) {
            if (service->touch_window_id == window->window_id) {
                (void)cancel_active_touch(service);
            }
            window->scene_bound = false;
            window->scene_policy_dirty = true;
            if (window->lifecycle != GUI_WINDOW_LIFECYCLE_SUSPENDED) {
                window->lifecycle_before_suspend = window->lifecycle;
            }
            window->lifecycle = GUI_WINDOW_LIFECYCLE_SUSPENDED;
            (void)send_service_event(
                service, window, GUI_WINDOW_RENDERING_SUSPENDED,
                GUI_WINDOW_REASON_DISPLAY_LOSS);
            (void)send_lifecycle(service, window,
                                  GUI_WINDOW_REASON_DISPLAY_LOSS);
            (void)send_visibility(service, window);
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
            if (service->compositor_connection == NULL) break;
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
    WindowIoState previous_state = service->io_state;

    if (service->io_connection != NULL) {
        (void)eventloop_remove_source(service->loop, service->io_source);
        (void)ipc_connection_close(&service->io_connection);
    }
    service->io_source = 0;
    service->io_state = WINDOW_IO_SUSPENDED;
    if (service->has_io_endpoint) {
        if (previous_state == WINDOW_IO_READY) {
            reconnect_reset_backoff(&service->io_reconnect_delay_ticks,
                                    &service->io_reconnect_wait_ticks);
        } else {
            reconnect_schedule_retry(&service->io_reconnect_delay_ticks,
                                     &service->io_reconnect_wait_ticks);
        }
    }
    (void)cancel_active_touch(service);
    service->io_touch_down = false;
    service->io_touch_device_id = 0;
    service->io_pointer_buttons = 0;
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

static bool key_is_modifier(u16 code, GuiModifierMask *out_modifier)
{
    if (code == LINUX_KEY_LEFTSHIFT || code == LINUX_KEY_RIGHTSHIFT) {
        *out_modifier = GUI_MODIFIER_SHIFT;
        return true;
    }
    if (code == LINUX_KEY_LEFTCTRL || code == LINUX_KEY_RIGHTCTRL) {
        *out_modifier = GUI_MODIFIER_CONTROL;
        return true;
    }
    if (code == LINUX_KEY_LEFTALT || code == LINUX_KEY_RIGHTALT) {
        *out_modifier = GUI_MODIFIER_ALT;
        return true;
    }
    if (code == LINUX_KEY_LEFTMETA || code == LINUX_KEY_RIGHTMETA) {
        *out_modifier = GUI_MODIFIER_SUPER;
        return true;
    }
    if (code == LINUX_KEY_CAPSLOCK) {
        *out_modifier = GUI_MODIFIER_CAPS_LOCK;
        return true;
    }
    if (code == LINUX_KEY_NUMLOCK) {
        *out_modifier = GUI_MODIFIER_NUM_LOCK;
        return true;
    }
    *out_modifier = 0;
    return false;
}

static void update_modifier_state(WindowServiceHandle service, u16 code,
                                  i32 value)
{
    GuiModifierMask modifier;

    if (!key_is_modifier(code, &modifier)) return;
    if (value != 0) {
        service->io_modifiers |= modifier;
    } else {
        service->io_modifiers &= ~modifier;
    }
}

static ErrorCode input_timestamp(MonotonicTime *out_time)
{
    return time_monotonic_now(out_time);
}

static u32 viewport_physical_limit(WindowServiceHandle service, bool x_axis)
{
    bool rotated = service->orientation == GUI_ORIENTATION_RIGHT ||
                   service->orientation == GUI_ORIENTATION_LEFT;
    bool logical_x_axis = rotated ? !x_axis : x_axis;
    u32 logical = logical_x_axis ? service->viewport_width :
                                   service->viewport_height;
    u64 scaled;

    if (logical == 0) return 0;
    scaled = ((u64)(logical - 1) * (u64)service->scale) /
             (u64)GUI_SCALE_1X;
    return scaled > 0xffffffffULL ? 0xffffffffU : (u32)scaled;
}

static u32 input_axis_to_raw(WindowServiceHandle service, i32 value,
                             bool x_axis)
{
    u32 max = viewport_physical_limit(service, x_axis);

    if (value <= 0) return 0;
    if ((u32)value > max) return max;
    return (u32)value;
}

static u32 clamp_u32(u32 value, u32 min, u32 max)
{
    if (max < min) max = min;
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static u32 physical_to_logical(WindowServiceHandle service, u32 value)
{
    u64 scaled;

    if (service->scale == 0) return value;
    scaled = ((u64)value * (u64)GUI_SCALE_1X) / (u64)service->scale;
    return scaled > 0xffffffffULL ? 0xffffffffU : (u32)scaled;
}

static void normalize_input_point(WindowServiceHandle service, u32 raw_x,
                                  u32 raw_y, i32 *out_x, i32 *out_y)
{
    const u32 fixed_point_limit = 0x7fffffffU >> 8;
    u32 x = physical_to_logical(service, raw_x);
    u32 y = physical_to_logical(service, raw_y);
    u32 nx = x;
    u32 ny = y;
    u32 max_x = service->viewport_width == 0 ? 0 :
                service->viewport_width - 1;
    u32 max_y = service->viewport_height == 0 ? 0 :
                service->viewport_height - 1;

    if (service->orientation == GUI_ORIENTATION_RIGHT) {
        nx = y > max_x ? 0 : max_x - y;
        ny = x;
    } else if (service->orientation == GUI_ORIENTATION_INVERTED) {
        nx = x > max_x ? 0 : max_x - x;
        ny = y > max_y ? 0 : max_y - y;
    } else if (service->orientation == GUI_ORIENTATION_LEFT) {
        nx = y;
        ny = x > max_y ? 0 : max_y - x;
    }

    nx = clamp_u32(nx, 0, max_x);
    ny = clamp_u32(ny, 0, max_y);
    nx = clamp_u32(nx, 0, fixed_point_limit);
    ny = clamp_u32(ny, 0, fixed_point_limit);
    *out_x = (i32)(nx << 8);
    *out_y = (i32)(ny << 8);
}

static ErrorCode shell_key_target(WindowServiceHandle service,
                                  WindowRecord **out_window)
{
    usize index;
    WindowRecord *best = NULL;

    if (out_window == NULL) return CORE_INVALID_ARGUMENT;
    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        WindowRecord *window = &service->windows[index];
        if (!window->active || window->owner != service->shell_client ||
            window->owner == NULL || window->owner->connection == NULL ||
            window->lifecycle == GUI_WINDOW_LIFECYCLE_HIDDEN ||
            window->lifecycle == GUI_WINDOW_LIFECYCLE_CLOSING ||
            window->lifecycle == GUI_WINDOW_LIFECYCLE_SUSPENDED) {
            continue;
        }
        if (window->role == GUI_WINDOW_ROLE_LOCK ||
            window->role == GUI_WINDOW_ROLE_SYSTEM_OVERLAY ||
            window->role == GUI_WINDOW_ROLE_SYSTEM_BAR ||
            window->role == GUI_WINDOW_ROLE_HOME) {
            if (best == NULL || window->window_id > best->window_id) {
                best = window;
            }
        }
    }
    *out_window = best;
    return best == NULL ? CORE_NOT_FOUND : CORE_OK;
}

static ErrorCode send_shell_key(WindowServiceHandle service, u16 code,
                                GuiKeyPhase phase, MonotonicTime now)
{
    WindowRecord *target;
    GuiWindowKeyEvent event;
    u8 payload[WINDOW_SERVICE_RESPONSE_MAX];
    usize size;
    ErrorCode result = shell_key_target(service, &target);

    if (result != CORE_OK) return CORE_OK;
    event.window_id = target->window_id;
    event.input_serial = next_input_serial(service);
    event.timestamp_ms = now;
    event.key_code = code;
    event.phase = phase;
    event.modifiers = service->io_modifiers;
    result = gui_encode_window_key(
        &event, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    (void)queue_payload(service, target->owner, GUI_WINDOW_KEY, payload, size);
    return CORE_OK;
}

static ErrorCode route_io_key(WindowServiceHandle service, u16 code, i32 value)
{
    GuiWindowKeyEvent event;
    MonotonicTime now;
    ErrorCode result;

    if (value != 0 && value != 1 && value != 2) return CORE_OK;
    result = input_timestamp(&now);
    if (result != CORE_OK) return result;
    update_modifier_state(service, code, value);
    if (key_is_shell_reserved(code)) {
        if (code == LINUX_KEY_HOME && value == 1) {
            WindowRecord *home = find_home_window(service);
            if (home != NULL) {
                result = activate_window(service, home);
                if (result != CORE_OK) return result;
            }
        }
        return send_shell_key(service, code, value == 0 ? GUI_KEY_UP :
                              value == 2 ? GUI_KEY_REPEAT : GUI_KEY_DOWN,
                              now);
    }
    event.window_id = 0;
    event.input_serial = next_input_serial(service);
    event.timestamp_ms = now;
    event.key_code = code;
    event.phase = value == 0 ? GUI_KEY_UP :
                  value == 2 ? GUI_KEY_REPEAT : GUI_KEY_DOWN;
    event.modifiers = service->io_modifiers;
    return window_service_route_key(service, &event);
}

static ErrorCode route_io_touch_contact(WindowServiceHandle service,
                                        bool down)
{
    GuiWindowTouchEvent event;
    MonotonicTime now;
    ErrorCode result;
    i32 x;
    i32 y;

    if (down == service->io_touch_down) return CORE_OK;
    result = input_timestamp(&now);
    if (result != CORE_OK) return result;
    normalize_input_point(service, service->io_touch_x, service->io_touch_y,
                          &x, &y);
    event.window_id = 0;
    event.input_serial = next_input_serial(service);
    event.timestamp_ms = now;
    event.touch_id = 0;
    event.phase = down ? GUI_TOUCH_DOWN : GUI_TOUCH_UP;
    event.x = x;
    event.y = y;
    result = window_service_route_touch(service, &event);
    if (result == CORE_OK) {
        service->io_touch_down = down;
    } else if (!down &&
               (result == CORE_INVALID_STATE || result == CORE_NOT_FOUND)) {
        service->io_touch_down = false;
    }
    return result;
}

static ErrorCode route_io_touch_move(WindowServiceHandle service)
{
    GuiWindowTouchEvent event;
    MonotonicTime now;
    ErrorCode result;
    i32 x;
    i32 y;

    if (!service->io_touch_down) return CORE_OK;
    result = input_timestamp(&now);
    if (result != CORE_OK) return result;
    normalize_input_point(service, service->io_touch_x, service->io_touch_y,
                          &x, &y);
    event.window_id = 0;
    event.input_serial = next_input_serial(service);
    event.timestamp_ms = now;
    event.touch_id = 0;
    event.phase = GUI_TOUCH_MOVE;
    event.x = x;
    event.y = y;
    return window_service_route_touch(service, &event);
}

static ErrorCode route_io_pointer_button(WindowServiceHandle service,
                                         u32 button_mask, bool down)
{
    GuiWindowPointerEvent event;
    MonotonicTime now;
    ErrorCode result;
    i32 x;
    i32 y;

    result = input_timestamp(&now);
    if (result != CORE_OK) return result;
    if (down) {
        service->io_pointer_buttons |= button_mask;
    } else {
        service->io_pointer_buttons &= ~button_mask;
    }
    normalize_input_point(service, (u32)service->io_pointer_x,
                          (u32)service->io_pointer_y, &x, &y);
    event.window_id = 0;
    event.input_serial = next_input_serial(service);
    event.timestamp_ms = now;
    event.pointer_id = 0;
    event.phase = down ? GUI_POINTER_DOWN : GUI_POINTER_UP;
    event.buttons = service->io_pointer_buttons;
    event.modifiers = service->io_modifiers;
    event.x = x;
    event.y = y;
    return window_service_route_pointer(service, &event);
}

static ErrorCode route_io_pointer_move(WindowServiceHandle service,
                                       i32 dx, i32 dy)
{
    GuiWindowPointerEvent event;
    MonotonicTime now;
    ErrorCode result;
    i32 x;
    i32 y;
    u32 max_x = viewport_physical_limit(service, true);
    u32 max_y = viewport_physical_limit(service, false);

    service->io_pointer_x += dx;
    service->io_pointer_y += dy;
    if (service->io_pointer_x < 0) service->io_pointer_x = 0;
    if (service->io_pointer_y < 0) service->io_pointer_y = 0;
    if ((u32)service->io_pointer_x > max_x) {
        service->io_pointer_x = (i32)max_x;
    }
    if ((u32)service->io_pointer_y > max_y) {
        service->io_pointer_y = (i32)max_y;
    }
    result = input_timestamp(&now);
    if (result != CORE_OK) return result;
    normalize_input_point(service, (u32)service->io_pointer_x,
                          (u32)service->io_pointer_y, &x, &y);
    event.window_id = 0;
    event.input_serial = next_input_serial(service);
    event.timestamp_ms = now;
    event.pointer_id = 0;
    event.phase = GUI_POINTER_MOVE;
    event.buttons = service->io_pointer_buttons;
    event.modifiers = service->io_modifiers;
    event.x = x;
    event.y = y;
    return window_service_route_pointer(service, &event);
}

static ErrorCode handle_io_frame(WindowServiceHandle service,
                                 const GuiIoInputFrameEvent *frame)
{
    usize index;
    bool touch_moved = false;
    bool touch_contact_seen = false;
    bool touch_contact_down = false;
    ErrorCode first_error = CORE_OK;

    for (index = 0; index < frame->event_count; ++index) {
        const GuiIoInputEvent *event = &frame->events[index];
        ErrorCode result = CORE_OK;

        if (event->kind == GUI_IO_INPUT_KEY) {
            if (event->code == LINUX_BTN_TOUCH) {
                touch_contact_seen = true;
                touch_contact_down = event->value != 0;
            } else if (event->code == LINUX_BTN_LEFT) {
                result = route_io_pointer_button(service, 1,
                                                 event->value != 0);
            } else if (event->code == LINUX_BTN_RIGHT) {
                result = route_io_pointer_button(service, 2,
                                                 event->value != 0);
            } else if (event->code == LINUX_BTN_MIDDLE) {
                result = route_io_pointer_button(service, 4,
                                                 event->value != 0);
            } else {
                result = route_io_key(service, event->code, event->value);
            }
        } else if (event->kind == GUI_IO_INPUT_ABSOLUTE_AXIS) {
            if (event->code == LINUX_ABS_X ||
                event->code == LINUX_ABS_MT_POSITION_X) {
                service->io_touch_x = input_axis_to_raw(service, event->value,
                                                        true);
                touch_moved = true;
            } else if (event->code == LINUX_ABS_Y ||
                       event->code == LINUX_ABS_MT_POSITION_Y) {
                service->io_touch_y = input_axis_to_raw(service, event->value,
                                                        false);
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
            touch_contact_seen = false;
            touch_moved = false;
        }
        if (result != CORE_OK && result != CORE_NOT_FOUND &&
            result != CORE_PERMISSION_DENIED && first_error == CORE_OK) {
            first_error = result;
        }
    }
    if (touch_contact_seen) {
        ErrorCode result = route_io_touch_contact(service, touch_contact_down);
        if (result != CORE_OK && result != CORE_NOT_FOUND &&
            result != CORE_PERMISSION_DENIED && first_error == CORE_OK) {
            first_error = result;
        }
        if (result == CORE_OK) {
            service->io_touch_device_id = touch_contact_down ?
                frame->device_id : 0;
        }
    } else if (touch_moved && service->io_touch_down) {
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
        if (result.error != CORE_OK) {
            io_suspend(service);
            return CORE_OK;
        }
        service->io_state = WINDOW_IO_READY;
        reconnect_reset_backoff(&service->io_reconnect_delay_ticks,
                                &service->io_reconnect_wait_ticks);
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
        ErrorCode result = gui_decode_io_device_event(message, message->type,
                                                      &event);
        if (result != CORE_OK) return result;
        if (message->type == GUI_IO_DEVICE_REMOVED) {
            if (service->io_touch_down &&
                (service->io_touch_device_id == 0 ||
                 service->io_touch_device_id == event.device_id)) {
                (void)cancel_active_touch(service);
                service->io_touch_down = false;
                service->io_touch_device_id = 0;
            }
            if ((event.classes & GUI_INPUT_DEVICE_POINTER) != 0) {
                service->io_pointer_buttons = 0;
            }
            if ((event.classes & GUI_INPUT_DEVICE_KEYBOARD) != 0) {
                service->io_modifiers = 0;
            }
        }
        return CORE_OK;
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
            if (service->io_connection == NULL) break;
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

static ErrorCode reconnect_dependencies(WindowServiceHandle service)
{
    if (service->has_compositor_endpoint &&
        service->compositor_state == WINDOW_COMPOSITOR_SUSPENDED &&
        service->compositor_connection == NULL) {
        if (service->compositor_reconnect_wait_ticks == 0) {
            ErrorCode result = compositor_begin_connect(
                service, &service->compositor_endpoint);
            if (result != CORE_OK) {
                reconnect_schedule_retry(
                    &service->compositor_reconnect_delay_ticks,
                    &service->compositor_reconnect_wait_ticks);
            }
        }
    }
    if (service->has_io_endpoint &&
        service->io_state == WINDOW_IO_SUSPENDED &&
        service->io_connection == NULL) {
        if (service->io_reconnect_wait_ticks == 0) {
            ErrorCode result = io_begin_connect(service,
                                               &service->io_endpoint);
            if (result != CORE_OK) {
                reconnect_schedule_retry(&service->io_reconnect_delay_ticks,
                                         &service->io_reconnect_wait_ticks);
            }
        }
    }
    return CORE_OK;
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
            (void)destroy_window(service, &service->windows[index],
                                 NULL, 0, 0);
            if (service->windows[index].active &&
                service->windows[index].owner == client) {
                service->windows[index].owner = NULL;
                service->windows[index].create_client = NULL;
                service->windows[index].destroy_client = NULL;
                service->windows[index].rebind_client = NULL;
            }
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
    service->compositor_reconnect_delay_ticks = 1;
    service->io_reconnect_delay_ticks = 1;
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
    if (config->compositor_endpoint != NULL &&
        config->controller_token != NULL) {
        result = string_copy(config->compositor_endpoint,
                             &service->compositor_endpoint);
        if (result != CORE_OK) goto fail;
        service->has_compositor_endpoint = true;
    }
    if (config->io_endpoint != NULL) {
        result = string_copy(config->io_endpoint, &service->io_endpoint);
        if (result != CORE_OK) goto fail;
        service->has_io_endpoint = true;
    }

    result = eventloop_create(&service->loop);
    if (result != CORE_OK) goto fail;
    if (service->has_compositor_endpoint || service->has_io_endpoint) {
        result = eventloop_create_timer(
            service->loop, WINDOW_SERVICE_RECONNECT_INTERVAL_MS,
            WINDOW_SERVICE_RECONNECT_INTERVAL_MS,
            &service->reconnect_source);
        if (result != CORE_OK) goto fail;
    }
    result = ipc_server_create(config->endpoint, &service->server);
    if (result != CORE_OK) goto fail;
    result = ipc_server_get_fd(service->server, &fd);
    if (result != CORE_OK) goto fail;
    result = eventloop_add_fd(service->loop, fd,
        EVENT_READABLE | EVENT_ERROR | EVENT_HANGUP,
        &service->server_source);
    if (result != CORE_OK) goto fail;
    service->compositor_state = WINDOW_COMPOSITOR_DISABLED;
    if (service->has_compositor_endpoint) {
        result = compositor_begin_connect(service, &service->compositor_endpoint);
        if (result != CORE_OK) {
            service->compositor_state = WINDOW_COMPOSITOR_SUSPENDED;
            reconnect_schedule_retry(
                &service->compositor_reconnect_delay_ticks,
                &service->compositor_reconnect_wait_ticks);
        }
    }
    service->io_state = WINDOW_IO_DISABLED;
    if (service->has_io_endpoint) {
        result = io_begin_connect(service, &service->io_endpoint);
        if (result != CORE_OK) {
            service->io_state = WINDOW_IO_SUSPENDED;
            reconnect_schedule_retry(&service->io_reconnect_delay_ticks,
                                     &service->io_reconnect_wait_ticks);
        }
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
    result = reconnect_dependencies(service);
    if (result != CORE_OK) return result;
    result = eventloop_wait(service->loop, events, WINDOW_SERVICE_EVENT_MAX,
                            timeout_ms, &count);
    if (result != CORE_OK) return result;
    for (index = 0; index < count; ++index) {
        WindowClient *client;
        if (events[index].source_id == service->reconnect_source &&
            service->reconnect_source != 0) {
            reconnect_tick(service);
            result = reconnect_dependencies(service);
        } else if (events[index].source_id == service->server_source) {
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
    return reconnect_dependencies(service);
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
    if (handle->has_compositor_endpoint) {
        (void)string_destroy(&handle->compositor_endpoint);
    }
    if (handle->has_io_endpoint) {
        (void)string_destroy(&handle->io_endpoint);
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
    if (service->touch_active &&
        (service->viewport_width != width ||
         service->viewport_height != height ||
         service->scale != scale ||
         service->orientation != orientation)) {
        ErrorCode result = cancel_active_touch(service);
        if (result != CORE_OK) return result;
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

static bool window_can_receive_input(const WindowRecord *window, bool keyboard)
{
    if (window == NULL || !window->active ||
        window->lifecycle != GUI_WINDOW_LIFECYCLE_FOREGROUND ||
        window->owner == NULL || window->owner->connection == NULL) {
        return false;
    }
    if (keyboard &&
        (window->flags & GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD) == 0) {
        return false;
    }
    return true;
}

static bool modal_blocks_active_window(WindowServiceHandle service,
                                       const WindowRecord *window)
{
    if (window->role != GUI_WINDOW_ROLE_DIALOG &&
        window->role != GUI_WINDOW_ROLE_TRANSIENT) {
        return false;
    }
    return window_chain_contains(service, window->modal_parent_id,
                                 service->active_window_id);
}

static WindowRecord *input_target_window(WindowServiceHandle service,
                                         bool keyboard)
{
    WindowRecord *active = find_window(service, service->active_window_id);
    WindowRecord *best = NULL;
    usize index;

    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        WindowRecord *window = &service->windows[index];
        if (window_can_receive_input(window, false) &&
            window->role == GUI_WINDOW_ROLE_LOCK) {
            if (best == NULL || window->window_id > best->window_id) {
                best = window;
            }
        }
    }
    if (best != NULL) {
        if (keyboard &&
            (best->flags & GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD) == 0) {
            return NULL;
        }
        return best;
    }

    best = NULL;
    for (index = 0; index < WINDOW_SERVICE_WINDOW_MAX; ++index) {
        WindowRecord *window = &service->windows[index];
        if (window_can_receive_input(window, false) &&
            modal_blocks_active_window(service, window)) {
            if (best == NULL || window->window_id > best->window_id) {
                best = window;
            }
        }
    }
    if (best != NULL) {
        if (keyboard &&
            (best->flags & GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD) == 0) {
            return NULL;
        }
        return best;
    }

    if (!window_can_receive_input(active, keyboard)) return NULL;
    return active;
}

static ErrorCode cancel_touch_if_input_target_changed(
    WindowServiceHandle service)
{
    WindowRecord *target;

    if (!service->touch_active) return CORE_OK;
    target = input_target_window(service, false);
    if (target != NULL && target->window_id == service->touch_window_id) {
        return CORE_OK;
    }
    return cancel_active_touch(service);
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
        window = input_target_window(service, false);
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
    window = input_target_window(service, false);
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
    window = input_target_window(service, true);
    if (window == NULL) return CORE_NOT_FOUND;
    routed = *event;
    routed.window_id = window->window_id;
    result = gui_encode_window_key(
        &routed, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    return queue_payload(service, window->owner, GUI_WINDOW_KEY,
                         payload, size);
}
