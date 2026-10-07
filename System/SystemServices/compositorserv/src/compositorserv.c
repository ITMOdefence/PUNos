#include <compositorserv.h>

#include <gui/protocol.h>
#include <special/ipc.h>
#include <sys/eventloop.h>
#include <sys/memory.h>
#include <sys/random.h>
#include <punos/syscall/syscall.h>

#include "renderer.h"

#define COMPOSITOR_SERVICE_CLIENT_MAX ((usize)32)
#define COMPOSITOR_SERVICE_EVENT_MAX ((usize)64)
#define COMPOSITOR_SERVICE_SURFACE_MAX ((usize)64)
#define COMPOSITOR_SERVICE_BUFFER_MAX ((usize)64)
#define COMPOSITOR_SERVICE_RESPONSE_MAX ((usize)64)
#define COMPOSITOR_SERVICE_SCENE_MAX ((usize)128)

typedef struct CompositorSurface {
    bool active;
    u64 surface_id;
    u8 bind_token[GUI_SURFACE_BIND_TOKEN_SIZE];

    bool pending_attach_set;
    u64 pending_buffer_id;
    bool current_buffer_set;
    u64 current_buffer_id;

    bool pending_full_damage;
    u16 pending_damage_count;
    GuiRect pending_damage[GUI_MAX_DAMAGE_RECTS];

    bool pending_opaque_set;
    u16 pending_opaque_count;
    GuiRect pending_opaque[GUI_MAX_DAMAGE_RECTS];
    u16 current_opaque_count;
    GuiRect current_opaque[GUI_MAX_DAMAGE_RECTS];

    bool pending_frame;
    u64 pending_frame_request_id;
    bool committed_frame;
    u64 committed_frame_request_id;
    u64 committed_frame_serial;

    u64 last_commit_serial;
    ErrorCode pending_error;
} CompositorSurface;

typedef enum CompositorBufferState {
    COMPOSITOR_BUFFER_AVAILABLE = 0,
    COMPOSITOR_BUFFER_PENDING = 1,
    COMPOSITOR_BUFFER_IN_USE = 2
} CompositorBufferState;

typedef struct CompositorBuffer {
    bool active;
    bool destroy_pending;
    u64 buffer_id;
    u32 width;
    u32 height;
    u32 stride;
    GuiPixelFormat pixel_format;
    u64 byte_size;
    i32 fd;
    SharedMemoryMapping mapping;
    CompositorBufferState state;
    u64 surface_id;
} CompositorBuffer;

typedef struct CompositorClient {
    IpcConnectionHandle connection;
    EventSourceId source;
    u64 next_event_serial;
    CompositorSurface surfaces[COMPOSITOR_SERVICE_SURFACE_MAX];
    CompositorBuffer buffers[COMPOSITOR_SERVICE_BUFFER_MAX];
} CompositorClient;

typedef struct CompositorSceneNode {
    bool active;
    u64 window_id;
    u64 surface_id;
    CompositorClient *surface_owner;
    GuiSceneRect geometry;
    bool geometry_set;
    i32 clip_x;
    i32 clip_y;
    u32 clip_width;
    u32 clip_height;
    GuiSceneVisibility visibility;
    GuiSceneLayer layer;
    u64 above_window_id;
    u64 display_id;
} CompositorSceneNode;

typedef struct CompositorPresentRef {
    CompositorClient *owner;
    u64 surface_id;
    u64 buffer_id;
    u64 commit_serial;
} CompositorPresentRef;

typedef struct CompositorPendingPresent {
    bool active;
    u64 request_id;
    SharedMemory frame;
    u16 ref_count;
    CompositorPresentRef refs[COMPOSITOR_SERVICE_SCENE_MAX];
} CompositorPendingPresent;

typedef enum CompositorDisplayState {
    COMPOSITOR_DISPLAY_DISABLED = 0,
    COMPOSITOR_DISPLAY_CONNECTING = 1,
    COMPOSITOR_DISPLAY_BINDING = 2,
    COMPOSITOR_DISPLAY_LISTING = 3,
    COMPOSITOR_DISPLAY_READY = 4,
    COMPOSITOR_DISPLAY_SUSPENDED = 5
} CompositorDisplayState;

struct CompositorServiceHandle {
    MemoryBlock handle_block;
    EventLoopHandle loop;
    IpcServerHandle server;
    EventSourceId server_source;
    CompositorClient clients[COMPOSITOR_SERVICE_CLIENT_MAX];
    u8 controller_token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE];
    CompositorClient *controller_client;
    CompositorSceneNode scene[COMPOSITOR_SERVICE_SCENE_MAX];
    u64 next_binding_event_serial;
    u64 next_surface_id;
    u64 next_buffer_id;
    IpcConnectionHandle display_connection;
    EventSourceId display_source;
    CompositorDisplayState display_state;
    u8 presenter_token[COMPOSITOR_SERVICE_PRESENTER_TOKEN_SIZE];
    GuiDisplayInfo display_info;
    bool display_info_valid;
    u64 next_display_request_id;
    bool frame_dirty;
    CompositorPendingPresent pending_present;
};

static void remove_client(CompositorServiceHandle service,
                          CompositorClient *client);
static ErrorCode queue_payload(CompositorServiceHandle service,
                               CompositorClient *client, IpcMessageType type,
                               u8 *data, usize size);

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
    surface->pending_attach_set = false;
    surface->pending_buffer_id = 0;
    surface->current_buffer_set = false;
    surface->current_buffer_id = 0;
    surface->pending_full_damage = false;
    surface->pending_damage_count = 0;
    surface->pending_opaque_set = false;
    surface->pending_opaque_count = 0;
    surface->current_opaque_count = 0;
    surface->pending_frame = false;
    surface->pending_frame_request_id = 0;
    surface->committed_frame = false;
    surface->committed_frame_request_id = 0;
    surface->committed_frame_serial = 0;
    surface->last_commit_serial = 0;
    surface->pending_error = CORE_OK;
    for (index = 0; index < GUI_MAX_DAMAGE_RECTS; ++index) {
        surface->pending_damage[index] = (GuiRect){ 0, 0, 0, 0 };
        surface->pending_opaque[index] = (GuiRect){ 0, 0, 0, 0 };
        surface->current_opaque[index] = (GuiRect){ 0, 0, 0, 0 };
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
    buffer->destroy_pending = false;
    buffer->buffer_id = 0;
    buffer->width = 0;
    buffer->height = 0;
    buffer->stride = 0;
    buffer->pixel_format = 0;
    buffer->byte_size = 0;
    buffer->fd = -1;
    buffer->mapping.data = NULL;
    buffer->mapping.size = 0;
    buffer->state = COMPOSITOR_BUFFER_AVAILABLE;
    buffer->surface_id = 0;
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
            !client->buffers[index].destroy_pending &&
            client->buffers[index].buffer_id == buffer_id) {
            return &client->buffers[index];
        }
    }
    return NULL;
}

static CompositorBuffer *find_buffer_any(CompositorClient *client,
                                         u64 buffer_id)
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

static void release_surface_pending_buffer(CompositorClient *client,
                                           CompositorSurface *surface)
{
    CompositorBuffer *buffer;

    if (client == NULL || surface == NULL || !surface->pending_attach_set ||
        surface->pending_buffer_id == 0) {
        return;
    }
    buffer = find_buffer_any(client, surface->pending_buffer_id);
    if (buffer == NULL || buffer->state != COMPOSITOR_BUFFER_PENDING ||
        buffer->surface_id != surface->surface_id) {
        return;
    }
    buffer->state = COMPOSITOR_BUFFER_AVAILABLE;
    buffer->surface_id = 0;
    if (buffer->destroy_pending) clear_buffer(buffer);
}

static CompositorBuffer *find_free_buffer(CompositorClient *client)
{
    usize index;

    for (index = 0; index < COMPOSITOR_SERVICE_BUFFER_MAX; ++index) {
        if (!client->buffers[index].active) return &client->buffers[index];
    }
    return NULL;
}

static void clear_scene_node(CompositorSceneNode *node)
{
    node->active = false;
    node->window_id = 0;
    node->surface_id = 0;
    node->surface_owner = NULL;
    node->geometry = (GuiSceneRect){ 0, 0, 0, 0 };
    node->geometry_set = false;
    node->clip_x = 0;
    node->clip_y = 0;
    node->clip_width = 0;
    node->clip_height = 0;
    node->visibility = GUI_SCENE_HIDDEN;
    node->layer = GUI_SCENE_LAYER_APP;
    node->above_window_id = 0;
    node->display_id = 0;
}

static void clear_scene(CompositorServiceHandle service)
{
    usize index;
    for (index = 0; index < COMPOSITOR_SERVICE_SCENE_MAX; ++index) {
        clear_scene_node(&service->scene[index]);
    }
}

static CompositorSceneNode *find_scene_window(CompositorServiceHandle service,
                                               u64 window_id)
{
    usize index;
    for (index = 0; index < COMPOSITOR_SERVICE_SCENE_MAX; ++index) {
        if (service->scene[index].active &&
            service->scene[index].window_id == window_id) {
            return &service->scene[index];
        }
    }
    return NULL;
}

static CompositorSceneNode *find_scene_surface(CompositorServiceHandle service,
                                                u64 surface_id)
{
    usize index;
    for (index = 0; index < COMPOSITOR_SERVICE_SCENE_MAX; ++index) {
        if (service->scene[index].active &&
            service->scene[index].surface_id == surface_id) {
            return &service->scene[index];
        }
    }
    return NULL;
}

static CompositorSceneNode *find_free_scene_node(CompositorServiceHandle service)
{
    usize index;
    for (index = 0; index < COMPOSITOR_SERVICE_SCENE_MAX; ++index) {
        if (!service->scene[index].active) return &service->scene[index];
    }
    return NULL;
}

static CompositorSurface *find_surface_global(CompositorServiceHandle service,
                                               u64 surface_id,
                                               CompositorClient **out_owner)
{
    usize index;
    for (index = 0; index < COMPOSITOR_SERVICE_CLIENT_MAX; ++index) {
        CompositorClient *client = &service->clients[index];
        CompositorSurface *surface;
        if (client->connection == NULL) continue;
        surface = find_surface(client, surface_id);
        if (surface != NULL) {
            if (out_owner != NULL) *out_owner = client;
            return surface;
        }
    }
    return NULL;
}

static bool token_equal(const u8 *a, const u8 *b, usize size)
{
    usize index;
    u8 difference = 0;
    for (index = 0; index < size; ++index) difference |= (u8)(a[index] ^ b[index]);
    return difference == 0;
}

static void clear_pending_present(CompositorServiceHandle service)
{
    if (service->pending_present.active) {
        (void)memory_shared_close(&service->pending_present.frame);
    }
    service->pending_present.active = false;
    service->pending_present.request_id = 0;
    service->pending_present.ref_count = 0;
    service->pending_present.frame.fd = -1;
    service->pending_present.frame.reserved = 0;
    service->pending_present.frame.data = NULL;
    service->pending_present.frame.size = 0;
}

static void drop_present_refs_for_client(CompositorServiceHandle service,
                                         CompositorClient *client)
{
    usize read_index;
    usize write_index = 0;

    if (!service->pending_present.active || client == NULL) return;
    for (read_index = 0; read_index < service->pending_present.ref_count;
         ++read_index) {
        CompositorPresentRef ref = service->pending_present.refs[read_index];
        if (ref.owner == client) continue;
        service->pending_present.refs[write_index++] = ref;
    }
    service->pending_present.ref_count = (u16)write_index;
}

static ErrorCode display_update_interest(CompositorServiceHandle service,
                                         bool writable)
{
    return eventloop_update_fd(
        service->loop, service->display_source,
        EVENT_READABLE | EVENT_ERROR | EVENT_HANGUP |
        (writable ? EVENT_WRITABLE : 0));
}

static ErrorCode display_queue(CompositorServiceHandle service,
                               IpcMessageType type, u8 *data, usize size)
{
    IpcMessage message = { type, 0, { data, size }, IPC_NO_DESCRIPTOR, 0 };
    bool pending;
    ErrorCode result;

    if (service->display_connection == NULL) return CORE_INVALID_STATE;
    result = ipc_connection_send(service->display_connection, &message);
    if (result != CORE_OK) return result;
    result = ipc_connection_flush(service->display_connection, &pending);
    if (result != CORE_OK) return result;
    return display_update_interest(service, pending);
}

static ErrorCode display_queue_descriptor(CompositorServiceHandle service,
                                          IpcMessageType type, u8 *data,
                                          usize size, i32 descriptor)
{
    IpcMessage message = { type, 0, { data, size }, descriptor, 0 };
    bool pending;
    ErrorCode result;

    if (service->display_connection == NULL) return CORE_INVALID_STATE;
    result = ipc_connection_send(service->display_connection, &message);
    if (result != CORE_OK) return result;
    result = ipc_connection_flush(service->display_connection, &pending);
    if (result != CORE_OK) return result;
    return display_update_interest(service, pending);
}

static u64 next_display_request_id(CompositorServiceHandle service)
{
    u64 value = service->next_display_request_id++;
    if (value == 0) value = service->next_display_request_id++;
    return value;
}

static ErrorCode display_send_bind(CompositorServiceHandle service)
{
    GuiPresenterBindRequest request;
    u8 payload[64];
    usize size;
    usize index;
    ErrorCode result;

    request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    request.prefix.request_id = next_display_request_id(service);
    for (index = 0; index < COMPOSITOR_SERVICE_PRESENTER_TOKEN_SIZE; ++index) {
        request.presenter_token[index] = service->presenter_token[index];
    }
    result = gui_encode_presenter_bind(&request,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    result = display_queue(service, GUI_DISPLAY_BIND_PRESENTER, payload, size);
    if (result == CORE_OK) service->display_state = COMPOSITOR_DISPLAY_BINDING;
    return result;
}

static ErrorCode display_send_list(CompositorServiceHandle service)
{
    GuiRequestPrefix prefix;
    u8 payload[16];
    usize size;
    ErrorCode result;

    prefix.protocol_version = GUI_PROTOCOL_VERSION;
    prefix.request_id = next_display_request_id(service);
    result = gui_encode_versioned_request(prefix,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    result = display_queue(service, GUI_DISPLAY_LIST, payload, size);
    if (result == CORE_OK) service->display_state = COMPOSITOR_DISPLAY_LISTING;
    return result;
}

static bool scene_targets_display(const CompositorSceneNode *node,
                                  const GuiDisplayInfo *display)
{
    if (!node->active || node->visibility != GUI_SCENE_VISIBLE ||
        !node->geometry_set) {
        return false;
    }
    return node->display_id == 0 || node->display_id == display->display_id;
}

static i32 scene_order_compare(const CompositorSceneNode *left,
                               const CompositorSceneNode *right)
{
    if (left->layer < right->layer) return -1;
    if (left->layer > right->layer) return 1;
    if (left->above_window_id == right->window_id) return 1;
    if (right->above_window_id == left->window_id) return -1;
    if (left->window_id < right->window_id) return -1;
    if (left->window_id > right->window_id) return 1;
    return 0;
}

static void sort_scene_nodes(CompositorSceneNode **nodes, usize count)
{
    usize index;
    for (index = 1; index < count; ++index) {
        CompositorSceneNode *value = nodes[index];
        usize position = index;
        while (position != 0 &&
               scene_order_compare(nodes[position - 1], value) > 0) {
            nodes[position] = nodes[position - 1];
            --position;
        }
        nodes[position] = value;
    }
}

static ErrorCode compose_output_frame(CompositorServiceHandle service,
                                      SharedMemory *out_frame,
                                      CompositorPresentRef *out_refs,
                                      u16 *out_ref_count)
{
    CompositorSceneNode *nodes[COMPOSITOR_SERVICE_SCENE_MAX];
    CompositorRenderLayer layers[COMPOSITOR_SERVICE_SCENE_MAX];
    CompositorRenderTarget target;
    u32 width;
    u32 height;
    usize byte_size;
    usize node_count = 0;
    usize layer_count = 0;
    usize index;
    ErrorCode result;

    if (!service->display_info_valid || out_frame == NULL ||
        out_refs == NULL || out_ref_count == NULL) {
        return CORE_INVALID_STATE;
    }
    width = service->display_info.current_mode.width_px;
    height = service->display_info.current_mode.height_px;
    if (width == 0 || height == 0 ||
        (service->display_info.flags & GUI_DISPLAY_ENABLED) == 0) {
        return CORE_INVALID_STATE;
    }
    result = core_size_multiply((usize)width, (usize)height, &byte_size);
    if (result != CORE_OK) return result;
    result = core_size_multiply(byte_size, (usize)4, &byte_size);
    if (result != CORE_OK) return result;
    result = memory_shared_create(byte_size, out_frame);
    if (result != CORE_OK) return result;

    for (index = 0; index < COMPOSITOR_SERVICE_SCENE_MAX; ++index) {
        CompositorSceneNode *node = &service->scene[index];
        if (scene_targets_display(node, &service->display_info)) {
            nodes[node_count++] = node;
        }
    }
    sort_scene_nodes(nodes, node_count);

    for (index = 0; index < node_count; ++index) {
        CompositorSceneNode *node = nodes[index];
        CompositorSurface *surface = find_surface(node->surface_owner,
                                                   node->surface_id);
        CompositorBuffer *buffer;
        CompositorRenderLayer *layer;
        if (surface == NULL || !surface->current_buffer_set) continue;
        buffer = find_buffer_any(node->surface_owner,
                                 surface->current_buffer_id);
        if (buffer == NULL || buffer->mapping.data == NULL) continue;
        if (node->geometry.width != buffer->width ||
            node->geometry.height != buffer->height) {
            continue;
        }

        layer = &layers[layer_count];
        layer->pixels = buffer->mapping.data;
        layer->width = buffer->width;
        layer->height = buffer->height;
        layer->stride = buffer->stride;
        layer->format = buffer->pixel_format;
        layer->x = node->geometry.x >> 8;
        layer->y = node->geometry.y >> 8;
        layer->clip_set = node->clip_width != 0 && node->clip_height != 0;
        layer->clip_x = node->clip_x;
        layer->clip_y = node->clip_y;
        layer->clip_width = node->clip_width;
        layer->clip_height = node->clip_height;

        out_refs[layer_count].owner = node->surface_owner;
        out_refs[layer_count].surface_id = surface->surface_id;
        out_refs[layer_count].buffer_id = buffer->buffer_id;
        out_refs[layer_count].commit_serial = surface->last_commit_serial;
        ++layer_count;
    }

    target.pixels = out_frame->data;
    target.width = width;
    target.height = height;
    target.stride = width * 4;
    result = compositor_render_clear(&target, 0, 0, 0);
    if (result == CORE_OK) {
        result = compositor_render_layers(&target, layers, layer_count);
    }
    if (result != CORE_OK) {
        (void)memory_shared_close(out_frame);
        return result;
    }
    *out_ref_count = (u16)layer_count;
    return CORE_OK;
}

static ErrorCode display_present_frame(CompositorServiceHandle service)
{
    SharedMemory frame = { -1, 0, NULL, 0 };
    CompositorPresentRef refs[COMPOSITOR_SERVICE_SCENE_MAX];
    GuiDisplayPresentRequest request;
    u8 payload[128];
    usize size;
    u16 ref_count = 0;
    usize index;
    u64 request_id;
    ErrorCode result;

    if (!service->frame_dirty || service->pending_present.active ||
        service->display_state != COMPOSITOR_DISPLAY_READY ||
        !service->display_info_valid) {
        return CORE_OK;
    }
    result = compose_output_frame(service, &frame, refs, &ref_count);
    if (result == CORE_INVALID_STATE) return CORE_OK;
    if (result != CORE_OK) return result;

    request_id = next_display_request_id(service);
    request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    request.prefix.request_id = request_id;
    request.display_id = service->display_info.display_id;
    request.generation = service->display_info.generation;
    request.width = service->display_info.current_mode.width_px;
    request.height = service->display_info.current_mode.height_px;
    request.stride = request.width * 4;
    request.pixel_format = GUI_PIXEL_XRGB8888;
    request.byte_size = frame.size;
    request.flags = GUI_FULL_DAMAGE;
    request.damage_count = 0;
    result = gui_encode_display_present(&request,
        (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) {
        (void)memory_shared_close(&frame);
        return result;
    }
    result = display_queue_descriptor(service, GUI_DISPLAY_PRESENT,
                                      payload, size, frame.fd);
    if (result != CORE_OK) {
        (void)memory_shared_close(&frame);
        return result;
    }

    service->pending_present.active = true;
    service->pending_present.request_id = request_id;
    service->pending_present.frame = frame;
    service->pending_present.ref_count = ref_count;
    for (index = 0; index < ref_count; ++index) {
        service->pending_present.refs[index] = refs[index];
    }
    service->frame_dirty = false;
    return CORE_OK;
}

static bool buffer_is_current(CompositorClient *client, u64 buffer_id)
{
    usize index;

    if (client == NULL || buffer_id == 0) return false;
    for (index = 0; index < COMPOSITOR_SERVICE_SURFACE_MAX; ++index) {
        CompositorSurface *surface = &client->surfaces[index];
        if (surface->active && surface->current_buffer_set &&
            surface->current_buffer_id == buffer_id) {
            return true;
        }
    }
    return false;
}

static ErrorCode send_frame_done(CompositorServiceHandle service,
                                 const CompositorPresentRef *ref,
                                 u64 presented_at_ms)
{
    CompositorSurface *surface;
    GuiSurfaceFrameDoneEvent event;
    u8 payload[40];
    usize size;
    ErrorCode result;

    if (ref == NULL || ref->owner == NULL ||
        ref->owner->connection == NULL) {
        return CORE_OK;
    }
    surface = find_surface(ref->owner, ref->surface_id);
    if (surface == NULL || !surface->committed_frame ||
        surface->committed_frame_serial > ref->commit_serial) {
        return CORE_OK;
    }

    event.request_id = surface->committed_frame_request_id;
    event.surface_id = surface->surface_id;
    event.commit_serial = ref->commit_serial;
    event.presented_at_ms = presented_at_ms;
    result = gui_encode_surface_frame_done(
        &event, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    result = queue_payload(service, ref->owner, GUI_SURFACE_FRAME_DONE,
                           payload, size);
    if (result != CORE_OK) return result;

    surface->committed_frame = false;
    surface->committed_frame_request_id = 0;
    surface->committed_frame_serial = 0;
    return CORE_OK;
}

static ErrorCode send_buffer_released(CompositorServiceHandle service,
                                      CompositorClient *client,
                                      CompositorBuffer *buffer)
{
    GuiBufferReleasedEvent event;
    u8 payload[24];
    usize size;
    ErrorCode result;

    if (client == NULL || buffer == NULL || !buffer->active ||
        buffer->state != COMPOSITOR_BUFFER_IN_USE ||
        buffer_is_current(client, buffer->buffer_id)) {
        return CORE_OK;
    }
    if (client->connection == NULL) {
        return CORE_OK;
    }
    if (client->next_event_serial == 0) return CORE_OVERFLOW;

    event.serial = client->next_event_serial++;
    event.buffer_id = buffer->buffer_id;
    result = gui_encode_buffer_released(
        &event, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    result = queue_payload(service, client, GUI_SURFACE_BUFFER_RELEASED,
                           payload, size);
    if (result != CORE_OK) return result;

    buffer->state = COMPOSITOR_BUFFER_AVAILABLE;
    buffer->surface_id = 0;
    if (buffer->destroy_pending) clear_buffer(buffer);
    return CORE_OK;
}

static ErrorCode finish_presented_frame(CompositorServiceHandle service,
                                        u64 presented_at_ms)
{
    usize index;
    ErrorCode first_error = CORE_OK;

    for (index = 0; index < service->pending_present.ref_count; ++index) {
        ErrorCode result = send_frame_done(
            service, &service->pending_present.refs[index], presented_at_ms);
        if (result != CORE_OK && first_error == CORE_OK) first_error = result;
    }

    /* A retired source buffer is safe to return only after a newer output
     * frame has been accepted as presented. Current buffers stay InUse because
     * a later composition may still read them. */
    for (index = 0; index < COMPOSITOR_SERVICE_CLIENT_MAX; ++index) {
        CompositorClient *client = &service->clients[index];
        usize buffer_index;
        if (client->connection == NULL) continue;
        for (buffer_index = 0; buffer_index < COMPOSITOR_SERVICE_BUFFER_MAX;
             ++buffer_index) {
            ErrorCode result = send_buffer_released(
                service, client, &client->buffers[buffer_index]);
            if (result != CORE_OK && first_error == CORE_OK) first_error = result;
        }
    }
    return first_error;
}

static void display_suspend(CompositorServiceHandle service)
{
    clear_pending_present(service);
    service->frame_dirty = true;
    if (service->display_connection != NULL) {
        (void)eventloop_remove_source(service->loop, service->display_source);
        (void)ipc_connection_close(&service->display_connection);
    }
    service->display_source = 0;
    service->display_state = COMPOSITOR_DISPLAY_SUSPENDED;
    service->display_info_valid = false;
}

static ErrorCode display_handle_message(CompositorServiceHandle service,
                                        IpcMessage *message)
{
    if (message->type == GUI_DISPLAY_PRESENT_RESULT) {
        GuiBasicResult result;
        ErrorCode code = gui_decode_basic_result(message,
            GUI_DISPLAY_PRESENT_RESULT, &result);
        if (code != CORE_OK) return code;
        if (!service->pending_present.active ||
            result.request_id != service->pending_present.request_id) {
            return CORE_NOT_FOUND;
        }
        if (result.error != CORE_OK) {
            clear_pending_present(service);
            service->frame_dirty = true;
        }
        return CORE_OK;
    }
    if (message->type == GUI_DISPLAY_PRESENTED) {
        GuiDisplayPresentedEvent event;
        ErrorCode finish_result;
        ErrorCode code = gui_decode_display_presented(message, &event);
        if (code != CORE_OK) return code;
        if (!service->pending_present.active ||
            event.request_id != service->pending_present.request_id) {
            return CORE_NOT_FOUND;
        }
        finish_result = finish_presented_frame(service, event.presented_at_ms);
        clear_pending_present(service);
        return finish_result;
    }
    if (message->type == GUI_DISPLAY_PRESENT_FAILED) {
        GuiDisplayPresentFailedEvent event;
        ErrorCode code = gui_decode_display_present_failed(message, &event);
        if (code != CORE_OK) return code;
        if (!service->pending_present.active ||
            event.request_id != service->pending_present.request_id) {
            return CORE_NOT_FOUND;
        }
        clear_pending_present(service);
        service->frame_dirty = true;
        return CORE_OK;
    }
    if (message->type == GUI_DISPLAY_BIND_PRESENTER_RESULT) {
        GuiBasicResult result;
        ErrorCode code = gui_decode_basic_result(message,
            GUI_DISPLAY_BIND_PRESENTER_RESULT, &result);
        if (code != CORE_OK) return code;
        if (result.error != CORE_OK) return result.error;
        return display_send_list(service);
    }
    if (message->type == GUI_DISPLAY_LIST_RESULT) {
        GuiDisplayListHeader header;
        usize first_display;
        ErrorCode code = gui_decode_display_list_header(message, &header,
                                                         &first_display);
        if (code != CORE_OK) return code;
        if (header.result.error != CORE_OK) return header.result.error;
        service->display_info_valid = false;
        if (header.display_count != 0) {
            usize consumed;
            Bytes source = { message->payload.data + first_display,
                             message->payload.size - first_display };
            code = gui_decode_display_info(source, &service->display_info,
                                           &consumed);
            if (code != CORE_OK) return code;
            service->display_info_valid = true;
        }
        service->display_state = COMPOSITOR_DISPLAY_READY;
        service->frame_dirty = true;
        return CORE_OK;
    }
    if (message->type == GUI_DISPLAY_CHANGED ||
        message->type == GUI_DISPLAY_ADDED) {
        u64 serial;
        GuiDisplayInfo info;
        ErrorCode code = gui_decode_display_info_event(message, message->type,
                                                        &serial, &info);
        (void)serial;
        if (code != CORE_OK) return code;
        service->display_info = info;
        service->display_info_valid = true;
        service->frame_dirty = true;
        return CORE_OK;
    }
    if (message->type == GUI_DISPLAY_REMOVED) {
        service->display_info_valid = false;
        clear_pending_present(service);
        service->frame_dirty = true;
        return CORE_OK;
    }
    if (message->type == GUI_DISPLAY_BACKEND_LOST) {
        service->display_info_valid = false;
        service->display_state = COMPOSITOR_DISPLAY_SUSPENDED;
        clear_pending_present(service);
        service->frame_dirty = true;
        return CORE_OK;
    }
    return CORE_OK;
}

static ErrorCode service_display_event(CompositorServiceHandle service,
                                       EventMask mask)
{
    if ((mask & (EVENT_ERROR | EVENT_HANGUP)) != 0) {
        display_suspend(service);
        return CORE_OK;
    }

    if ((mask & EVENT_WRITABLE) != 0) {
        if (service->display_state == COMPOSITOR_DISPLAY_CONNECTING) {
            IpcConnectionState state;
            ErrorCode result = ipc_connection_finish_connect(
                service->display_connection, &state);
            if (result != CORE_OK) {
                display_suspend(service);
                return CORE_OK;
            }
            if (state == IPC_CONNECTION_OPEN) {
                result = display_send_bind(service);
                if (result != CORE_OK) {
                    display_suspend(service);
                    return CORE_OK;
                }
            }
        } else {
            bool pending;
            ErrorCode result = ipc_connection_flush(service->display_connection,
                                                     &pending);
            if (result != CORE_OK) {
                display_suspend(service);
                return CORE_OK;
            }
            (void)display_update_interest(service, pending);
        }
    }

    if ((mask & EVENT_READABLE) != 0 && service->display_connection != NULL) {
        while (true) {
            IpcReceiveStatus status;
            IpcMessage message;
            ErrorCode result = ipc_connection_receive(service->display_connection,
                                                       &status, &message);
            if (result != CORE_OK) {
                display_suspend(service);
                return CORE_OK;
            }
            if (status == IPC_RECEIVE_NO_MESSAGE) break;
            if (status == IPC_RECEIVE_PEER_CLOSED) {
                display_suspend(service);
                break;
            }
            result = display_handle_message(service, &message);
            (void)ipc_message_destroy(&message);
            if (result != CORE_OK) {
                display_suspend(service);
                return CORE_OK;
            }
        }
    }
    return CORE_OK;
}

static ErrorCode display_begin_connect(CompositorServiceHandle service,
                                       const String *endpoint)
{
    IpcConnectionHandle connection = NULL;
    IpcConnectionState state;
    i32 fd;
    ErrorCode result = ipc_connection_begin_connect(endpoint, &connection);

    if (result != CORE_OK) return result;
    result = ipc_connection_get_fd(connection, &fd);
    if (result != CORE_OK) goto fail;
    result = eventloop_add_fd(service->loop, fd,
        EVENT_READABLE | EVENT_WRITABLE | EVENT_ERROR | EVENT_HANGUP,
        &service->display_source);
    if (result != CORE_OK) goto fail;
    service->display_connection = connection;
    service->display_state = COMPOSITOR_DISPLAY_CONNECTING;
    (void)state;
    return CORE_OK;

fail:
    (void)ipc_connection_close(&connection);
    return result;
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

static ErrorCode notify_binding_lost(CompositorServiceHandle service,
                                     CompositorSceneNode *node,
                                     GuiSceneBindingLostReason reason)
{
    GuiSceneBindingLostEvent event;
    u8 payload[32];
    usize size;
    ErrorCode result;

    if (service->controller_client == NULL || node == NULL || !node->active) {
        return CORE_OK;
    }
    if (service->next_binding_event_serial == 0) return CORE_OVERFLOW;
    event.serial = service->next_binding_event_serial++;
    event.window_id = node->window_id;
    event.surface_id = node->surface_id;
    event.reason = reason;
    result = gui_encode_scene_binding_lost(
        &event, (Bytes){ payload, sizeof(payload) }, &size);
    if (result != CORE_OK) return result;
    return queue_payload(service, service->controller_client,
                         GUI_SCENE_BINDING_LOST, payload, size);
}

static ErrorCode remove_scene_surface(CompositorServiceHandle service,
                                      u64 surface_id,
                                      GuiSceneBindingLostReason reason)
{
    usize index;
    ErrorCode first_error = CORE_OK;
    for (index = 0; index < COMPOSITOR_SERVICE_SCENE_MAX; ++index) {
        CompositorSceneNode *node = &service->scene[index];
        if (node->active && node->surface_id == surface_id) {
            ErrorCode result = notify_binding_lost(service, node, reason);
            if (result != CORE_OK && first_error == CORE_OK) first_error = result;
            clear_scene_node(node);
        }
    }
    return first_error;
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

    {
        CompositorBuffer *current_buffer = NULL;
        ErrorCode response_result;

        release_surface_pending_buffer(client, surface);
        if (surface->current_buffer_set) {
            current_buffer = find_buffer_any(client, surface->current_buffer_id);
        }
        result = remove_scene_surface(service, surface->surface_id,
                                      GUI_SCENE_BINDING_LOST_SURFACE_DESTROYED);
        clear_surface(surface);
        service->frame_dirty = true;
        if (result != CORE_OK) return result;

        response_result = send_basic(service, client,
                                     GUI_SURFACE_DESTROY_RESULT,
                                     request.prefix.request_id, CORE_OK);
        if (response_result != CORE_OK) return response_result;

        if (current_buffer != NULL &&
            current_buffer->state == COMPOSITOR_BUFFER_IN_USE) {
            ErrorCode release_result = send_buffer_released(service, client,
                                                             current_buffer);
            if (release_result != CORE_OK) return release_result;
        }
        return CORE_OK;
    }
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
    buffer->destroy_pending = false;
    buffer->buffer_id = service->next_buffer_id++;
    buffer->width = request.width;
    buffer->height = request.height;
    buffer->stride = request.stride;
    buffer->pixel_format = request.pixel_format;
    buffer->byte_size = request.byte_size;
    buffer->fd = message->descriptor;
    buffer->mapping = mapping;
    buffer->state = COMPOSITOR_BUFFER_AVAILABLE;
    buffer->surface_id = 0;

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

    if (buffer->state == COMPOSITOR_BUFFER_AVAILABLE) {
        clear_buffer(buffer);
    } else {
        /* The id becomes invalid immediately; storage stays until release. */
        buffer->destroy_pending = true;
    }
    return send_basic(service, client, GUI_SURFACE_BUFFER_DESTROY_RESULT,
                      request.prefix.request_id, CORE_OK);
}


static CompositorBuffer *surface_effective_buffer(CompositorClient *client,
                                                  CompositorSurface *surface)
{
    u64 buffer_id;

    if (surface->pending_attach_set) {
        buffer_id = surface->pending_buffer_id;
    } else if (surface->current_buffer_set) {
        buffer_id = surface->current_buffer_id;
    } else {
        return NULL;
    }
    if (buffer_id == 0) return NULL;
    return find_buffer(client, buffer_id);
}

static bool rect_inside_buffer(const GuiRect *rect,
                               const CompositorBuffer *buffer)
{
    u64 right;
    u64 bottom;

    if (rect->x < 0 || rect->y < 0) return false;
    right = (u64)(u32)rect->x + (u64)rect->width;
    bottom = (u64)(u32)rect->y + (u64)rect->height;
    return right <= (u64)buffer->width && bottom <= (u64)buffer->height;
}

static void release_pending_reservation(CompositorClient *client,
                                        CompositorSurface *surface)
{
    CompositorBuffer *buffer;

    if (!surface->pending_attach_set || surface->pending_buffer_id == 0) return;
    buffer = find_buffer(client, surface->pending_buffer_id);
    if (buffer != NULL && buffer->state == COMPOSITOR_BUFFER_PENDING &&
        buffer->surface_id == surface->surface_id) {
        buffer->state = COMPOSITOR_BUFFER_AVAILABLE;
        buffer->surface_id = 0;
    }
}

static ErrorCode handle_surface_attach(CompositorServiceHandle service,
                                       CompositorClient *client,
                                       const IpcMessage *message)
{
    GuiSurfaceAttachRequest request;
    CompositorSurface *surface;
    CompositorBuffer *buffer;
    ErrorCode result = gui_decode_surface_attach(message, &request);

    (void)service;
    if (result != CORE_OK) return result;
    surface = find_surface(client, request.surface_id);
    if (surface == NULL) return CORE_NOT_FOUND;

    release_pending_reservation(client, surface);
    surface->pending_attach_set = true;
    surface->pending_buffer_id = request.buffer_id;

    if (request.buffer_id == 0) return CORE_OK;
    buffer = find_buffer(client, request.buffer_id);
    if (buffer == NULL) {
        surface->pending_error = CORE_NOT_FOUND;
        return CORE_OK;
    }
    if (buffer->state != COMPOSITOR_BUFFER_AVAILABLE) {
        surface->pending_error = CORE_INVALID_STATE;
        return CORE_OK;
    }
    buffer->state = COMPOSITOR_BUFFER_PENDING;
    buffer->surface_id = surface->surface_id;
    return CORE_OK;
}

static ErrorCode handle_surface_damage(CompositorServiceHandle service,
                                       CompositorClient *client,
                                       const IpcMessage *message)
{
    GuiSurfaceDamageRequest request;
    CompositorSurface *surface;
    CompositorBuffer *buffer;
    usize index;
    ErrorCode result = gui_decode_surface_damage(message, &request);

    (void)service;
    if (result != CORE_OK) return result;
    surface = find_surface(client, request.surface_id);
    if (surface == NULL) return CORE_NOT_FOUND;
    buffer = surface_effective_buffer(client, surface);
    if (buffer == NULL) {
        surface->pending_error = CORE_INVALID_STATE;
        return CORE_OK;
    }
    for (index = 0; index < request.rect_count; ++index) {
        if (!rect_inside_buffer(&request.rects[index], buffer)) {
            surface->pending_error = CORE_INVALID_ARGUMENT;
            return CORE_OK;
        }
    }
    if ((request.flags & GUI_FULL_DAMAGE) != 0) {
        surface->pending_full_damage = true;
        surface->pending_damage_count = 0;
        return CORE_OK;
    }
    if (surface->pending_full_damage) return CORE_OK;
    if ((usize)surface->pending_damage_count + (usize)request.rect_count >
        GUI_MAX_DAMAGE_RECTS) {
        surface->pending_full_damage = true;
        surface->pending_damage_count = 0;
        return CORE_OK;
    }
    for (index = 0; index < request.rect_count; ++index) {
        surface->pending_damage[surface->pending_damage_count++] =
            request.rects[index];
    }
    return CORE_OK;
}

static ErrorCode handle_surface_opaque(CompositorServiceHandle service,
                                       CompositorClient *client,
                                       const IpcMessage *message)
{
    GuiSurfaceRegionRequest request;
    CompositorSurface *surface;
    CompositorBuffer *buffer;
    usize index;
    ErrorCode result = gui_decode_surface_region(message, &request);

    (void)service;
    if (result != CORE_OK) return result;
    surface = find_surface(client, request.surface_id);
    if (surface == NULL) return CORE_NOT_FOUND;
    buffer = surface_effective_buffer(client, surface);
    if (buffer == NULL && request.rect_count != 0) {
        surface->pending_error = CORE_INVALID_STATE;
        return CORE_OK;
    }
    for (index = 0; index < request.rect_count; ++index) {
        if (!rect_inside_buffer(&request.rects[index], buffer)) {
            surface->pending_error = CORE_INVALID_ARGUMENT;
            return CORE_OK;
        }
    }
    surface->pending_opaque_set = true;
    surface->pending_opaque_count = request.rect_count;
    for (index = 0; index < request.rect_count; ++index) {
        surface->pending_opaque[index] = request.rects[index];
    }
    return CORE_OK;
}

static ErrorCode handle_surface_frame_request(CompositorServiceHandle service,
                                              CompositorClient *client,
                                              const IpcMessage *message)
{
    GuiSurfaceFrameRequest request;
    CompositorSurface *surface;
    ErrorCode result = gui_decode_surface_frame_request(message, &request);

    (void)service;
    if (result != CORE_OK) return result;
    surface = find_surface(client, request.surface_id);
    if (surface == NULL) return CORE_NOT_FOUND;
    if (surface->pending_frame) {
        surface->pending_error = CORE_INVALID_STATE;
        return CORE_OK;
    }
    surface->pending_frame = true;
    surface->pending_frame_request_id = request.prefix.request_id;
    return CORE_OK;
}

static ErrorCode handle_surface_commit(CompositorServiceHandle service,
                                       CompositorClient *client,
                                       const IpcMessage *message)
{
    GuiSurfaceCommitRequest request;
    CompositorSurface *surface;
    CompositorBuffer *new_buffer = NULL;
    CompositorBuffer *old_buffer = NULL;
    ErrorCode error = CORE_OK;
    usize index;
    ErrorCode result = gui_decode_surface_commit(message, &request);

    if (result != CORE_OK) return result;
    surface = find_surface(client, request.surface_id);
    if (surface == NULL) {
        return send_basic(service, client, GUI_SURFACE_COMMIT_RESULT,
                          request.prefix.request_id, CORE_NOT_FOUND);
    }
    if (request.commit_serial <= surface->last_commit_serial) {
        error = CORE_INVALID_STATE;
    } else if (surface->pending_error != CORE_OK) {
        error = surface->pending_error;
    } else if (surface->pending_frame && surface->committed_frame) {
        error = CORE_WOULD_BLOCK;
    }

    if (error != CORE_OK) {
        /* Subject errors are reported by commit, but valid pending state is
         * retained so the client can correct it with later pending requests. */
        surface->pending_error = CORE_OK;
        return send_basic(service, client, GUI_SURFACE_COMMIT_RESULT,
                          request.prefix.request_id, error);
    }

    if (surface->pending_attach_set && surface->pending_buffer_id != 0) {
        new_buffer = find_buffer(client, surface->pending_buffer_id);
        if (new_buffer == NULL || new_buffer->state != COMPOSITOR_BUFFER_PENDING ||
            new_buffer->surface_id != surface->surface_id) {
            return send_basic(service, client, GUI_SURFACE_COMMIT_RESULT,
                              request.prefix.request_id, CORE_INVALID_STATE);
        }
    }

    if (surface->current_buffer_set) {
        old_buffer = find_buffer(client, surface->current_buffer_id);
        if (old_buffer == NULL) {
            /* A destroyed in-use buffer is intentionally hidden by find_buffer. */
            usize buffer_index;
            for (buffer_index = 0; buffer_index < COMPOSITOR_SERVICE_BUFFER_MAX;
                 ++buffer_index) {
                CompositorBuffer *candidate = &client->buffers[buffer_index];
                if (candidate->active &&
                    candidate->buffer_id == surface->current_buffer_id) {
                    old_buffer = candidate;
                    break;
                }
            }
        }
    }

    if (surface->pending_attach_set) {
        if (old_buffer != NULL && old_buffer != new_buffer &&
            old_buffer->surface_id == surface->surface_id) {
            old_buffer->surface_id = 0;
        }
        if (new_buffer == NULL) {
            surface->current_buffer_set = false;
            surface->current_buffer_id = 0;
        } else {
            new_buffer->state = COMPOSITOR_BUFFER_IN_USE;
            new_buffer->surface_id = surface->surface_id;
            surface->current_buffer_set = true;
            surface->current_buffer_id = new_buffer->buffer_id;
        }
    }

    if (surface->pending_opaque_set) {
        surface->current_opaque_count = surface->pending_opaque_count;
        for (index = 0; index < surface->pending_opaque_count; ++index) {
            surface->current_opaque[index] = surface->pending_opaque[index];
        }
    }

    if (surface->pending_frame) {
        surface->committed_frame = true;
        surface->committed_frame_request_id = surface->pending_frame_request_id;
        surface->committed_frame_serial = request.commit_serial;
    }

    surface->last_commit_serial = request.commit_serial;
    surface->pending_attach_set = false;
    surface->pending_buffer_id = 0;
    surface->pending_full_damage = false;
    surface->pending_damage_count = 0;
    surface->pending_opaque_set = false;
    surface->pending_opaque_count = 0;
    surface->pending_frame = false;
    surface->pending_frame_request_id = 0;
    surface->pending_error = CORE_OK;
    service->frame_dirty = true;

    return send_basic(service, client, GUI_SURFACE_COMMIT_RESULT,
                      request.prefix.request_id, CORE_OK);
}

static ErrorCode handle_controller_bind(CompositorServiceHandle service,
                                        CompositorClient *client,
                                        const IpcMessage *message)
{
    GuiControllerBindRequest request;
    ErrorCode error;
    ErrorCode result = gui_decode_controller_bind(message, &request);
    if (result != CORE_OK) return result;

    if (!token_equal(request.controller_token, service->controller_token,
                     GUI_CONTROLLER_TOKEN_SIZE)) {
        error = CORE_PERMISSION_DENIED;
    } else if (service->controller_client != NULL &&
               service->controller_client != client) {
        error = CORE_ALREADY_EXISTS;
    } else {
        service->controller_client = client;
        error = CORE_OK;
    }
    return send_basic(service, client, GUI_COMPOSITOR_BIND_CONTROLLER_RESULT,
                      request.prefix.request_id, error);
}

static ErrorCode handle_scene_bind(CompositorServiceHandle service,
                                   CompositorClient *client,
                                   const IpcMessage *message)
{
    GuiSceneBindRequest request;
    CompositorSurface *surface;
    CompositorClient *owner = NULL;
    CompositorSceneNode *node;
    ErrorCode result = gui_decode_scene_bind(message, &request);
    if (result != CORE_OK) return result;
    if (service->controller_client != client) {
        return send_basic(service, client, GUI_SCENE_RESULT,
                          request.prefix.request_id, CORE_PERMISSION_DENIED);
    }
    if (find_scene_window(service, request.window_id) != NULL) {
        return send_basic(service, client, GUI_SCENE_RESULT,
                          request.prefix.request_id, CORE_ALREADY_EXISTS);
    }
    if (find_scene_surface(service, request.surface_id) != NULL) {
        return send_basic(service, client, GUI_SCENE_RESULT,
                          request.prefix.request_id, CORE_ALREADY_EXISTS);
    }
    surface = find_surface_global(service, request.surface_id, &owner);
    if (surface == NULL) {
        return send_basic(service, client, GUI_SCENE_RESULT,
                          request.prefix.request_id, CORE_NOT_FOUND);
    }
    if (!token_equal(surface->bind_token, request.surface_bind_token,
                     GUI_SURFACE_BIND_TOKEN_SIZE)) {
        return send_basic(service, client, GUI_SCENE_RESULT,
                          request.prefix.request_id, CORE_PERMISSION_DENIED);
    }
    node = find_free_scene_node(service);
    if (node == NULL) {
        return send_basic(service, client, GUI_SCENE_RESULT,
                          request.prefix.request_id, CORE_OUT_OF_MEMORY);
    }
    clear_scene_node(node);
    node->active = true;
    node->window_id = request.window_id;
    node->surface_id = request.surface_id;
    node->surface_owner = owner;
    service->frame_dirty = true;
    return send_basic(service, client, GUI_SCENE_RESULT,
                      request.prefix.request_id, CORE_OK);
}

static ErrorCode handle_scene_geometry(CompositorServiceHandle service,
                                       CompositorClient *client,
                                       const IpcMessage *message)
{
    GuiSceneGeometryRequest request;
    CompositorSceneNode *node;
    ErrorCode result = gui_decode_scene_geometry(message, &request);
    if (result != CORE_OK) return result;
    if (service->controller_client != client) {
        return send_basic(service, client, GUI_SCENE_RESULT,
                          request.prefix.request_id, CORE_PERMISSION_DENIED);
    }
    node = find_scene_window(service, request.window_id);
    if (node == NULL) return send_basic(service, client, GUI_SCENE_RESULT,
        request.prefix.request_id, CORE_NOT_FOUND);
    node->geometry = request.geometry;
    node->geometry_set = true;
    node->clip_x = request.clip_x;
    node->clip_y = request.clip_y;
    node->clip_width = request.clip_width;
    node->clip_height = request.clip_height;
    service->frame_dirty = true;
    return send_basic(service, client, GUI_SCENE_RESULT,
                      request.prefix.request_id, CORE_OK);
}

static ErrorCode handle_scene_visibility(CompositorServiceHandle service,
                                         CompositorClient *client,
                                         const IpcMessage *message)
{
    GuiSceneVisibilityRequest request;
    CompositorSceneNode *node;
    ErrorCode result = gui_decode_scene_visibility(message, &request);
    if (result != CORE_OK) return result;
    if (service->controller_client != client) return send_basic(service, client,
        GUI_SCENE_RESULT, request.prefix.request_id, CORE_PERMISSION_DENIED);
    node = find_scene_window(service, request.window_id);
    if (node == NULL) return send_basic(service, client, GUI_SCENE_RESULT,
        request.prefix.request_id, CORE_NOT_FOUND);
    if (request.visibility == GUI_SCENE_VISIBLE) {
        CompositorSurface *surface = find_surface(node->surface_owner,
                                                   node->surface_id);
        if (!node->geometry_set || surface == NULL ||
            !surface->current_buffer_set) {
            return send_basic(service, client, GUI_SCENE_RESULT,
                              request.prefix.request_id, CORE_INVALID_STATE);
        }
    }
    node->visibility = request.visibility;
    service->frame_dirty = true;
    return send_basic(service, client, GUI_SCENE_RESULT,
                      request.prefix.request_id, CORE_OK);
}

static ErrorCode handle_scene_stack(CompositorServiceHandle service,
                                    CompositorClient *client,
                                    const IpcMessage *message)
{
    GuiSceneStackRequest request;
    CompositorSceneNode *node;
    ErrorCode result = gui_decode_scene_stack(message, &request);
    if (result != CORE_OK) return result;
    if (service->controller_client != client) return send_basic(service, client,
        GUI_SCENE_RESULT, request.prefix.request_id, CORE_PERMISSION_DENIED);
    node = find_scene_window(service, request.window_id);
    if (node == NULL) return send_basic(service, client, GUI_SCENE_RESULT,
        request.prefix.request_id, CORE_NOT_FOUND);
    if (request.above_window_id != 0 &&
        find_scene_window(service, request.above_window_id) == NULL) {
        return send_basic(service, client, GUI_SCENE_RESULT,
                          request.prefix.request_id, CORE_NOT_FOUND);
    }
    node->layer = request.layer;
    node->above_window_id = request.above_window_id;
    service->frame_dirty = true;
    return send_basic(service, client, GUI_SCENE_RESULT,
                      request.prefix.request_id, CORE_OK);
}

static ErrorCode handle_scene_output(CompositorServiceHandle service,
                                     CompositorClient *client,
                                     const IpcMessage *message)
{
    GuiSceneOutputRequest request;
    CompositorSceneNode *node;
    ErrorCode result = gui_decode_scene_output(message, &request);
    if (result != CORE_OK) return result;
    if (service->controller_client != client) return send_basic(service, client,
        GUI_SCENE_RESULT, request.prefix.request_id, CORE_PERMISSION_DENIED);
    node = find_scene_window(service, request.window_id);
    if (node == NULL) return send_basic(service, client, GUI_SCENE_RESULT,
        request.prefix.request_id, CORE_NOT_FOUND);
    node->display_id = request.display_id;
    service->frame_dirty = true;
    return send_basic(service, client, GUI_SCENE_RESULT,
                      request.prefix.request_id, CORE_OK);
}

static ErrorCode handle_scene_remove(CompositorServiceHandle service,
                                     CompositorClient *client,
                                     const IpcMessage *message)
{
    GuiSceneWindowRequest request;
    CompositorSceneNode *node;
    ErrorCode result = gui_decode_scene_window(message, &request);
    if (result != CORE_OK) return result;
    if (service->controller_client != client) return send_basic(service, client,
        GUI_SCENE_RESULT, request.prefix.request_id, CORE_PERMISSION_DENIED);
    node = find_scene_window(service, request.window_id);
    if (node == NULL) return send_basic(service, client, GUI_SCENE_RESULT,
        request.prefix.request_id, CORE_NOT_FOUND);
    clear_scene_node(node);
    service->frame_dirty = true;
    return send_basic(service, client, GUI_SCENE_RESULT,
                      request.prefix.request_id, CORE_OK);
}

static void remove_scene_for_client(CompositorServiceHandle service,
                                    CompositorClient *client)
{
    usize index;
    for (index = 0; index < COMPOSITOR_SERVICE_SCENE_MAX; ++index) {
        CompositorSceneNode *node = &service->scene[index];
        if (node->active && node->surface_owner == client) {
            (void)notify_binding_lost(service, node,
                                      GUI_SCENE_BINDING_LOST_CLIENT_DISCONNECTED);
            clear_scene_node(node);
        }
    }
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
        return handle_surface_attach(service, client, message);
    case GUI_SURFACE_DAMAGE:
        return handle_surface_damage(service, client, message);
    case GUI_SURFACE_SET_OPAQUE:
        return handle_surface_opaque(service, client, message);
    case GUI_SURFACE_REQUEST_FRAME:
        return handle_surface_frame_request(service, client, message);
    case GUI_SURFACE_COMMIT:
        return handle_surface_commit(service, client, message);
    case GUI_COMPOSITOR_BIND_CONTROLLER:
        return handle_controller_bind(service, client, message);
    case GUI_SCENE_BIND_WINDOW:
        return handle_scene_bind(service, client, message);
    case GUI_SCENE_SET_GEOMETRY:
        return handle_scene_geometry(service, client, message);
    case GUI_SCENE_SET_VISIBILITY:
        return handle_scene_visibility(service, client, message);
    case GUI_SCENE_SET_STACK:
        return handle_scene_stack(service, client, message);
    case GUI_SCENE_SET_OUTPUT:
        return handle_scene_output(service, client, message);
    case GUI_SCENE_REMOVE_WINDOW:
        return handle_scene_remove(service, client, message);
    default:
        return CORE_UNSUPPORTED;
    }
}

static void remove_client(CompositorServiceHandle service,
                          CompositorClient *client)
{
    usize index;

    if (service->controller_client == client) {
        service->controller_client = NULL;
        clear_scene(service);
    } else {
        remove_scene_for_client(service, client);
    }
    drop_present_refs_for_client(service, client);
    for (index = 0; index < COMPOSITOR_SERVICE_SURFACE_MAX; ++index) {
        if (client->surfaces[index].active) {
            release_surface_pending_buffer(client, &client->surfaces[index]);
        }
    }
    (void)eventloop_remove_source(service->loop, client->source);
    (void)ipc_connection_close(&client->connection);
    clear_client_surfaces(client);
    clear_client_buffers(client);
    client->source = 0;
    client->next_event_serial = 1;
    service->frame_dirty = true;
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
            service->clients[index].next_event_serial = 1;
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
    config.display_endpoint = NULL;
    config.presenter_token = NULL;
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
        config->controller_token == NULL || out_service == NULL ||
        ((config->display_endpoint == NULL) !=
         (config->presenter_token == NULL))) {
        return CORE_INVALID_ARGUMENT;
    }

    result = memory_allocate(sizeof(*service), &block);
    if (result != CORE_OK) return result;

    service = (CompositorServiceHandle)block.data;
    service->handle_block = block;
    service->loop = NULL;
    service->server = NULL;
    service->server_source = 0;
    service->display_connection = NULL;
    service->display_source = 0;
    service->display_state = COMPOSITOR_DISPLAY_DISABLED;
    service->display_info_valid = false;
    service->next_display_request_id = 1;
    service->frame_dirty = false;
    service->pending_present.active = false;
    service->pending_present.request_id = 0;
    service->pending_present.ref_count = 0;
    service->pending_present.frame.fd = -1;
    service->pending_present.frame.reserved = 0;
    service->pending_present.frame.data = NULL;
    service->pending_present.frame.size = 0;

    for (index = 0; index < COMPOSITOR_SERVICE_CLIENT_MAX; ++index) {
        service->clients[index].connection = NULL;
        service->clients[index].source = 0;
        service->clients[index].next_event_serial = 1;
        clear_client_surfaces(&service->clients[index]);
        clear_client_buffers(&service->clients[index]);
    }
    service->controller_client = NULL;
    clear_scene(service);
    service->next_binding_event_serial = 1;
    service->next_surface_id = 1;
    service->next_buffer_id = 1;
    for (index = 0; index < COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE; ++index) {
        service->controller_token[index] = config->controller_token[index];
    }
    for (index = 0; index < COMPOSITOR_SERVICE_PRESENTER_TOKEN_SIZE; ++index) {
        service->presenter_token[index] = config->presenter_token == NULL ? 0 :
                                          config->presenter_token[index];
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

    if (config->display_endpoint != NULL) {
        result = display_begin_connect(service, config->display_endpoint);
        if (result != CORE_OK) goto fail_server_source;
    }

    *out_service = service;
    return CORE_OK;

fail_server_source:
    (void)eventloop_remove_source(service->loop, service->server_source);
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
        } else if (service->display_connection != NULL &&
                   events[index].source_id == service->display_source) {
            result = service_display_event(service, events[index].mask);
        } else {
            CompositorClient *client =
                find_client(service, events[index].source_id);
            result = client == NULL ? CORE_NOT_FOUND :
                service_client_event(service, client, events[index].mask);
        }
        if (result != CORE_OK) return result;
    }

    result = display_present_frame(service);
    return result;
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

    clear_pending_present(handle);
    if (handle->display_connection != NULL) {
        (void)eventloop_remove_source(handle->loop, handle->display_source);
        (void)ipc_connection_close(&handle->display_connection);
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
