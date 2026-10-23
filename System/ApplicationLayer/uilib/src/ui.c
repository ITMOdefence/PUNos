#include <uilib/ui.h>
#include <punos/syscall/syscall.h>
#include <special/ipc.h>
#include <sys/memory.h>

#define COMPOSITOR_ENDPOINT "/run/punos/compositorserv.sock"
#define WINDOW_ENDPOINT "/run/punos/windowserv.sock"

typedef struct UiConnection {
    struct UiContextHandle *context;
    IpcConnectionHandle ipc;
    AppEventSourceId source;
    bool open;
} UiConnection;

struct UiContextHandle {
    MemoryBlock block;
    AppHandle app;
    u32 buffer_count;
    bool stopping;
    UiConnection compositor;
    UiConnection window;
};

static void keep_first(ErrorCode *result, ErrorCode error)
{
    if (*result == CORE_OK && error != CORE_OK) *result = error;
}

static ErrorCode connection_close(UiConnection *connection)
{
    ErrorCode result = CORE_OK;

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
        /* Message dispatch is added with UiView in the next layer. */
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
    result = connection_open(context, &context->compositor,
                             COMPOSITOR_ENDPOINT,
                             sizeof(COMPOSITOR_ENDPOINT) - 1);
    if (result == CORE_OK) {
        result = connection_open(context, &context->window,
                                 WINDOW_ENDPOINT,
                                 sizeof(WINDOW_ENDPOINT) - 1);
    }
    if (result != CORE_OK) {
        context->stopping = true;
        (void)connection_close(&context->window);
        (void)connection_close(&context->compositor);
        (void)memory_free(&block);
        return result;
    }
    *out_context = context;
    return CORE_OK;
}

ErrorCode ui_shutdown(UiContextHandle *context)
{
    struct UiContextHandle *handle;
    MemoryBlock block;
    ErrorCode result = CORE_OK;

    if (context == NULL || *context == NULL) return CORE_INVALID_ARGUMENT;
    handle = *context;
    handle->stopping = true;
    keep_first(&result, connection_close(&handle->window));
    keep_first(&result, connection_close(&handle->compositor));
    block = handle->block;
    keep_first(&result, memory_free(&block));
    *context = NULL;
    return result;
}
