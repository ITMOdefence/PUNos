#include <sys/graphics.h>

#include <sys/memory.h>

#include <punos/syscall/syscall.h>

#define GRAPHICS_MAX_DIMENSION ((u32)16384)

typedef struct QueuedEvent {
    GraphicsEvent event;
    struct GraphicsFrameHandle *frame;
} QueuedEvent;

struct GraphicsDeviceHandle {
    MemoryBlock handle_block;
    i32 event_fd;
    bool enabled;
    u32 orientation;
    GraphicsMode mode;
    MemoryBlock framebuffer;
    QueuedEvent events[GRAPHICS_PRESENT_QUEUE_DEPTH];
    usize event_head;
    usize event_count;
};

struct GraphicsFrameHandle {
    MemoryBlock handle_block;
    GraphicsFrameDescriptor descriptor;
    i32 fd;
    SharedMemoryMapping mapping;
    bool queued;
};

static ErrorCode last_error(void)
{
    return core_error_from_errno(errno);
}

static ErrorCode validate_mode(GraphicsMode mode)
{
    if (mode.width_px == 0 || mode.height_px == 0 ||
        mode.refresh_millihz == 0 || mode.width_px > GRAPHICS_MAX_DIMENSION ||
        mode.height_px > GRAPHICS_MAX_DIMENSION) {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

static ErrorCode framebuffer_size(GraphicsMode mode, usize *out_size)
{
    usize pixels;
    ErrorCode result = core_size_multiply(mode.width_px, mode.height_px,
                                           &pixels);
    if (result != CORE_OK) {
        return result;
    }
    return core_size_multiply(pixels, 4, out_size);
}

static ErrorCode validate_descriptor(const GraphicsFrameDescriptor *descriptor)
{
    u64 row_size;
    u64 minimum_size;

    if (descriptor == NULL || descriptor->width == 0 ||
        descriptor->height == 0 || descriptor->width > GRAPHICS_MAX_DIMENSION ||
        descriptor->height > GRAPHICS_MAX_DIMENSION) {
        return CORE_INVALID_ARGUMENT;
    }
    if (descriptor->format != GRAPHICS_PIXEL_XRGB8888 &&
        descriptor->format != GRAPHICS_PIXEL_ARGB8888) {
        return CORE_UNSUPPORTED;
    }
    row_size = (u64)descriptor->width * 4;
    if ((u64)descriptor->stride < row_size) {
        return CORE_INVALID_ARGUMENT;
    }
    minimum_size = (u64)descriptor->stride * descriptor->height;
    if (descriptor->byte_size < minimum_size ||
        descriptor->byte_size > USIZE_MAX) {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

ErrorCode graphics_device_open(const GraphicsDeviceConfig *config,
                               GraphicsDeviceHandle *out_device)
{
    MemoryBlock handle_block;
    MemoryBlock framebuffer = { NULL, 0 };
    struct GraphicsDeviceHandle *device;
    usize size;
    i32 event_fd;
    ErrorCode result;

    if (config == NULL || out_device == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_mode(config->initial_mode);
    if (result != CORE_OK) {
        return result;
    }
    if (config->enabled) {
        result = framebuffer_size(config->initial_mode, &size);
        if (result != CORE_OK) {
            return result;
        }
        result = memory_allocate(size, &framebuffer);
        if (result != CORE_OK) {
            return result;
        }
    }
    event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC | EFD_SEMAPHORE);
    if (event_fd < 0) {
        result = last_error();
        (void)memory_free(&framebuffer);
        return result;
    }
    result = memory_allocate(sizeof(*device), &handle_block);
    if (result != CORE_OK) {
        (void)close(event_fd);
        (void)memory_free(&framebuffer);
        return result;
    }
    device = (struct GraphicsDeviceHandle *)handle_block.data;
    device->handle_block = handle_block;
    device->event_fd = event_fd;
    device->enabled = config->enabled;
    device->orientation = GRAPHICS_ORIENTATION_NORMAL;
    device->mode = config->initial_mode;
    device->framebuffer = framebuffer;
    device->event_head = 0;
    device->event_count = 0;
    *out_device = device;
    return CORE_OK;
}

ErrorCode graphics_device_get_fd(GraphicsDeviceHandle device, i32 *out_fd)
{
    if (device == NULL || out_fd == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_fd = device->event_fd;
    return CORE_OK;
}

ErrorCode graphics_device_get_output(GraphicsDeviceHandle device,
                                     GraphicsOutputInfo *out_output)
{
    if (device == NULL || out_output == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    out_output->output_id = GRAPHICS_SOFTWARE_OUTPUT_ID;
    out_output->connected = true;
    out_output->enabled = device->enabled;
    out_output->reserved = 0;
    out_output->physical_width_mm =
        (u32)(((u64)device->mode.width_px * 254) / 1600);
    out_output->physical_height_mm =
        (u32)(((u64)device->mode.height_px * 254) / 1600);
    out_output->orientation = device->orientation;
    out_output->current_mode = device->mode;
    return CORE_OK;
}

ErrorCode graphics_output_configure(GraphicsDeviceHandle device,
                                    bool enabled, GraphicsMode mode,
                                    u32 orientation)
{
    MemoryBlock framebuffer = { NULL, 0 };
    usize size;
    ErrorCode result;

    if (device == NULL || orientation > GRAPHICS_ORIENTATION_270) {
        return CORE_INVALID_ARGUMENT;
    }
    if (device->event_count != 0) {
        return CORE_INVALID_STATE;
    }
    result = validate_mode(mode);
    if (result != CORE_OK) {
        return result;
    }
    if (enabled) {
        GraphicsMode framebuffer_mode = mode;
        if (orientation == GRAPHICS_ORIENTATION_90 ||
            orientation == GRAPHICS_ORIENTATION_270) {
            framebuffer_mode.width_px = mode.height_px;
            framebuffer_mode.height_px = mode.width_px;
        }
        result = framebuffer_size(framebuffer_mode, &size);
        if (result != CORE_OK) {
            return result;
        }
        result = memory_allocate(size, &framebuffer);
        if (result != CORE_OK) {
            return result;
        }
    }
    result = memory_free(&device->framebuffer);
    if (result != CORE_OK) {
        (void)memory_free(&framebuffer);
        return result;
    }
    device->framebuffer = framebuffer;
    device->mode = mode;
    device->orientation = orientation;
    device->enabled = enabled;
    return CORE_OK;
}

ErrorCode graphics_frame_import(GraphicsDeviceHandle device,
                                const GraphicsFrameDescriptor *descriptor,
                                i32 fd, GraphicsFrameHandle *out_frame)
{
    SharedMemoryMapping mapping;
    MemoryBlock block;
    struct GraphicsFrameHandle *frame;
    ErrorCode result;

    if (device == NULL || out_frame == NULL || fd < 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_descriptor(descriptor);
    if (result != CORE_OK) {
        return result;
    }
    result = memory_shared_map_readonly(fd, (usize)descriptor->byte_size,
                                        &mapping);
    if (result != CORE_OK) {
        return result;
    }
    result = memory_allocate(sizeof(*frame), &block);
    if (result != CORE_OK) {
        (void)memory_shared_unmap(&mapping);
        return result;
    }
    frame = (struct GraphicsFrameHandle *)block.data;
    frame->handle_block = block;
    frame->descriptor = *descriptor;
    frame->fd = fd;
    frame->mapping = mapping;
    frame->queued = false;
    *out_frame = frame;
    return CORE_OK;
}

static ErrorCode copy_frame(GraphicsDeviceHandle device,
                            GraphicsFrameHandle frame)
{
    usize row;
    usize row_size = (usize)frame->descriptor.width * 4;

    for (row = 0; row < frame->descriptor.height; ++row) {
        Bytes destination = {
            device->framebuffer.data + row * row_size, row_size
        };
        Bytes source = {
            (u8 *)frame->mapping.data + row * frame->descriptor.stride,
            row_size
        };
        ErrorCode result = memory_copy(destination, source);
        if (result != CORE_OK) {
            return result;
        }
    }
    return CORE_OK;
}

ErrorCode graphics_output_present(GraphicsDeviceHandle device, u64 output_id,
                                  GraphicsFrameHandle frame,
                                  const GraphicsDamage *damage,
                                  u64 present_id)
{
    usize tail;
    usize index;
    u64 wake = 1;
    ErrorCode result;

    if (device == NULL || frame == NULL || damage == NULL || present_id == 0 ||
        output_id != GRAPHICS_SOFTWARE_OUTPUT_ID ||
        damage->count > GRAPHICS_DAMAGE_MAX_RECTS ||
        (damage->count != 0 && damage->rects == NULL) ||
        (damage->full && damage->count != 0)) {
        return CORE_INVALID_ARGUMENT;
    }
    for (index = 0; index < damage->count; ++index) {
        if (damage->rects[index].width == 0 ||
            damage->rects[index].height == 0) {
            return CORE_INVALID_ARGUMENT;
        }
    }
    if (!device->enabled || frame->queued) {
        return CORE_INVALID_STATE;
    }
    if (device->orientation == GRAPHICS_ORIENTATION_90 ||
        device->orientation == GRAPHICS_ORIENTATION_270) {
        if (frame->descriptor.width != device->mode.height_px ||
            frame->descriptor.height != device->mode.width_px) {
            return CORE_INVALID_STATE;
        }
    } else if (frame->descriptor.width != device->mode.width_px ||
               frame->descriptor.height != device->mode.height_px) {
        return CORE_INVALID_STATE;
    }
    if (device->event_count == GRAPHICS_PRESENT_QUEUE_DEPTH) {
        return CORE_WOULD_BLOCK;
    }
    result = copy_frame(device, frame);
    if (result != CORE_OK) {
        return result;
    }
    tail = (device->event_head + device->event_count) %
           GRAPHICS_PRESENT_QUEUE_DEPTH;
    device->events[tail].event.type = GRAPHICS_EVENT_PRESENT_COMPLETE;
    device->events[tail].event.error = CORE_OK;
    device->events[tail].event.output_id = output_id;
    device->events[tail].event.present_id = present_id;
    device->events[tail].frame = frame;
    if (write(device->event_fd, &wake, sizeof(wake)) != (ssize_t)sizeof(wake)) {
        return last_error();
    }
    frame->queued = true;
    ++device->event_count;
    return CORE_OK;
}

ErrorCode graphics_device_next_event(GraphicsDeviceHandle device,
                                     GraphicsEventStatus *out_status,
                                     GraphicsEvent *out_event)
{
    QueuedEvent *queued;
    u64 wake;

    if (device == NULL || out_status == NULL || out_event == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (device->event_count == 0) {
        *out_status = GRAPHICS_EVENT_NONE;
        return CORE_OK;
    }
    if (read(device->event_fd, &wake, sizeof(wake)) != (ssize_t)sizeof(wake)) {
        return last_error();
    }
    queued = &device->events[device->event_head];
    *out_event = queued->event;
    queued->frame->queued = false;
    device->event_head = (device->event_head + 1) %
                         GRAPHICS_PRESENT_QUEUE_DEPTH;
    --device->event_count;
    *out_status = GRAPHICS_EVENT_READY;
    return CORE_OK;
}

ErrorCode graphics_frame_destroy(GraphicsFrameHandle *frame)
{
    struct GraphicsFrameHandle *handle;
    MemoryBlock block;
    ErrorCode result = CORE_OK;

    if (frame == NULL || *frame == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    handle = *frame;
    if (handle->queued) {
        return CORE_INVALID_STATE;
    }
    if (memory_shared_unmap(&handle->mapping) != CORE_OK) {
        result = CORE_IO_ERROR;
    }
    if (close(handle->fd) != 0 && result == CORE_OK) {
        result = last_error();
    }
    block = handle->handle_block;
    if (memory_free(&block) != CORE_OK && result == CORE_OK) {
        result = CORE_IO_ERROR;
    }
    *frame = NULL;
    return result;
}

ErrorCode graphics_device_close(GraphicsDeviceHandle *device)
{
    struct GraphicsDeviceHandle *handle;
    MemoryBlock block;
    ErrorCode result = CORE_OK;

    if (device == NULL || *device == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    handle = *device;
    if (handle->event_count != 0) {
        return CORE_INVALID_STATE;
    }
    if (close(handle->event_fd) != 0) {
        result = last_error();
    }
    (void)memory_free(&handle->framebuffer);
    block = handle->handle_block;
    if (memory_free(&block) != CORE_OK && result == CORE_OK) {
        result = CORE_IO_ERROR;
    }
    *device = NULL;
    return result;
}
