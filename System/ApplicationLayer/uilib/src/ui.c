#include <uilib/ui.h>
#include <punos/syscall/syscall.h>
#include <special/ipc.h>
#include <sys/memory.h>

#define COMPOSITOR_ENDPOINT "/run/punos/compositorserv.sock"
#define WINDOW_ENDPOINT "/run/punos/windowserv.sock"
#define RECONNECT_INTERVAL_MS ((DurationMs)250)

typedef struct UiConnection {
    struct UiContextHandle *context;
    IpcConnectionHandle ipc;
    AppEventSourceId source;
    bool open;
} UiConnection;

typedef enum UiViewState {
    UI_VIEW_WAITING_CONNECTIONS = 0,
    UI_VIEW_CREATING_SURFACE = 1,
    UI_VIEW_CREATING_WINDOW = 2,
    UI_VIEW_READY = 3,
    UI_VIEW_REBINDING = 4,
    UI_VIEW_FAILED = 5
} UiViewState;

typedef enum UiBufferState {
    UI_BUFFER_EMPTY = 0,
    UI_BUFFER_IMPORTING = 1,
    UI_BUFFER_FREE = 2,
    UI_BUFFER_ACQUIRED = 3,
    UI_BUFFER_SUBMITTED = 4
} UiBufferState;

struct UiBufferHandle {
    struct UiViewHandle *view;
    SharedMemory memory;
    UiBufferState state;
    u64 request_id;
    u64 buffer_id;
};

struct UiViewHandle {
    MemoryBlock block;
    struct UiContextHandle *context;
    struct UiViewHandle *next;
    UiEventHandler handler;
    void *handler_context;
    const char *title;
    u16 title_length;
    UiViewRole role;
    UiViewFlags flags;
    struct UiViewHandle *parent;
    UiViewState state;
    u64 request_id;
    u64 surface_id;
    u8 surface_token[GUI_SURFACE_BIND_TOKEN_SIZE];
    u64 window_id;
    bool configured;
    u32 width;
    u32 height;
    u32 stride;
    u64 configure_serial;
    u64 commit_serial;
    u64 pending_commit_request;
    GuiWindowConfigureEvent latest_configure;
    bool has_configure;
    u16 damage_count;
    GuiRect damage[GUI_MAX_DAMAGE_RECTS];
    struct UiBufferHandle buffers[UI_BUFFER_COUNT_MAX];
};

struct UiContextHandle {
    MemoryBlock block;
    AppHandle app;
    u32 buffer_count;
    bool stopping;
    u64 next_request_id;
    AppEventSourceId reconnect_source;
    struct UiViewHandle *views;
    UiConnection compositor;
    UiConnection window;
};

static ErrorCode start_waiting_views(struct UiContextHandle *context);
static ErrorCode dispatch_message(UiConnection *connection,
                                  const IpcMessage *message);
static void suspend_connection(UiConnection *connection);

static void keep_first(ErrorCode *result, ErrorCode error)
{
    if (*result == CORE_OK && error != CORE_OK) *result = error;
}

static void emit(struct UiViewHandle *view, UiEvent event)
{
    if (view->handler != NULL) view->handler(view, &event, view->handler_context);
}

static u64 next_request(struct UiContextHandle *context)
{
    context->next_request_id++;
    if (context->next_request_id == 0) context->next_request_id++;
    return context->next_request_id;
}

static struct UiViewHandle *find_request(struct UiContextHandle *context,
                                         u64 request_id, UiViewState state)
{
    struct UiViewHandle *view = context->views;
    while (view != NULL) {
        if (view->request_id == request_id && view->state == state) return view;
        view = view->next;
    }
    return NULL;
}

static struct UiViewHandle *find_window(struct UiContextHandle *context,
                                        u64 window_id)
{
    struct UiViewHandle *view = context->views;
    while (view != NULL) {
        if (view->window_id == window_id && view->state == UI_VIEW_READY) return view;
        view = view->next;
    }
    return NULL;
}

static ErrorCode queue_message(UiConnection *connection, IpcMessageType type,
                               u8 *payload, usize payload_size)
{
    IpcMessage message = { type, { payload, payload_size } };
    bool pending;
    ErrorCode result;

    if (!connection->open) return CORE_WOULD_BLOCK;
    result = ipc_connection_send(connection->ipc, &message);
    if (result == CORE_OK) result = ipc_connection_flush(connection->ipc, &pending);
    if (result == CORE_OK) {
        result = app_event_update_fd(connection->context->app, connection->source,
            EVENT_READABLE | (pending ? EVENT_WRITABLE : 0));
    }
    return result;
}

static ErrorCode queue_descriptor(UiConnection *connection,
                                  IpcMessageType type, u8 *payload,
                                  usize payload_size, i32 descriptor)
{
    IpcMessage message = { type, { payload, payload_size } };
    bool pending;
    ErrorCode result;

    if (!connection->open) return CORE_WOULD_BLOCK;
    result = ipc_connection_send_with_descriptor(connection->ipc, &message,
                                                 descriptor);
    if (result == CORE_OK) result = ipc_connection_flush(connection->ipc, &pending);
    if (result == CORE_OK) {
        result = app_event_update_fd(connection->context->app, connection->source,
            EVENT_READABLE | (pending ? EVENT_WRITABLE : 0));
    }
    return result;
}

static struct UiBufferHandle *find_buffer_request(
    struct UiContextHandle *context, u64 request_id)
{
    for (struct UiViewHandle *view = context->views; view != NULL;
         view = view->next) {
        for (u32 index = 0; index < context->buffer_count; ++index) {
            struct UiBufferHandle *buffer = &view->buffers[index];
            if (buffer->state == UI_BUFFER_IMPORTING &&
                buffer->request_id == request_id) return buffer;
        }
    }
    return NULL;
}

static struct UiBufferHandle *find_buffer_id(struct UiContextHandle *context,
                                              u64 buffer_id)
{
    for (struct UiViewHandle *view = context->views; view != NULL;
         view = view->next) {
        for (u32 index = 0; index < context->buffer_count; ++index) {
            struct UiBufferHandle *buffer = &view->buffers[index];
            if (buffer->buffer_id == buffer_id && buffer_id != 0) return buffer;
        }
    }
    return NULL;
}

static bool view_has_free_buffer(struct UiViewHandle *view)
{
    for (u32 index = 0; index < view->context->buffer_count; ++index)
        if (view->buffers[index].state == UI_BUFFER_FREE) return true;
    return false;
}

static ErrorCode import_buffer(struct UiViewHandle *view,
                               struct UiBufferHandle *buffer)
{
    u8 payload[64];
    usize payload_size = 0;
    usize byte_size;
    GuiBufferImportRequest request = { 0 };
    ErrorCode result = core_size_multiply(view->stride, view->height, &byte_size);

    if (result != CORE_OK) return result;
    result = memory_shared_create(byte_size, &buffer->memory);
    if (result != CORE_OK) return result;
    buffer->view = view;
    request.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION,
                                        next_request(view->context) };
    request.width = view->width;
    request.height = view->height;
    request.stride = view->stride;
    request.pixel_format = GUI_PIXEL_ARGB8888;
    request.byte_size = byte_size;
    result = gui_encode_buffer_import(&request,
        (Bytes){ payload, sizeof(payload) }, &payload_size);
    if (result == CORE_OK) result = queue_descriptor(
        &view->context->compositor, GUI_SURFACE_BUFFER_IMPORT,
        payload, payload_size, buffer->memory.fd);
    if (result != CORE_OK) {
        (void)memory_shared_close(&buffer->memory);
        return result;
    }
    buffer->request_id = request.prefix.request_id;
    buffer->state = UI_BUFFER_IMPORTING;
    return CORE_OK;
}

static ErrorCode create_buffer_pool(struct UiViewHandle *view)
{
    usize stride;
    ErrorCode result;

    if (view->configured) return CORE_OK;
    result = core_size_multiply(view->width, 4, &stride);
    if (result != CORE_OK || stride > (usize)(~(u32)0))
        return result != CORE_OK ? result : CORE_OVERFLOW;
    view->stride = (u32)stride;
    for (u32 index = 0; index < view->context->buffer_count; ++index) {
        result = import_buffer(view, &view->buffers[index]);
        if (result != CORE_OK) return result;
    }
    view->configured = true;
    return CORE_OK;
}

static void destroy_buffer_pool(struct UiViewHandle *view, bool notify_service)
{
    for (u32 index = 0; index < view->context->buffer_count; ++index) {
        struct UiBufferHandle *buffer = &view->buffers[index];
        if (notify_service && buffer->buffer_id != 0 &&
            view->context->compositor.open) {
            u8 payload[32];
            usize size;
            GuiBufferIdRequest request = {
                { GUI_PROTOCOL_VERSION, next_request(view->context) },
                buffer->buffer_id
            };
            if (gui_encode_buffer_id_request(
                    &request, (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK)
                (void)queue_message(&view->context->compositor,
                    GUI_SURFACE_BUFFER_DESTROY, payload, size);
        }
        if (buffer->memory.size != 0) (void)memory_shared_close(&buffer->memory);
        *buffer = (struct UiBufferHandle){ 0 };
    }
    view->configured = false;
    view->damage_count = 0;
    view->pending_commit_request = 0;
}

static ErrorCode begin_surface(struct UiViewHandle *view)
{
    u8 payload[16];
    usize size = 0;
    GuiRequestPrefix prefix = { GUI_PROTOCOL_VERSION,
                                next_request(view->context) };
    ErrorCode result = gui_encode_versioned_request(
        prefix, (Bytes){ payload, sizeof(payload) }, &size);
    if (result == CORE_OK) {
        result = queue_message(&view->context->compositor,
                               GUI_SURFACE_CREATE, payload, size);
    }
    if (result == CORE_OK) {
        view->request_id = prefix.request_id;
        view->state = UI_VIEW_CREATING_SURFACE;
    }
    return result;
}

static ErrorCode begin_window(struct UiViewHandle *view)
{
    u8 payload[GUI_WINDOW_TITLE_MAX + 64];
    usize size = 0;
    GuiWindowCreateRequest request = { 0 };
    ErrorCode result;

    request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    request.prefix.request_id = next_request(view->context);
    request.surface_id = view->surface_id;
    for (usize i = 0; i < GUI_SURFACE_BIND_TOKEN_SIZE; ++i)
        request.surface_bind_token[i] = view->surface_token[i];
    request.role = view->role;
    request.parent_id = view->parent == NULL ? 0 : view->parent->window_id;
    request.flags = view->flags;
    request.title_length = view->title_length;
    result = memory_copy((Bytes){ request.title, request.title_length },
                         (Bytes){ (u8 *)view->title, view->title_length });
    if (result == CORE_OK) {
        result = gui_encode_window_create(&request,
            (Bytes){ payload, sizeof(payload) }, &size);
    }
    if (result == CORE_OK) {
        result = queue_message(&view->context->window,
                               GUI_WINDOW_CREATE, payload, size);
    }
    if (result == CORE_OK) {
        view->request_id = request.prefix.request_id;
        view->state = UI_VIEW_CREATING_WINDOW;
    }
    return result;
}

static ErrorCode begin_rebind(struct UiViewHandle *view)
{
    u8 payload[64];
    usize size;
    GuiWindowSurfaceRebindRequest request = { 0 };
    ErrorCode result;

    request.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION,
                                        next_request(view->context) };
    request.window_id = view->window_id;
    request.surface_id = view->surface_id;
    for (usize index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index)
        request.surface_bind_token[index] = view->surface_token[index];
    result = gui_encode_window_surface_rebind(
        &request, (Bytes){ payload, sizeof(payload) }, &size);
    if (result == CORE_OK) result = queue_message(
        &view->context->window, GUI_WINDOW_SURFACE_REBIND, payload, size);
    if (result == CORE_OK) {
        view->request_id = request.prefix.request_id;
        view->state = UI_VIEW_REBINDING;
    }
    return result;
}

static void fail_view(struct UiViewHandle *view, ErrorCode error)
{
    view->state = UI_VIEW_FAILED;
    emit(view, (UiEvent){ .kind = UI_EVENT_SERVICE_ERROR, .error = error });
}

static ErrorCode start_waiting_views(struct UiContextHandle *context)
{
    struct UiViewHandle *view;
    ErrorCode result = CORE_OK;
    if (!context->compositor.open || !context->window.open) return CORE_OK;
    for (view = context->views; view != NULL; view = view->next) {
        if (view->state == UI_VIEW_WAITING_CONNECTIONS) {
            ErrorCode error;
            if (view->surface_id == 0) error = begin_surface(view);
            else if (view->window_id == 0) error = begin_window(view);
            else error = begin_rebind(view);
            if (error != CORE_OK && error != CORE_WOULD_BLOCK) fail_view(view, error);
            if (result == CORE_OK && error != CORE_WOULD_BLOCK) result = error;
        }
    }
    return result;
}

static ErrorCode dispatch_compositor(struct UiContextHandle *context,
                                     const IpcMessage *message)
{
    if (message->type == GUI_SURFACE_COMMIT_RESULT) {
        GuiBasicResult response;
        ErrorCode result = gui_decode_basic_result(
            message, GUI_SURFACE_COMMIT_RESULT, &response);
        if (result != CORE_OK) return result;
        for (struct UiViewHandle *view = context->views; view != NULL;
             view = view->next) {
            if (view->pending_commit_request != response.request_id) continue;
            view->pending_commit_request = 0;
            if (response.error != CORE_OK) {
                fail_view(view, response.error);
                return CORE_OK;
            }
            if (view->configure_serial != 0 && context->window.open) {
                u8 payload[32];
                usize size;
                GuiWindowAckConfigureRequest ack = {
                    { GUI_PROTOCOL_VERSION, next_request(context) },
                    view->window_id, view->configure_serial
                };
                result = gui_encode_window_ack_configure(
                    &ack, (Bytes){ payload, sizeof(payload) }, &size);
                if (result == CORE_OK) result = queue_message(
                    &context->window, GUI_WINDOW_ACK_CONFIGURE, payload, size);
                if (result == CORE_OK) view->configure_serial = 0;
                return result;
            }
            return CORE_OK;
        }
        return CORE_OK;
    }
    if (message->type == GUI_SURFACE_BUFFER_IMPORT_RESULT) {
        GuiBufferImportResult response;
        struct UiBufferHandle *buffer;
        ErrorCode result = gui_decode_buffer_import_result(message, &response);
        if (result != CORE_OK) return result;
        buffer = find_buffer_request(context, response.result.request_id);
        if (buffer == NULL) return CORE_OK;
        if (response.result.error != CORE_OK) {
            buffer->state = UI_BUFFER_EMPTY;
            (void)memory_shared_close(&buffer->memory);
            fail_view(buffer->view, response.result.error);
        } else {
            buffer->buffer_id = response.buffer_id;
            buffer->state = UI_BUFFER_FREE;
            if (view_has_free_buffer(buffer->view))
                emit(buffer->view, (UiEvent){ .kind = UI_EVENT_BUFFER_AVAILABLE,
                                              .error = CORE_OK });
        }
        return CORE_OK;
    }
    if (message->type == GUI_SURFACE_BUFFER_RELEASED) {
        GuiBufferReleasedEvent event;
        struct UiBufferHandle *buffer;
        ErrorCode result = gui_decode_buffer_released(message, &event);
        if (result != CORE_OK) return result;
        buffer = find_buffer_id(context, event.buffer_id);
        if (buffer != NULL && buffer->state == UI_BUFFER_SUBMITTED) {
            buffer->state = UI_BUFFER_FREE;
            emit(buffer->view, (UiEvent){ .kind = UI_EVENT_BUFFER_AVAILABLE,
                                          .error = CORE_OK });
        }
        return CORE_OK;
    }
    if (message->type == GUI_SURFACE_FRAME_DONE) {
        GuiSurfaceFrameDoneEvent event;
        struct UiViewHandle *view;
        ErrorCode result = gui_decode_surface_frame_done(message, &event);
        if (result != CORE_OK) return result;
        view = context->views;
        while (view != NULL && view->surface_id != event.surface_id)
            view = view->next;
        if (view != NULL) emit(view, (UiEvent){ .kind = UI_EVENT_FRAME_DONE,
                                               .error = CORE_OK,
                                               .frame = event });
        return CORE_OK;
    }
    if (message->type == GUI_SURFACE_FULL_REDRAW) {
        GuiSurfaceRedrawEvent event;
        struct UiViewHandle *view;
        ErrorCode result = gui_decode_surface_redraw(message, &event);
        if (result != CORE_OK) return result;
        view = context->views;
        while (view != NULL && view->surface_id != event.surface_id)
            view = view->next;
        if (view != NULL) emit(view, (UiEvent){ .kind = UI_EVENT_REDRAW,
                                               .error = CORE_OK,
                                               .redraw = event });
        return CORE_OK;
    }
    GuiSurfaceCreateResult response;
    struct UiViewHandle *view;
    ErrorCode result;

    if (message->type != GUI_SURFACE_CREATE_RESULT) return CORE_OK;
    result = gui_decode_surface_create_result(message, &response);
    if (result != CORE_OK) return result;
    view = find_request(context, response.result.request_id,
                        UI_VIEW_CREATING_SURFACE);
    if (view == NULL) return CORE_OK; /* response to a locally cancelled view */
    if (response.result.error != CORE_OK) {
        fail_view(view, response.result.error);
        return CORE_OK;
    }
    view->surface_id = response.surface_id;
    for (usize i = 0; i < GUI_SURFACE_BIND_TOKEN_SIZE; ++i)
        view->surface_token[i] = response.surface_bind_token[i];
    result = view->window_id == 0 ? begin_window(view) : begin_rebind(view);
    if (result != CORE_OK) fail_view(view, result);
    return result;
}

static ErrorCode dispatch_window(struct UiContextHandle *context,
                                 const IpcMessage *message)
{
    if (message->type == GUI_WINDOW_OPERATION_RESULT) {
        GuiBasicResult response;
        struct UiViewHandle *view;
        ErrorCode result = gui_decode_basic_result(
            message, GUI_WINDOW_OPERATION_RESULT, &response);
        if (result != CORE_OK) return result;
        view = find_request(context, response.request_id, UI_VIEW_REBINDING);
        if (view == NULL) return CORE_OK;
        if (response.error != CORE_OK) fail_view(view, response.error);
        else {
            view->state = UI_VIEW_READY;
            if (view->has_configure && !view->configured) {
                view->width = view->latest_configure.width;
                view->height = view->latest_configure.height;
                view->configure_serial = view->latest_configure.configure_serial;
                result = create_buffer_pool(view);
                if (result != CORE_OK) {
                    fail_view(view, result);
                    return CORE_OK;
                }
            }
            emit(view, (UiEvent){ .kind = UI_EVENT_REDRAW, .error = CORE_OK,
                .redraw = { .surface_id = view->surface_id, .reason = 3 } });
        }
        return CORE_OK;
    }
    if (message->type == GUI_WINDOW_CREATE_RESULT) {
        GuiWindowCreateResult response;
        struct UiViewHandle *view;
        ErrorCode result = gui_decode_window_create_result(message, &response);
        if (result != CORE_OK) return result;
        view = find_request(context, response.result.request_id,
                            UI_VIEW_CREATING_WINDOW);
        if (view == NULL) return CORE_OK; /* response to a locally cancelled view */
        if (response.result.error != CORE_OK) {
            fail_view(view, response.result.error);
        } else {
            view->window_id = response.window_id;
            view->state = UI_VIEW_READY;
            emit(view, (UiEvent){ .kind = UI_EVENT_READY, .error = CORE_OK });
        }
        return CORE_OK;
    }
    if (message->type == GUI_WINDOW_CONFIGURE) {
        GuiWindowConfigureEvent value;
        ErrorCode result = gui_decode_window_configure(message, &value);
        struct UiViewHandle *view;
        if (result != CORE_OK) return result;
        view = find_window(context, value.window_id);
        if (view != NULL) {
            view->latest_configure = value;
            view->has_configure = true;
            if (view->configured &&
                (view->width != value.width || view->height != value.height))
                destroy_buffer_pool(view, true);
            if (!view->configured) {
                view->width = value.width;
                view->height = value.height;
                view->configure_serial = value.configure_serial;
                result = create_buffer_pool(view);
                if (result != CORE_OK) fail_view(view, result);
            }
            emit(view, (UiEvent){ .kind = UI_EVENT_CONFIGURE,
                                  .error = result,
                                  .configure = value });
        }
        return CORE_OK;
    }
    if (message->type == GUI_WINDOW_CLOSE_REQUESTED) {
        GuiWindowCloseEvent value;
        ErrorCode result = gui_decode_window_close_event(message, &value);
        struct UiViewHandle *view;
        if (result != CORE_OK) return result;
        view = find_window(context, value.window_id);
        if (view != NULL) emit(view, (UiEvent){ .kind = UI_EVENT_CLOSE_REQUESTED,
                                               .error = CORE_OK, .close = value });
        return CORE_OK;
    }
    if (message->type == GUI_WINDOW_TOUCH) {
        GuiWindowTouchEvent value;
        ErrorCode result = gui_decode_window_touch(message, &value);
        struct UiViewHandle *view;
        if (result != CORE_OK) return result;
        view = find_window(context, value.window_id);
        if (view != NULL) emit(view, (UiEvent){ .kind = UI_EVENT_TOUCH,
                                               .error = CORE_OK, .touch = value });
        return CORE_OK;
    }
    if (message->type == GUI_WINDOW_POINTER) {
        GuiWindowPointerEvent value;
        ErrorCode result = gui_decode_window_pointer(message, &value);
        struct UiViewHandle *view;
        if (result != CORE_OK) return result;
        view = find_window(context, value.window_id);
        if (view != NULL) emit(view, (UiEvent){ .kind = UI_EVENT_POINTER,
                                               .error = CORE_OK, .pointer = value });
        return CORE_OK;
    }
    if (message->type == GUI_WINDOW_KEY) {
        GuiWindowKeyEvent value;
        ErrorCode result = gui_decode_window_key(message, &value);
        struct UiViewHandle *view;
        if (result != CORE_OK) return result;
        view = find_window(context, value.window_id);
        if (view != NULL) emit(view, (UiEvent){ .kind = UI_EVENT_KEY,
                                               .error = CORE_OK, .key = value });
        return CORE_OK;
    }
    if (message->type == GUI_WINDOW_VISIBILITY_CHANGED) {
        GuiWindowVisibilityEvent value;
        ErrorCode result = gui_decode_window_visibility(message, &value);
        struct UiViewHandle *view;
        if (result != CORE_OK) return result;
        view = find_window(context, value.window_id);
        if (view != NULL) emit(view, (UiEvent){ .kind = UI_EVENT_VISIBILITY,
                                               .error = CORE_OK,
                                               .visibility = value });
        return CORE_OK;
    }
    if (message->type == GUI_WINDOW_LIFECYCLE_CHANGED) {
        GuiWindowLifecycleEvent value;
        ErrorCode result = gui_decode_window_lifecycle(message, &value);
        struct UiViewHandle *view;
        if (result != CORE_OK) return result;
        view = find_window(context, value.window_id);
        if (view != NULL) emit(view, (UiEvent){ .kind = UI_EVENT_LIFECYCLE,
                                               .error = CORE_OK,
                                               .lifecycle = value });
        return CORE_OK;
    }
    if (message->type == GUI_WINDOW_NAVIGATION_REQUESTED) {
        GuiWindowNavigationEvent value;
        ErrorCode result = gui_decode_window_navigation(message, &value);
        struct UiViewHandle *view;
        if (result != CORE_OK) return result;
        view = find_window(context, value.window_id);
        if (view != NULL) emit(view, (UiEvent){ .kind = UI_EVENT_NAVIGATION,
                                               .error = CORE_OK,
                                               .navigation = value });
    }
    return CORE_OK;
}

static ErrorCode dispatch_message(UiConnection *connection,
                                  const IpcMessage *message)
{
    if (connection == &connection->context->compositor)
        return dispatch_compositor(connection->context, message);
    return dispatch_window(connection->context, message);
}

static ErrorCode connection_close(UiConnection *connection)
{
    ErrorCode result = CORE_OK;

    if (!connection->context->stopping && connection->open)
        suspend_connection(connection);
    if (connection->source != 0) {
        keep_first(&result, app_event_remove(connection->context->app,
                                             connection->source));
        connection->source = 0;
    }
    if (connection->ipc != NULL) {
        keep_first(&result, ipc_connection_close(&connection->ipc));
    }
    connection->open = false;
    return result;
}

static void reset_buffers(struct UiViewHandle *view)
{
    destroy_buffer_pool(view, false);
}

static void suspend_connection(UiConnection *connection)
{
    struct UiContextHandle *context = connection->context;
    bool compositor = connection == &context->compositor;

    for (struct UiViewHandle *view = context->views; view != NULL;
         view = view->next) {
        if (view->state == UI_VIEW_FAILED) continue;
        if (compositor) {
            reset_buffers(view);
            view->surface_id = 0;
        } else {
            view->window_id = 0;
        }
        view->request_id = 0;
        view->state = UI_VIEW_WAITING_CONNECTIONS;
        emit(view, (UiEvent){ .kind = UI_EVENT_SERVICE_ERROR,
                              .error = CORE_IO_ERROR });
    }
}

static void connection_event(AppHandle app, AppEventSourceId source,
                             EventMask mask, void *opaque)
{
    UiConnection *connection = opaque;
    bool pending = false;
    ErrorCode result = CORE_OK;

    (void)app;
    (void)source;
    if (connection->context->stopping) return;
    if (!connection->open && (mask & EVENT_WRITABLE) != 0) {
        IpcConnectionState state;

        result = ipc_connection_finish_connect(connection->ipc, &state);
        if (result == CORE_OK && state == IPC_CONNECTION_STATE_OPEN) {
            connection->open = true;
            result = app_event_update_fd(connection->context->app,
                                         connection->source, EVENT_READABLE);
            if (result == CORE_OK) result = start_waiting_views(connection->context);
        } else if (result == CORE_OK && state != IPC_CONNECTION_STATE_CONNECTING) {
            result = CORE_IO_ERROR;
        }
    }
    if (result == CORE_OK && connection->open && (mask & EVENT_WRITABLE) != 0) {
        result = ipc_connection_flush(connection->ipc, &pending);
        if (result == CORE_OK) {
            result = app_event_update_fd(connection->context->app,
                connection->source,
                EVENT_READABLE | (pending ? EVENT_WRITABLE : 0));
        }
    }
    while (result == CORE_OK && connection->open &&
           (mask & EVENT_READABLE) != 0) {
        IpcReceiveStatus status;
        IpcMessage message = { 0 };
        i32 descriptor = IPC_NO_DESCRIPTOR;

        result = ipc_connection_receive_with_descriptor(
            connection->ipc, &status, &message, &descriptor);
        if (result != CORE_OK || status == IPC_RECEIVE_STATUS_NO_MESSAGE) break;
        if (status == IPC_RECEIVE_STATUS_PEER_CLOSED) {
            result = CORE_IO_ERROR;
            break;
        }
        result = dispatch_message(connection, &message);
        if (descriptor != IPC_NO_DESCRIPTOR) (void)close(descriptor);
        keep_first(&result, ipc_message_destroy(&message));
    }
    if (result != CORE_OK) (void)connection_close(connection);
}

static ErrorCode connection_open(struct UiContextHandle *context,
                                 UiConnection *connection,
                                 const char *endpoint_text, usize endpoint_size)
{
    String endpoint = { (char *)endpoint_text, endpoint_size };
    i32 fd;
    ErrorCode result;

    connection->context = context;
    result = ipc_connection_begin_connect(&endpoint, &connection->ipc);
    if (result == CORE_OK) result = ipc_connection_get_fd(connection->ipc, &fd);
    if (result == CORE_OK) {
        result = app_event_add_fd(context->app, fd,
                                  EVENT_READABLE | EVENT_WRITABLE,
                                  connection_event, connection,
                                  &connection->source);
    }
    if (result != CORE_OK && connection->ipc != NULL) {
        (void)ipc_connection_close(&connection->ipc);
    }
    return result;
}

static void reconnect_event(AppHandle app, AppEventSourceId source,
                            EventMask mask, void *opaque)
{
    struct UiContextHandle *context = opaque;
    (void)app;
    (void)source;
    (void)mask;
    if (context->stopping) return;
    if (context->compositor.ipc == NULL) {
        (void)connection_open(context, &context->compositor,
                              COMPOSITOR_ENDPOINT,
                              sizeof(COMPOSITOR_ENDPOINT) - 1);
    }
    if (context->window.ipc == NULL) {
        (void)connection_open(context, &context->window,
                              WINDOW_ENDPOINT,
                              sizeof(WINDOW_ENDPOINT) - 1);
    }
}

ErrorCode ui_initialize(AppHandle app, const UiConfig *config,
                        UiContextHandle *out_context)
{
    MemoryBlock block = { 0 };
    struct UiContextHandle *context;
    u32 count = config == NULL || config->preferred_buffer_count == 0
                    ? UI_BUFFER_COUNT_DEFAULT : config->preferred_buffer_count;
    ErrorCode result;

    if (app == NULL || out_context == NULL || count > UI_BUFFER_COUNT_MAX) {
        return CORE_INVALID_ARGUMENT;
    }
    result = memory_allocate(sizeof(*context), &block);
    if (result != CORE_OK) return result;
    context = (struct UiContextHandle *)block.data;
    context->block = block;
    context->app = app;
    context->buffer_count = count;
    context->next_request_id = 0;
    context->compositor.context = context;
    context->window.context = context;
    result = app_event_create_timer(app, 0, RECONNECT_INTERVAL_MS,
                                    reconnect_event, context,
                                    &context->reconnect_source);
    if (result != CORE_OK) {
        context->stopping = true;
        (void)memory_free(&block);
        return result;
    }
    reconnect_event(app, context->reconnect_source, EVENT_READABLE, context);
    *out_context = context;
    return CORE_OK;
}

static ErrorCode title_length(const char *title, u16 *out_length)
{
    usize length = 0;
    if (title == NULL || out_length == NULL) return CORE_INVALID_ARGUMENT;
    while (length <= GUI_WINDOW_TITLE_MAX && title[length] != '\0') ++length;
    if (length == 0 || length > GUI_WINDOW_TITLE_MAX) return CORE_INVALID_ARGUMENT;
    *out_length = (u16)length;
    return CORE_OK;
}

ErrorCode ui_view_create(UiContextHandle context, const UiViewConfig *config,
                         UiViewHandle *out_view)
{
    MemoryBlock block = { 0 };
    struct UiViewHandle *view;
    u16 length;
    ErrorCode result;

    if (context == NULL || config == NULL || out_view == NULL ||
        config->handler == NULL || context->stopping ||
        (config->role != UI_VIEW_ROLE_APP && config->role != UI_VIEW_ROLE_DIALOG &&
         config->role != UI_VIEW_ROLE_TRANSIENT) ||
        (config->flags & ~(UI_VIEW_EDGE_TO_EDGE | UI_VIEW_ACCEPTS_KEYBOARD |
                           UI_VIEW_HANDLES_BACK)) != 0 ||
        (config->parent != NULL && config->parent->context != context))
        return CORE_INVALID_ARGUMENT;
    result = title_length(config->title, &length);
    if (result != CORE_OK) return result;
    result = memory_allocate(sizeof(*view) + length + 1, &block);
    if (result != CORE_OK) return result;
    view = (struct UiViewHandle *)block.data;
    view->block = block;
    view->context = context;
    view->handler = config->handler;
    view->handler_context = config->context;
    view->title = (char *)block.data + sizeof(*view);
    view->title_length = length;
    view->role = config->role;
    view->flags = config->flags;
    view->parent = config->parent;
    result = memory_copy((Bytes){ (u8 *)view->title, length },
                         (Bytes){ (u8 *)config->title, length });
    if (result != CORE_OK) {
        (void)memory_free(&block);
        return result;
    }
    view->next = context->views;
    context->views = view;
    view->state = UI_VIEW_WAITING_CONNECTIONS;
    *out_view = view;
    result = start_waiting_views(context);
    return result == CORE_WOULD_BLOCK ? CORE_OK : result;
}

ErrorCode ui_view_acquire_buffer(UiViewHandle view,
                                 UiMappedBuffer *out_mapping)
{
    if (view == NULL || out_mapping == NULL || view->state != UI_VIEW_READY ||
        !view->configured) return CORE_INVALID_STATE;
    for (u32 index = 0; index < view->context->buffer_count; ++index) {
        struct UiBufferHandle *buffer = &view->buffers[index];
        if (buffer->state == UI_BUFFER_FREE) {
            buffer->state = UI_BUFFER_ACQUIRED;
            *out_mapping = (UiMappedBuffer){
                .buffer = buffer,
                .data = buffer->memory.data,
                .size = buffer->memory.size,
                .width = view->width,
                .height = view->height,
                .stride = view->stride,
                .pixel_format = GUI_PIXEL_ARGB8888
            };
            return CORE_OK;
        }
    }
    return CORE_WOULD_BLOCK;
}

ErrorCode ui_view_damage(UiViewHandle view, const GuiRect *rects,
                         usize count)
{
    if (view == NULL || rects == NULL || count == 0 ||
        count > GUI_MAX_DAMAGE_RECTS ||
        view->damage_count + count > GUI_MAX_DAMAGE_RECTS)
        return CORE_INVALID_ARGUMENT;
    for (usize index = 0; index < count; ++index) {
        const GuiRect *rect = &rects[index];
        if (rect->width == 0 || rect->height == 0 || rect->x < 0 || rect->y < 0 ||
            (u32)rect->x > view->width || rect->width > view->width - (u32)rect->x ||
            (u32)rect->y > view->height || rect->height > view->height - (u32)rect->y)
            return CORE_INVALID_ARGUMENT;
    }
    for (usize index = 0; index < count; ++index)
        view->damage[view->damage_count++] = rects[index];
    return CORE_OK;
}

ErrorCode ui_view_present(UiViewHandle view, UiBufferHandle buffer)
{
    u8 payload[20 + GUI_MAX_DAMAGE_RECTS * 16];
    usize size = 0;
    ErrorCode result;
    GuiRequestPrefix prefix;

    if (view == NULL || buffer == NULL || buffer->view != view ||
        buffer->state != UI_BUFFER_ACQUIRED || view->state != UI_VIEW_READY ||
        !view->configured) return CORE_INVALID_STATE;

    prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION, next_request(view->context) };
    GuiSurfaceAttachRequest attach = { prefix, view->surface_id, buffer->buffer_id };
    result = gui_encode_surface_attach(&attach,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result == CORE_OK) result = queue_message(&view->context->compositor,
        GUI_SURFACE_ATTACH, payload, size);

    GuiSurfaceDamageRequest damage = { 0 };
    damage.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION,
                                       next_request(view->context) };
    damage.surface_id = view->surface_id;
    damage.flags = view->damage_count == 0 ? GUI_FULL_DAMAGE : 0;
    damage.rect_count = view->damage_count;
    for (u16 index = 0; index < damage.rect_count; ++index)
        damage.rects[index] = view->damage[index];
    if (result == CORE_OK) result = gui_encode_surface_damage(&damage,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result == CORE_OK) result = queue_message(&view->context->compositor,
        GUI_SURFACE_DAMAGE, payload, size);

    GuiSurfaceFrameRequest frame = {
        { GUI_PROTOCOL_VERSION, next_request(view->context) }, view->surface_id
    };
    if (result == CORE_OK) result = gui_encode_surface_frame_request(&frame,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result == CORE_OK) result = queue_message(&view->context->compositor,
        GUI_SURFACE_REQUEST_FRAME, payload, size);

    view->commit_serial++;
    if (view->commit_serial == 0) view->commit_serial++;
    GuiSurfaceCommitRequest commit = {
        { GUI_PROTOCOL_VERSION, next_request(view->context) },
        view->surface_id, view->commit_serial
    };
    if (result == CORE_OK) result = gui_encode_surface_commit(&commit,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result == CORE_OK) result = queue_message(&view->context->compositor,
        GUI_SURFACE_COMMIT, payload, size);

    if (result == CORE_OK) {
        view->pending_commit_request = commit.prefix.request_id;
        buffer->state = UI_BUFFER_SUBMITTED;
        view->damage_count = 0;
    }
    return result;
}

static ErrorCode window_id_operation(UiViewHandle view, IpcMessageType type)
{
    u8 payload[32];
    usize size;
    GuiWindowIdRequest request;
    ErrorCode result;

    if (view == NULL || view->state != UI_VIEW_READY || view->window_id == 0)
        return CORE_INVALID_STATE;
    request = (GuiWindowIdRequest){
        { GUI_PROTOCOL_VERSION, next_request(view->context) }, view->window_id
    };
    result = gui_encode_window_id_request(&request,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result == CORE_OK) result = queue_message(
        &view->context->window, type, payload, size);
    return result;
}

ErrorCode ui_view_publish(UiViewHandle view)
{
    return window_id_operation(view, GUI_WINDOW_SHOW);
}

ErrorCode ui_view_hide(UiViewHandle view)
{
    return window_id_operation(view, GUI_WINDOW_HIDE);
}

ErrorCode ui_view_set_title(UiViewHandle view, const char *title)
{
    u8 payload[GUI_WINDOW_TITLE_MAX + 32];
    usize size;
    u16 length;
    GuiWindowSetTitleRequest request = { 0 };
    ErrorCode result;

    if (view == NULL || view->state != UI_VIEW_READY) return CORE_INVALID_STATE;
    result = title_length(title, &length);
    if (result != CORE_OK) return result;
    request.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION,
                                        next_request(view->context) };
    request.window_id = view->window_id;
    request.title_length = length;
    result = memory_copy((Bytes){ request.title, length },
                         (Bytes){ (u8 *)title, length });
    if (result == CORE_OK) result = gui_encode_window_set_title(
        &request, (Bytes){ payload, sizeof(payload) }, &size);
    if (result == CORE_OK) result = queue_message(
        &view->context->window, GUI_WINDOW_SET_TITLE, payload, size);
    return result;
}

ErrorCode ui_view_respond_close(UiViewHandle view, u64 close_serial,
                                GuiCloseResponse response)
{
    u8 payload[40];
    usize size;
    GuiWindowCloseResponseRequest request;
    ErrorCode result;

    if (view == NULL || view->state != UI_VIEW_READY || close_serial == 0 ||
        (response != GUI_CLOSE_ALLOW && response != GUI_CLOSE_DENY &&
         response != GUI_CLOSE_DEFER)) return CORE_INVALID_ARGUMENT;
    request = (GuiWindowCloseResponseRequest){
        { GUI_PROTOCOL_VERSION, next_request(view->context) },
        view->window_id, close_serial, response
    };
    result = gui_encode_window_close_response(
        &request, (Bytes){ payload, sizeof(payload) }, &size);
    if (result == CORE_OK) result = queue_message(
        &view->context->window, GUI_WINDOW_RESPOND_CLOSE, payload, size);
    return result;
}

ErrorCode ui_get_latest_configure(UiViewHandle view,
                                  GuiWindowConfigureEvent *out_configure)
{
    if (view == NULL || out_configure == NULL) return CORE_INVALID_ARGUMENT;
    if (!view->has_configure) return CORE_NOT_FOUND;
    *out_configure = view->latest_configure;
    return CORE_OK;
}

ErrorCode ui_view_destroy(UiViewHandle *view)
{
    struct UiViewHandle *handle;
    struct UiViewHandle **link;
    struct UiContextHandle *context;
    MemoryBlock block;
    ErrorCode result = CORE_OK;
    u8 payload[32];
    usize size;

    if (view == NULL || *view == NULL) return CORE_INVALID_ARGUMENT;
    handle = *view;
    context = handle->context;
    link = &context->views;
    while (*link != NULL && *link != handle) link = &(*link)->next;
    if (*link == NULL) return CORE_INVALID_STATE;
    for (struct UiViewHandle *child = context->views; child != NULL;
         child = child->next) {
        if (child->parent == handle) return CORE_INVALID_STATE;
    }
    for (u32 index = 0; index < context->buffer_count; ++index) {
        struct UiBufferHandle *buffer = &handle->buffers[index];
        if (buffer->buffer_id != 0 && context->compositor.open) {
            GuiBufferIdRequest request = {
                { GUI_PROTOCOL_VERSION, next_request(context) }, buffer->buffer_id
            };
            ErrorCode error = gui_encode_buffer_id_request(
                &request, (Bytes){ payload, sizeof(payload) }, &size);
            if (error == CORE_OK) error = queue_message(
                &context->compositor, GUI_SURFACE_BUFFER_DESTROY, payload, size);
            keep_first(&result, error);
        }
        if (buffer->memory.size != 0)
            keep_first(&result, memory_shared_close(&buffer->memory));
    }
    if (handle->window_id != 0 && context->window.open) {
        GuiWindowIdRequest request = {
            { GUI_PROTOCOL_VERSION, next_request(context) }, handle->window_id
        };
        ErrorCode error = gui_encode_window_id_request(
            &request, (Bytes){ payload, sizeof(payload) }, &size);
        if (error == CORE_OK) error = queue_message(
            &context->window, GUI_WINDOW_DESTROY, payload, size);
        keep_first(&result, error);
    }
    if (handle->surface_id != 0 && context->compositor.open) {
        GuiSurfaceIdRequest request = {
            { GUI_PROTOCOL_VERSION, next_request(context) }, handle->surface_id
        };
        ErrorCode error = gui_encode_surface_id_request(
            &request, (Bytes){ payload, sizeof(payload) }, &size);
        if (error == CORE_OK) error = queue_message(
            &context->compositor, GUI_SURFACE_DESTROY, payload, size);
        keep_first(&result, error);
    }
    *link = handle->next;
    block = handle->block;
    keep_first(&result, memory_free(&block));
    *view = NULL;
    return result;
}

ErrorCode ui_shutdown(UiContextHandle *context)
{
    struct UiContextHandle *handle;
    MemoryBlock block;
    ErrorCode result = CORE_OK;

    if (context == NULL || *context == NULL) return CORE_INVALID_ARGUMENT;
    handle = *context;
    if (handle->views != NULL) return CORE_INVALID_STATE;
    handle->stopping = true;
    if (handle->reconnect_source != 0) {
        keep_first(&result, app_event_remove(handle->app,
                                             handle->reconnect_source));
        handle->reconnect_source = 0;
    }
    keep_first(&result, connection_close(&handle->window));
    keep_first(&result, connection_close(&handle->compositor));
    block = handle->block;
    keep_first(&result, memory_free(&block));
    *context = NULL;
    return result;
}
