#include <special/display.h>

#include <sys/graphics.h>
#include <sys/memory.h>

struct DisplayBackendHandle {
    MemoryBlock handle_block;
    GraphicsDeviceHandle device;
    bool lost;
};

struct DisplayFrameHandle {
    MemoryBlock handle_block;
    DisplayBackendHandle owner;
    GraphicsFrameHandle frame;
};

static bool string_equals_literal(const String *value, const char *literal)
{
    const char *data;
    usize index = 0;

    if (value == NULL || string_c_str(value, &data) != CORE_OK) {
        return false;
    }
    while (literal[index] != '\0') {
        if (index >= value->length || data[index] != literal[index]) {
            return false;
        }
        ++index;
    }
    return index == value->length;
}

static DisplayMode display_mode(GraphicsMode mode)
{
    DisplayMode result = {
        mode.width_px, mode.height_px, mode.refresh_millihz
    };
    return result;
}

ErrorCode display_backend_open(const DisplayBackendConfig *config,
                               DisplayBackendHandle *out_backend)
{
    static const GraphicsDeviceConfig device_config = {
        { 1080, 1920, 60000 }, true
    };
    MemoryBlock block;
    struct DisplayBackendHandle *backend;
    GraphicsDeviceHandle device;
    const char *ignored_path;
    ErrorCode result;

    if (config == NULL || out_backend == NULL || config->backend_name == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (!string_equals_literal(config->backend_name, "software")) {
        return CORE_UNSUPPORTED;
    }
    if (config->device_path != NULL &&
        string_c_str(config->device_path, &ignored_path) != CORE_OK) {
        return CORE_INVALID_PATH;
    }
    result = memory_allocate(sizeof(*backend), &block);
    if (result != CORE_OK) {
        return result;
    }
    result = graphics_device_open(&device_config, &device);
    if (result != CORE_OK) {
        (void)memory_free(&block);
        return result;
    }
    backend = (struct DisplayBackendHandle *)block.data;
    backend->handle_block = block;
    backend->device = device;
    backend->lost = false;
    *out_backend = backend;
    return CORE_OK;
}

ErrorCode display_backend_get_fd(DisplayBackendHandle backend, i32 *out_fd)
{
    if (backend == NULL || out_fd == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (backend->lost) {
        return CORE_INVALID_STATE;
    }
    return graphics_device_get_fd(backend->device, out_fd);
}

ErrorCode display_backend_list_outputs(DisplayBackendHandle backend,
                                       DisplayOutputInfoList *out_outputs)
{
    MemoryBlock items_block;
    MemoryBlock modes_block;
    DisplayOutputInfo *item;
    GraphicsOutputInfo output;
    ErrorCode result;

    if (backend == NULL || out_outputs == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (backend->lost) {
        return CORE_INVALID_STATE;
    }
    result = graphics_device_get_output(backend->device, &output);
    if (result != CORE_OK) {
        return result;
    }
    result = memory_allocate(sizeof(DisplayOutputInfo), &items_block);
    if (result != CORE_OK) {
        return result;
    }
    result = memory_allocate(sizeof(DisplayMode), &modes_block);
    if (result != CORE_OK) {
        (void)memory_free(&items_block);
        return result;
    }
    item = (DisplayOutputInfo *)items_block.data;
    result = string_from_c_str("software-display", &item->name);
    if (result != CORE_OK) {
        (void)memory_free(&modes_block);
        (void)memory_free(&items_block);
        return result;
    }
    item->backend_output_id = output.output_id;
    item->connected = output.connected;
    item->enabled = output.enabled;
    item->reserved = 0;
    item->physical_width_mm = output.physical_width_mm;
    item->physical_height_mm = output.physical_height_mm;
    item->current_mode = display_mode(output.current_mode);
    item->modes = (DisplayMode *)modes_block.data;
    item->modes[0] = item->current_mode;
    item->mode_count = 1;
    out_outputs->items = item;
    out_outputs->count = 1;
    return CORE_OK;
}

ErrorCode display_output_info_list_destroy(DisplayOutputInfoList *outputs)
{
    MemoryBlock items;
    ErrorCode result = CORE_OK;
    usize index;

    if (outputs == NULL || outputs->items == NULL || outputs->count == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    for (index = 0; index < outputs->count; ++index) {
        DisplayOutputInfo *item = &outputs->items[index];
        MemoryBlock modes = {
            (u8 *)item->modes, item->mode_count * sizeof(DisplayMode)
        };
        if (string_destroy(&item->name) != CORE_OK) {
            result = CORE_IO_ERROR;
        }
        if (memory_free(&modes) != CORE_OK) {
            result = CORE_IO_ERROR;
        }
    }
    items.data = (u8 *)outputs->items;
    items.size = outputs->count * sizeof(DisplayOutputInfo);
    if (memory_free(&items) != CORE_OK) {
        result = CORE_IO_ERROR;
    }
    outputs->items = NULL;
    outputs->count = 0;
    return result;
}

ErrorCode display_output_configure(DisplayBackendHandle backend, u64 output_id,
                                   const DisplayOutputConfig *config)
{
    GraphicsMode mode;

    if (backend == NULL || config == NULL ||
        output_id != GRAPHICS_SOFTWARE_OUTPUT_ID ||
        config->orientation > DISPLAY_ORIENTATION_270) {
        return CORE_INVALID_ARGUMENT;
    }
    if (backend->lost) {
        return CORE_INVALID_STATE;
    }
    mode.width_px = config->mode.width_px;
    mode.height_px = config->mode.height_px;
    mode.refresh_millihz = config->mode.refresh_millihz;
    if (!config->enabled && mode.width_px == 0 && mode.height_px == 0 &&
        mode.refresh_millihz == 0) {
        GraphicsOutputInfo output;
        ErrorCode result = graphics_device_get_output(backend->device, &output);
        if (result != CORE_OK) {
            return result;
        }
        mode = output.current_mode;
    }
    return graphics_output_configure(backend->device, config->enabled, mode,
                                     config->orientation);
}

ErrorCode display_frame_import(DisplayBackendHandle backend,
                               const DisplayFrameDescriptor *descriptor,
                               i32 fd, DisplayFrameHandle *out_frame)
{
    MemoryBlock block;
    struct DisplayFrameHandle *frame;
    GraphicsFrameDescriptor graphics_descriptor;
    GraphicsFrameHandle graphics_frame;
    ErrorCode result;

    if (backend == NULL || descriptor == NULL || out_frame == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (backend->lost) {
        return CORE_INVALID_STATE;
    }
    result = memory_allocate(sizeof(*frame), &block);
    if (result != CORE_OK) {
        return result;
    }
    graphics_descriptor.width = descriptor->width;
    graphics_descriptor.height = descriptor->height;
    graphics_descriptor.stride = descriptor->stride;
    graphics_descriptor.format = descriptor->format;
    graphics_descriptor.byte_size = descriptor->byte_size;
    result = graphics_frame_import(backend->device, &graphics_descriptor, fd,
                                   &graphics_frame);
    if (result != CORE_OK) {
        (void)memory_free(&block);
        return result;
    }
    frame = (struct DisplayFrameHandle *)block.data;
    frame->handle_block = block;
    frame->owner = backend;
    frame->frame = graphics_frame;
    *out_frame = frame;
    return CORE_OK;
}

ErrorCode display_output_present(DisplayBackendHandle backend, u64 output_id,
                                 DisplayFrameHandle frame,
                                 const DisplayDamage *damage,
                                 u64 present_id)
{
    GraphicsRect rects[DISPLAY_DAMAGE_MAX_RECTS];
    GraphicsDamage graphics_damage;
    usize index;

    if (backend == NULL || frame == NULL || damage == NULL ||
        frame->owner != backend || damage->count > DISPLAY_DAMAGE_MAX_RECTS ||
        (damage->count != 0 && damage->rects == NULL)) {
        return CORE_INVALID_ARGUMENT;
    }
    if (backend->lost) {
        return CORE_INVALID_STATE;
    }
    for (index = 0; index < damage->count; ++index) {
        rects[index].x = damage->rects[index].x;
        rects[index].y = damage->rects[index].y;
        rects[index].width = damage->rects[index].width;
        rects[index].height = damage->rects[index].height;
    }
    graphics_damage.rects = damage->count == 0 ? NULL : rects;
    graphics_damage.count = damage->count;
    graphics_damage.full = damage->full;
    return graphics_output_present(backend->device, output_id, frame->frame,
                                   &graphics_damage, present_id);
}

ErrorCode display_backend_next_event(DisplayBackendHandle backend,
                                     DisplayEventStatus *out_status,
                                     DisplayBackendEvent *out_event)
{
    GraphicsEventStatus status;
    GraphicsEvent event;
    ErrorCode result;

    if (backend == NULL || out_status == NULL || out_event == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (backend->lost) {
        *out_status = DISPLAY_EVENT_BACKEND_LOST;
        return CORE_OK;
    }
    result = graphics_device_next_event(backend->device, &status, &event);
    if (result != CORE_OK) {
        return result;
    }
    if (status == GRAPHICS_EVENT_NONE) {
        *out_status = DISPLAY_EVENT_NO_EVENT;
        return CORE_OK;
    }
    out_event->error = event.error;
    out_event->backend_output_id = event.output_id;
    out_event->present_id = event.present_id;
    if (event.type == GRAPHICS_EVENT_PRESENT_COMPLETE) {
        out_event->type = DISPLAY_BACKEND_EVENT_PRESENT_COMPLETE;
    } else if (event.type == GRAPHICS_EVENT_PRESENT_FAILED) {
        out_event->type = DISPLAY_BACKEND_EVENT_PRESENT_FAILED;
    } else {
        backend->lost = true;
        out_event->type = DISPLAY_BACKEND_EVENT_LOST;
        *out_status = DISPLAY_EVENT_BACKEND_LOST;
        return CORE_OK;
    }
    *out_status = DISPLAY_EVENT_READY;
    return CORE_OK;
}

ErrorCode display_frame_destroy(DisplayFrameHandle *frame)
{
    struct DisplayFrameHandle *handle;
    MemoryBlock block;
    ErrorCode result;

    if (frame == NULL || *frame == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    handle = *frame;
    result = graphics_frame_destroy(&handle->frame);
    if (result != CORE_OK) {
        return result;
    }
    block = handle->handle_block;
    result = memory_free(&block);
    if (result == CORE_OK) {
        *frame = NULL;
    }
    return result;
}

ErrorCode display_backend_close(DisplayBackendHandle *backend)
{
    struct DisplayBackendHandle *handle;
    MemoryBlock block;
    ErrorCode result;

    if (backend == NULL || *backend == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    handle = *backend;
    result = graphics_device_close(&handle->device);
    if (result != CORE_OK) {
        return result;
    }
    block = handle->handle_block;
    result = memory_free(&block);
    if (result == CORE_OK) {
        *backend = NULL;
    }
    return result;
}
