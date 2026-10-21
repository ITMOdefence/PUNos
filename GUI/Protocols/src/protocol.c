#include <gui/protocol.h>

#define REQUEST_PREFIX_SIZE ((usize)10)
#define RECT_SIZE ((usize)16)
#define BUFFER_IMPORT_SIZE ((usize)34)
#define SURFACE_DAMAGE_BASE_SIZE ((usize)24)
#define SURFACE_ID_REQUEST_SIZE ((usize)18)
#define BUFFER_ID_REQUEST_SIZE ((usize)18)
#define SURFACE_ATTACH_SIZE ((usize)26)
#define SURFACE_FRAME_REQUEST_SIZE ((usize)18)
#define SURFACE_COMMIT_SIZE ((usize)26)
#define SURFACE_REGION_BASE_SIZE ((usize)20)
#define SURFACE_CREATE_RESULT_SIZE ((usize)36)
#define BUFFER_IMPORT_RESULT_SIZE ((usize)20)
#define SURFACE_FRAME_DONE_SIZE ((usize)32)
#define BUFFER_RELEASED_SIZE ((usize)16)
#define SURFACE_OUTPUT_EVENT_SIZE ((usize)24)
#define SURFACE_REDRAW_SIZE ((usize)20)
#define DISPLAY_PRESENT_BASE_SIZE ((usize)56)
#define PRESENTER_BIND_SIZE ((usize)42)
#define DISPLAY_CONFIGURE_SIZE ((usize)40)
#define BASIC_RESULT_SIZE ((usize)12)
#define DISPLAY_PRESENTED_SIZE ((usize)32)
#define DISPLAY_PRESENT_FAILED_SIZE ((usize)28)
#define DISPLAY_BACKEND_LOST_SIZE ((usize)12)
#define VERSIONED_REQUEST_SIZE ((usize)10)
#define DISPLAY_ID_REQUEST_SIZE ((usize)18)
#define DISPLAY_INFO_FIXED_SIZE ((usize)44)
#define DISPLAY_LIST_FIXED_SIZE ((usize)22)
#define DISPLAY_REMOVED_SIZE ((usize)24)
#define IO_SUBSCRIBE_SIZE ((usize)6)
#define IO_RESULT_SIZE ((usize)4)
#define IO_DEVICE_EVENT_SIZE ((usize)20)
#define IO_INPUT_FRAME_BASE_SIZE ((usize)18)
#define IO_INPUT_EVENT_SIZE ((usize)7)
#define CONTROLLER_BIND_SIZE ((usize)42)
#define SCENE_BIND_SIZE ((usize)42)
#define SCENE_GEOMETRY_SIZE ((usize)50)
#define SCENE_VISIBILITY_SIZE ((usize)22)
#define SCENE_STACK_SIZE ((usize)30)
#define SCENE_OUTPUT_SIZE ((usize)26)
#define SCENE_WINDOW_SIZE ((usize)18)
#define SCENE_BINDING_LOST_SIZE ((usize)28)
#define WINDOW_CREATE_BASE_SIZE ((usize)49)
#define WINDOW_CREATE_RESULT_SIZE ((usize)20)
#define WINDOW_ID_REQUEST_SIZE ((usize)18)
#define WINDOW_SET_TITLE_BASE_SIZE ((usize)20)
#define WINDOW_ACK_CONFIGURE_SIZE ((usize)26)
#define WINDOW_MODAL_SIZE ((usize)26)
#define WINDOW_SURFACE_REBIND_SIZE ((usize)42)
#define SHELL_BIND_SIZE ((usize)42)
#define SHELL_ACTIVATE_SIZE ((usize)18)
#define SHELL_INSETS_SIZE ((usize)34)
#define SHELL_NAVIGATE_SIZE ((usize)19)
#define WINDOW_CLOSE_RESPONSE_SIZE ((usize)27)
#define WINDOW_CONFIGURE_SIZE ((usize)45)
#define WINDOW_LIFECYCLE_SIZE ((usize)18)
#define WINDOW_CLOSE_SIZE ((usize)16)
#define WINDOW_VISIBILITY_SIZE ((usize)17)
#define WINDOW_POINTER_SIZE ((usize)45)
#define WINDOW_KEY_SIZE ((usize)33)
#define WINDOW_TOUCH_SIZE ((usize)37)
#define WINDOW_SERVICE_SIZE ((usize)17)
#define WINDOW_NAVIGATION_SIZE ((usize)17)
#define DISPLAY_FLAGS_KNOWN (GUI_DISPLAY_CONNECTED | GUI_DISPLAY_ENABLED | \
                             GUI_DISPLAY_PRIMARY)
#define INPUT_DEVICE_CLASSES_KNOWN (GUI_INPUT_DEVICE_KEYBOARD | \
                                    GUI_INPUT_DEVICE_POINTER | \
                                    GUI_INPUT_DEVICE_TOUCH)
#define WINDOW_FLAGS_KNOWN (GUI_WINDOW_FLAG_EDGE_TO_EDGE | \
                            GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD | \
                            GUI_WINDOW_FLAG_HANDLES_BACK)
#define MODIFIER_MASK_KNOWN (GUI_MODIFIER_SHIFT | GUI_MODIFIER_CONTROL | \
                             GUI_MODIFIER_ALT | GUI_MODIFIER_SUPER | \
                             GUI_MODIFIER_CAPS_LOCK | GUI_MODIFIER_NUM_LOCK)
#define MAX_BUFFER_DIMENSION ((u32)16384)

static void put_u16(u8 *data, u16 value)
{
    data[0] = (u8)value;
    data[1] = (u8)(value >> 8);
}

static void put_u32(u8 *data, u32 value)
{
    data[0] = (u8)value;
    data[1] = (u8)(value >> 8);
    data[2] = (u8)(value >> 16);
    data[3] = (u8)(value >> 24);
}

static void put_u64(u8 *data, u64 value)
{
    usize index;
    for (index = 0; index < 8; ++index) {
        data[index] = (u8)(value >> (index * 8));
    }
}

static u16 get_u16(const u8 *data)
{
    return (u16)((u16)data[0] | ((u16)data[1] << 8));
}

static u32 get_u32(const u8 *data)
{
    return (u32)data[0] | ((u32)data[1] << 8) |
           ((u32)data[2] << 16) | ((u32)data[3] << 24);
}

static u64 get_u64(const u8 *data)
{
    u64 value = 0;
    usize index;
    for (index = 0; index < 8; ++index) {
        value |= (u64)data[index] << (index * 8);
    }
    return value;
}

static ErrorCode validate_prefix(GuiRequestPrefix prefix)
{
    if (prefix.protocol_version != GUI_PROTOCOL_VERSION) {
        return CORE_UNSUPPORTED;
    }
    return prefix.request_id == 0 ? CORE_INVALID_ARGUMENT : CORE_OK;
}

static void encode_prefix(u8 *data, GuiRequestPrefix prefix)
{
    put_u16(data, prefix.protocol_version);
    put_u64(data + 2, prefix.request_id);
}

static ErrorCode decode_prefix(const u8 *data, GuiRequestPrefix *out_prefix)
{
    out_prefix->protocol_version = get_u16(data);
    out_prefix->request_id = get_u64(data + 2);
    return validate_prefix(*out_prefix);
}

static ErrorCode validate_storage(u32 width, u32 height, u32 stride,
                                  GuiPixelFormat format, u64 byte_size)
{
    u64 minimum_stride;
    u64 minimum_size;

    if (width == 0 || height == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    if (format != GUI_PIXEL_XRGB8888 && format != GUI_PIXEL_ARGB8888) {
        return CORE_UNSUPPORTED;
    }
    minimum_stride = (u64)width * 4;
    if ((u64)stride < minimum_stride) {
        return CORE_INVALID_ARGUMENT;
    }
    minimum_size = (u64)stride * (u64)height;
    return byte_size < minimum_size ? CORE_INVALID_ARGUMENT : CORE_OK;
}

static bool error_is_known(ErrorCode error)
{
    return (u32)error <= (u32)CORE_OVERFLOW;
}

static bool utf8_is_valid(const u8 *data, usize size)
{
    usize index = 0;

    while (index < size) {
        u8 lead = data[index];
        u8 second_min = 0x80;
        u8 second_max = 0xbf;
        usize count;
        usize offset;

        if (lead == 0 || lead == 0xc0 || lead == 0xc1 || lead > 0xf4) {
            return false;
        }
        if (lead < 0x80) {
            ++index;
            continue;
        }
        if (lead <= 0xdf) {
            count = 2;
        } else if (lead <= 0xef) {
            count = 3;
            if (lead == 0xe0) second_min = 0xa0;
            if (lead == 0xed) second_max = 0x9f;
        } else {
            count = 4;
            if (lead == 0xf0) second_min = 0x90;
            if (lead == 0xf4) second_max = 0x8f;
        }
        if (size - index < count || data[index + 1] < second_min ||
            data[index + 1] > second_max) {
            return false;
        }
        for (offset = 2; offset < count; ++offset) {
            if (data[index + offset] < 0x80 || data[index + offset] > 0xbf) {
                return false;
            }
        }
        index += count;
    }
    return true;
}

static bool modes_equal(GuiDisplayMode left, GuiDisplayMode right)
{
    return left.width_px == right.width_px &&
           left.height_px == right.height_px &&
           left.refresh_millihz == right.refresh_millihz;
}

static ErrorCode validate_display_info(const GuiDisplayInfo *info)
{
    usize index;
    bool current_found = false;

    if (info->display_id == 0 || info->generation == 0 ||
        (info->flags & ~DISPLAY_FLAGS_KNOWN) != 0 ||
        info->name_length == 0 || info->name_length > GUI_DISPLAY_NAME_MAX ||
        info->mode_count > GUI_DISPLAY_MODE_MAX ||
        !utf8_is_valid(info->name, info->name_length)) {
        return CORE_INVALID_ARGUMENT;
    }
    for (index = 0; index < info->mode_count; ++index) {
        usize previous;
        GuiDisplayMode mode = info->modes[index];
        if (mode.width_px == 0 || mode.height_px == 0 ||
            mode.refresh_millihz == 0) {
            return CORE_INVALID_ARGUMENT;
        }
        if (modes_equal(mode, info->current_mode)) {
            current_found = true;
        }
        for (previous = 0; previous < index; ++previous) {
            if (modes_equal(mode, info->modes[previous])) {
                return CORE_INVALID_ARGUMENT;
            }
        }
    }
    if ((info->flags & GUI_DISPLAY_ENABLED) != 0) {
        return current_found ? CORE_OK : CORE_INVALID_ARGUMENT;
    }
    return info->current_mode.width_px == 0 &&
           info->current_mode.height_px == 0 &&
           info->current_mode.refresh_millihz == 0 ? CORE_OK :
           CORE_INVALID_ARGUMENT;
}

static ErrorCode validate_mode(bool enabled, GuiDisplayOrientation orientation,
                               GuiDisplayMode mode)
{
    if (orientation > GUI_ORIENTATION_LEFT) {
        return CORE_INVALID_ARGUMENT;
    }
    if (!enabled) {
        return mode.width_px == 0 && mode.height_px == 0 &&
               mode.refresh_millihz == 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
    }
    return mode.width_px != 0 && mode.height_px != 0 &&
           mode.refresh_millihz != 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
}

static ErrorCode validate_rects(u32 flags, u16 count, const GuiRect *rects)
{
    usize index;

    if ((flags & ~GUI_FULL_DAMAGE) != 0 || count > GUI_MAX_DAMAGE_RECTS ||
        ((flags & GUI_FULL_DAMAGE) != 0 && count != 0)) {
        return CORE_INVALID_ARGUMENT;
    }
    for (index = 0; index < count; ++index) {
        if (rects[index].width == 0 || rects[index].height == 0) {
            return CORE_INVALID_ARGUMENT;
        }
    }
    return CORE_OK;
}

static ErrorCode validate_region_rects(u16 count, const GuiRect *rects)
{
    usize index;

    if (count > GUI_MAX_DAMAGE_RECTS) {
        return CORE_INVALID_ARGUMENT;
    }
    for (index = 0; index < count; ++index) {
        if (rects[index].width == 0 || rects[index].height == 0) {
            return CORE_INVALID_ARGUMENT;
        }
    }
    return CORE_OK;
}

static void encode_rect(u8 *data, const GuiRect *rect)
{
    put_u32(data, (u32)rect->x);
    put_u32(data + 4, (u32)rect->y);
    put_u32(data + 8, rect->width);
    put_u32(data + 12, rect->height);
}

static void decode_rect(const u8 *data, GuiRect *out_rect)
{
    out_rect->x = (i32)get_u32(data);
    out_rect->y = (i32)get_u32(data + 4);
    out_rect->width = get_u32(data + 8);
    out_rect->height = get_u32(data + 12);
}

static ErrorCode variable_size(usize base, u16 count, usize *out_size)
{
    usize rect_bytes;
    ErrorCode result = core_size_multiply((usize)count, RECT_SIZE, &rect_bytes);
    if (result != CORE_OK) {
        return result;
    }
    return core_size_add(base, rect_bytes, out_size);
}

ErrorCode gui_encode_buffer_import(const GuiBufferImportRequest *request,
                                   Bytes destination, usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < BUFFER_IMPORT_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result == CORE_OK &&
        (request->width > MAX_BUFFER_DIMENSION ||
         request->height > MAX_BUFFER_DIMENSION)) {
        result = CORE_INVALID_ARGUMENT;
    }
    if (result == CORE_OK) {
        result = validate_storage(request->width, request->height,
                                  request->stride, request->pixel_format,
                                  request->byte_size);
    }
    if (result != CORE_OK) {
        return result;
    }
    encode_prefix(destination.data, request->prefix);
    put_u32(destination.data + 10, request->width);
    put_u32(destination.data + 14, request->height);
    put_u32(destination.data + 18, request->stride);
    put_u32(destination.data + 22, request->pixel_format);
    put_u64(destination.data + 26, request->byte_size);
    *out_size = BUFFER_IMPORT_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_buffer_import(const IpcMessage *message,
                                   GuiBufferImportRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_SURFACE_BUFFER_IMPORT ||
        message->payload.data == NULL || message->payload.size != BUFFER_IMPORT_SIZE ||
        message->descriptor < 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->width = get_u32(message->payload.data + 10);
    out_request->height = get_u32(message->payload.data + 14);
    out_request->stride = get_u32(message->payload.data + 18);
    out_request->pixel_format = get_u32(message->payload.data + 22);
    out_request->byte_size = get_u64(message->payload.data + 26);
    if (result != CORE_OK) {
        return result;
    }
    if (out_request->width > MAX_BUFFER_DIMENSION ||
        out_request->height > MAX_BUFFER_DIMENSION) {
        return CORE_INVALID_ARGUMENT;
    }
    return validate_storage(out_request->width, out_request->height,
                            out_request->stride, out_request->pixel_format,
                            out_request->byte_size);
}

ErrorCode gui_encode_surface_damage(const GuiSurfaceDamageRequest *request,
                                    Bytes destination, usize *out_size)
{
    usize size;
    usize index;
    ErrorCode result;

    if (request == NULL || out_size == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result == CORE_OK && request->surface_id == 0) {
        result = CORE_INVALID_ARGUMENT;
    }
    if (result == CORE_OK) {
        result = validate_rects(request->flags, request->rect_count,
                                request->rects);
    }
    if (result == CORE_OK) {
        result = variable_size(SURFACE_DAMAGE_BASE_SIZE, request->rect_count,
                               &size);
    }
    if (result != CORE_OK) {
        return result;
    }
    if (destination.data == NULL || destination.size < size) {
        return CORE_INVALID_ARGUMENT;
    }
    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->surface_id);
    put_u32(destination.data + 18, request->flags);
    put_u16(destination.data + 22, request->rect_count);
    for (index = 0; index < request->rect_count; ++index) {
        encode_rect(destination.data + 24 + index * RECT_SIZE,
                    &request->rects[index]);
    }
    *out_size = size;
    return CORE_OK;
}

ErrorCode gui_decode_surface_damage(const IpcMessage *message,
                                    GuiSurfaceDamageRequest *out_request)
{
    usize expected;
    usize index;
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_SURFACE_DAMAGE || message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL || message->payload.size < SURFACE_DAMAGE_BASE_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_request->rect_count = get_u16(message->payload.data + 22);
    result = variable_size(SURFACE_DAMAGE_BASE_SIZE, out_request->rect_count,
                           &expected);
    if (result != CORE_OK || expected != message->payload.size) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->surface_id = get_u64(message->payload.data + 10);
    out_request->flags = get_u32(message->payload.data + 18);
    for (index = 0; index < out_request->rect_count; ++index) {
        decode_rect(message->payload.data + 24 + index * RECT_SIZE,
                    &out_request->rects[index]);
    }
    if (result != CORE_OK || out_request->surface_id == 0) {
        return result != CORE_OK ? result : CORE_INVALID_ARGUMENT;
    }
    return validate_rects(out_request->flags, out_request->rect_count,
                          out_request->rects);
}

ErrorCode gui_encode_surface_id_request(const GuiSurfaceIdRequest *request,
                                        Bytes destination, usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SURFACE_ID_REQUEST_SIZE || request->surface_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result != CORE_OK) {
        return result;
    }
    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->surface_id);
    *out_size = SURFACE_ID_REQUEST_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_surface_id_request(const IpcMessage *message,
                                        IpcMessageType expected_type,
                                        GuiSurfaceIdRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL || message->type != expected_type ||
        message->descriptor != IPC_NO_DESCRIPTOR || message->payload.data == NULL ||
        message->payload.size != SURFACE_ID_REQUEST_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->surface_id = get_u64(message->payload.data + 10);
    if (result != CORE_OK) {
        return result;
    }
    return out_request->surface_id != 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_buffer_id_request(const GuiBufferIdRequest *request,
                                       Bytes destination, usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < BUFFER_ID_REQUEST_SIZE || request->buffer_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result != CORE_OK) {
        return result;
    }
    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->buffer_id);
    *out_size = BUFFER_ID_REQUEST_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_buffer_id_request(const IpcMessage *message,
                                       IpcMessageType expected_type,
                                       GuiBufferIdRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL || message->type != expected_type ||
        message->descriptor != IPC_NO_DESCRIPTOR || message->payload.data == NULL ||
        message->payload.size != BUFFER_ID_REQUEST_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->buffer_id = get_u64(message->payload.data + 10);
    if (result != CORE_OK) {
        return result;
    }
    return out_request->buffer_id != 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_surface_attach(const GuiSurfaceAttachRequest *request,
                                    Bytes destination, usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SURFACE_ATTACH_SIZE || request->surface_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result != CORE_OK) {
        return result;
    }
    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->surface_id);
    put_u64(destination.data + 18, request->buffer_id);
    *out_size = SURFACE_ATTACH_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_surface_attach(const IpcMessage *message,
                                    GuiSurfaceAttachRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_SURFACE_ATTACH ||
        message->descriptor != IPC_NO_DESCRIPTOR || message->payload.data == NULL ||
        message->payload.size != SURFACE_ATTACH_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->surface_id = get_u64(message->payload.data + 10);
    out_request->buffer_id = get_u64(message->payload.data + 18);
    if (result != CORE_OK) {
        return result;
    }
    return out_request->surface_id != 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_surface_frame_request(
    const GuiSurfaceFrameRequest *request, Bytes destination,
    usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SURFACE_FRAME_REQUEST_SIZE || request->surface_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result != CORE_OK) {
        return result;
    }
    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->surface_id);
    *out_size = SURFACE_FRAME_REQUEST_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_surface_frame_request(
    const IpcMessage *message, GuiSurfaceFrameRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_SURFACE_REQUEST_FRAME ||
        message->descriptor != IPC_NO_DESCRIPTOR || message->payload.data == NULL ||
        message->payload.size != SURFACE_FRAME_REQUEST_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->surface_id = get_u64(message->payload.data + 10);
    if (result != CORE_OK) {
        return result;
    }
    return out_request->surface_id != 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_surface_commit(const GuiSurfaceCommitRequest *request,
                                    Bytes destination, usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SURFACE_COMMIT_SIZE || request->surface_id == 0 ||
        request->commit_serial == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result != CORE_OK) {
        return result;
    }
    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->surface_id);
    put_u64(destination.data + 18, request->commit_serial);
    *out_size = SURFACE_COMMIT_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_surface_commit(const IpcMessage *message,
                                    GuiSurfaceCommitRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_SURFACE_COMMIT ||
        message->descriptor != IPC_NO_DESCRIPTOR || message->payload.data == NULL ||
        message->payload.size != SURFACE_COMMIT_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->surface_id = get_u64(message->payload.data + 10);
    out_request->commit_serial = get_u64(message->payload.data + 18);
    if (result != CORE_OK) {
        return result;
    }
    if (out_request->surface_id == 0 || out_request->commit_serial == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

ErrorCode gui_encode_surface_region(const GuiSurfaceRegionRequest *request,
                                    Bytes destination, usize *out_size)
{
    usize size;
    usize index;
    ErrorCode result;

    if (request == NULL || out_size == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result == CORE_OK && request->surface_id == 0) {
        result = CORE_INVALID_ARGUMENT;
    }
    if (result == CORE_OK) {
        result = validate_region_rects(request->rect_count, request->rects);
    }
    if (result == CORE_OK) {
        result = variable_size(SURFACE_REGION_BASE_SIZE, request->rect_count,
                               &size);
    }
    if (result != CORE_OK) {
        return result;
    }
    if (destination.data == NULL || destination.size < size) {
        return CORE_INVALID_ARGUMENT;
    }
    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->surface_id);
    put_u16(destination.data + 18, request->rect_count);
    for (index = 0; index < request->rect_count; ++index) {
        encode_rect(destination.data + 20 + index * RECT_SIZE,
                    &request->rects[index]);
    }
    *out_size = size;
    return CORE_OK;
}

ErrorCode gui_decode_surface_region(const IpcMessage *message,
                                    GuiSurfaceRegionRequest *out_request)
{
    usize expected;
    usize index;
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_SURFACE_SET_OPAQUE ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size < SURFACE_REGION_BASE_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_request->rect_count = get_u16(message->payload.data + 18);
    result = variable_size(SURFACE_REGION_BASE_SIZE, out_request->rect_count,
                           &expected);
    if (result != CORE_OK || expected != message->payload.size) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->surface_id = get_u64(message->payload.data + 10);
    for (index = 0; index < out_request->rect_count; ++index) {
        decode_rect(message->payload.data + 20 + index * RECT_SIZE,
                    &out_request->rects[index]);
    }
    if (result != CORE_OK || out_request->surface_id == 0) {
        return result != CORE_OK ? result : CORE_INVALID_ARGUMENT;
    }
    return validate_region_rects(out_request->rect_count, out_request->rects);
}

ErrorCode gui_encode_surface_create_result(
    const GuiSurfaceCreateResult *result, Bytes destination,
    usize *out_size)
{
    usize basic_size;
    usize index;
    ErrorCode code;

    if (result == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SURFACE_CREATE_RESULT_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    if (result->result.error == CORE_OK) {
        if (result->surface_id == 0) {
            return CORE_INVALID_ARGUMENT;
        }
    } else {
        if (result->surface_id != 0) {
            return CORE_INVALID_ARGUMENT;
        }
        for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
            if (result->surface_bind_token[index] != 0) {
                return CORE_INVALID_ARGUMENT;
            }
        }
    }
    code = gui_encode_basic_result(&result->result, destination, &basic_size);
    if (code != CORE_OK) {
        return code;
    }
    put_u64(destination.data + basic_size, result->surface_id);
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        destination.data[basic_size + 8 + index] = result->surface_bind_token[index];
    }
    *out_size = SURFACE_CREATE_RESULT_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_surface_create_result(
    const IpcMessage *message, GuiSurfaceCreateResult *out_result)
{
    usize index;

    if (message == NULL || out_result == NULL ||
        message->type != GUI_SURFACE_CREATE_RESULT ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != SURFACE_CREATE_RESULT_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_result->result.error = (ErrorCode)get_u32(message->payload.data);
    out_result->result.request_id = get_u64(message->payload.data + 4);
    out_result->surface_id = get_u64(message->payload.data + 12);
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        out_result->surface_bind_token[index] = message->payload.data[20 + index];
    }
    if (!error_is_known(out_result->result.error) ||
        out_result->result.request_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    if (out_result->result.error == CORE_OK) {
        return out_result->surface_id != 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
    }
    if (out_result->surface_id != 0) {
        return CORE_INVALID_ARGUMENT;
    }
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        if (out_result->surface_bind_token[index] != 0) {
            return CORE_INVALID_ARGUMENT;
        }
    }
    return CORE_OK;
}

ErrorCode gui_encode_buffer_import_result(
    const GuiBufferImportResult *result, Bytes destination,
    usize *out_size)
{
    usize basic_size;
    ErrorCode code;

    if (result == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < BUFFER_IMPORT_RESULT_SIZE ||
        ((result->result.error == CORE_OK) != (result->buffer_id != 0))) {
        return CORE_INVALID_ARGUMENT;
    }
    code = gui_encode_basic_result(&result->result, destination, &basic_size);
    if (code != CORE_OK) {
        return code;
    }
    put_u64(destination.data + basic_size, result->buffer_id);
    *out_size = BUFFER_IMPORT_RESULT_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_buffer_import_result(
    const IpcMessage *message, GuiBufferImportResult *out_result)
{
    if (message == NULL || out_result == NULL ||
        message->type != GUI_SURFACE_BUFFER_IMPORT_RESULT ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != BUFFER_IMPORT_RESULT_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_result->result.error = (ErrorCode)get_u32(message->payload.data);
    out_result->result.request_id = get_u64(message->payload.data + 4);
    out_result->buffer_id = get_u64(message->payload.data + 12);
    if (!error_is_known(out_result->result.error) ||
        out_result->result.request_id == 0 ||
        ((out_result->result.error == CORE_OK) != (out_result->buffer_id != 0))) {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

ErrorCode gui_encode_surface_frame_done(
    const GuiSurfaceFrameDoneEvent *event, Bytes destination,
    usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SURFACE_FRAME_DONE_SIZE || event->request_id == 0 ||
        event->surface_id == 0 || event->commit_serial == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->request_id);
    put_u64(destination.data + 8, event->surface_id);
    put_u64(destination.data + 16, event->commit_serial);
    put_u64(destination.data + 24, event->presented_at_ms);
    *out_size = SURFACE_FRAME_DONE_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_surface_frame_done(
    const IpcMessage *message, GuiSurfaceFrameDoneEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != GUI_SURFACE_FRAME_DONE ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != SURFACE_FRAME_DONE_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->request_id = get_u64(message->payload.data);
    out_event->surface_id = get_u64(message->payload.data + 8);
    out_event->commit_serial = get_u64(message->payload.data + 16);
    out_event->presented_at_ms = get_u64(message->payload.data + 24);
    return out_event->request_id != 0 && out_event->surface_id != 0 &&
           out_event->commit_serial != 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_buffer_released(
    const GuiBufferReleasedEvent *event, Bytes destination,
    usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < BUFFER_RELEASED_SIZE || event->serial == 0 ||
        event->buffer_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->serial);
    put_u64(destination.data + 8, event->buffer_id);
    *out_size = BUFFER_RELEASED_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_buffer_released(
    const IpcMessage *message, GuiBufferReleasedEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != GUI_SURFACE_BUFFER_RELEASED ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != BUFFER_RELEASED_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->serial = get_u64(message->payload.data);
    out_event->buffer_id = get_u64(message->payload.data + 8);
    return out_event->serial != 0 && out_event->buffer_id != 0 ? CORE_OK :
           CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_surface_output_event(
    const GuiSurfaceOutputEvent *event, Bytes destination,
    usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SURFACE_OUTPUT_EVENT_SIZE || event->serial == 0 ||
        event->surface_id == 0 || event->display_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->serial);
    put_u64(destination.data + 8, event->surface_id);
    put_u64(destination.data + 16, event->display_id);
    *out_size = SURFACE_OUTPUT_EVENT_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_surface_output_event(
    const IpcMessage *message, IpcMessageType expected_type,
    GuiSurfaceOutputEvent *out_event)
{
    if (message == NULL || out_event == NULL || message->type != expected_type ||
        (expected_type != GUI_SURFACE_OUTPUT_ENTER &&
         expected_type != GUI_SURFACE_OUTPUT_LEAVE) ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != SURFACE_OUTPUT_EVENT_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->serial = get_u64(message->payload.data);
    out_event->surface_id = get_u64(message->payload.data + 8);
    out_event->display_id = get_u64(message->payload.data + 16);
    return out_event->serial != 0 && out_event->surface_id != 0 &&
           out_event->display_id != 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_surface_redraw(
    const GuiSurfaceRedrawEvent *event, Bytes destination,
    usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SURFACE_REDRAW_SIZE || event->serial == 0 ||
        event->surface_id == 0 || event->reason < 1 || event->reason > 3) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->serial);
    put_u64(destination.data + 8, event->surface_id);
    put_u32(destination.data + 16, event->reason);
    *out_size = SURFACE_REDRAW_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_surface_redraw(
    const IpcMessage *message, GuiSurfaceRedrawEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != GUI_SURFACE_FULL_REDRAW ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != SURFACE_REDRAW_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->serial = get_u64(message->payload.data);
    out_event->surface_id = get_u64(message->payload.data + 8);
    out_event->reason = get_u32(message->payload.data + 16);
    return out_event->serial != 0 && out_event->surface_id != 0 &&
           out_event->reason >= 1 && out_event->reason <= 3 ? CORE_OK :
           CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_display_present(const GuiDisplayPresentRequest *request,
                                     Bytes destination, usize *out_size)
{
    usize size;
    usize index;
    ErrorCode result;

    if (request == NULL || out_size == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result == CORE_OK && (request->display_id == 0 || request->generation == 0)) {
        result = CORE_INVALID_ARGUMENT;
    }
    if (result == CORE_OK) {
        result = validate_storage(request->width, request->height,
                                  request->stride, request->pixel_format,
                                  request->byte_size);
    }
    if (result == CORE_OK) {
        result = validate_rects(request->flags, request->damage_count,
                                request->damage);
    }
    if (result == CORE_OK) {
        result = variable_size(DISPLAY_PRESENT_BASE_SIZE,
                               request->damage_count, &size);
    }
    if (result != CORE_OK) {
        return result;
    }
    if (destination.data == NULL || destination.size < size) {
        return CORE_INVALID_ARGUMENT;
    }
    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->display_id);
    put_u64(destination.data + 18, request->generation);
    put_u32(destination.data + 26, request->width);
    put_u32(destination.data + 30, request->height);
    put_u32(destination.data + 34, request->stride);
    put_u32(destination.data + 38, request->pixel_format);
    put_u64(destination.data + 42, request->byte_size);
    put_u32(destination.data + 50, request->flags);
    put_u16(destination.data + 54, request->damage_count);
    for (index = 0; index < request->damage_count; ++index) {
        encode_rect(destination.data + 56 + index * RECT_SIZE,
                    &request->damage[index]);
    }
    *out_size = size;
    return CORE_OK;
}

ErrorCode gui_decode_display_present(const IpcMessage *message,
                                     GuiDisplayPresentRequest *out_request)
{
    usize expected;
    usize index;
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_DISPLAY_PRESENT || message->descriptor < 0 ||
        message->payload.data == NULL || message->payload.size < DISPLAY_PRESENT_BASE_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_request->damage_count = get_u16(message->payload.data + 54);
    result = variable_size(DISPLAY_PRESENT_BASE_SIZE, out_request->damage_count,
                           &expected);
    if (result != CORE_OK || expected != message->payload.size) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->display_id = get_u64(message->payload.data + 10);
    out_request->generation = get_u64(message->payload.data + 18);
    out_request->width = get_u32(message->payload.data + 26);
    out_request->height = get_u32(message->payload.data + 30);
    out_request->stride = get_u32(message->payload.data + 34);
    out_request->pixel_format = get_u32(message->payload.data + 38);
    out_request->byte_size = get_u64(message->payload.data + 42);
    out_request->flags = get_u32(message->payload.data + 50);
    for (index = 0; index < out_request->damage_count; ++index) {
        decode_rect(message->payload.data + 56 + index * RECT_SIZE,
                    &out_request->damage[index]);
    }
    if (result != CORE_OK || out_request->display_id == 0 ||
        out_request->generation == 0) {
        return result != CORE_OK ? result : CORE_INVALID_ARGUMENT;
    }
    result = validate_storage(out_request->width, out_request->height,
                              out_request->stride, out_request->pixel_format,
                              out_request->byte_size);
    if (result != CORE_OK) {
        return result;
    }
    return validate_rects(out_request->flags, out_request->damage_count,
                          out_request->damage);
}

ErrorCode gui_encode_presenter_bind(const GuiPresenterBindRequest *request,
                                    Bytes destination, usize *out_size)
{
    usize index;
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < PRESENTER_BIND_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result != CORE_OK) {
        return result;
    }
    encode_prefix(destination.data, request->prefix);
    for (index = 0; index < GUI_PRESENTER_TOKEN_SIZE; ++index) {
        destination.data[10 + index] = request->presenter_token[index];
    }
    *out_size = PRESENTER_BIND_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_presenter_bind(const IpcMessage *message,
                                    GuiPresenterBindRequest *out_request)
{
    usize index;
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_DISPLAY_BIND_PRESENTER ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != PRESENTER_BIND_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    if (result != CORE_OK) {
        return result;
    }
    for (index = 0; index < GUI_PRESENTER_TOKEN_SIZE; ++index) {
        out_request->presenter_token[index] = message->payload.data[10 + index];
    }
    return CORE_OK;
}

ErrorCode gui_encode_display_configure(
    const GuiDisplayConfigureRequest *request, Bytes destination,
    usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < DISPLAY_CONFIGURE_SIZE || request->display_id == 0 ||
        request->generation == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result == CORE_OK) {
        result = validate_mode(request->enabled, request->orientation,
                               request->mode);
    }
    if (result != CORE_OK) {
        return result;
    }
    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->display_id);
    put_u64(destination.data + 18, request->generation);
    destination.data[26] = request->enabled ? 1 : 0;
    destination.data[27] = request->orientation;
    put_u32(destination.data + 28, request->mode.width_px);
    put_u32(destination.data + 32, request->mode.height_px);
    put_u32(destination.data + 36, request->mode.refresh_millihz);
    *out_size = DISPLAY_CONFIGURE_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_display_configure(
    const IpcMessage *message, GuiDisplayConfigureRequest *out_request)
{
    ErrorCode result;
    u8 enabled;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_DISPLAY_CONFIGURE ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != DISPLAY_CONFIGURE_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->display_id = get_u64(message->payload.data + 10);
    out_request->generation = get_u64(message->payload.data + 18);
    enabled = message->payload.data[26];
    out_request->enabled = enabled == 1;
    out_request->orientation = message->payload.data[27];
    out_request->mode.width_px = get_u32(message->payload.data + 28);
    out_request->mode.height_px = get_u32(message->payload.data + 32);
    out_request->mode.refresh_millihz = get_u32(message->payload.data + 36);
    if (result != CORE_OK) {
        return result;
    }
    if (out_request->display_id == 0 || out_request->generation == 0 ||
        enabled > 1) {
        return CORE_INVALID_ARGUMENT;
    }
    return validate_mode(out_request->enabled, out_request->orientation,
                         out_request->mode);
}

ErrorCode gui_encode_basic_result(const GuiBasicResult *result,
                                  Bytes destination, usize *out_size)
{
    if (result == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < BASIC_RESULT_SIZE || result->request_id == 0 ||
        !error_is_known(result->error)) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u32(destination.data, (u32)result->error);
    put_u64(destination.data + 4, result->request_id);
    *out_size = BASIC_RESULT_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_basic_result(const IpcMessage *message,
                                  IpcMessageType expected_type,
                                  GuiBasicResult *out_result)
{
    if (message == NULL || out_result == NULL || message->type != expected_type ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL || message->payload.size != BASIC_RESULT_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_result->error = (ErrorCode)get_u32(message->payload.data);
    out_result->request_id = get_u64(message->payload.data + 4);
    if (!error_is_known(out_result->error) || out_result->request_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

ErrorCode gui_encode_display_presented(
    const GuiDisplayPresentedEvent *event, Bytes destination,
    usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < DISPLAY_PRESENTED_SIZE || event->request_id == 0 ||
        event->display_id == 0 || event->generation == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->request_id);
    put_u64(destination.data + 8, event->display_id);
    put_u64(destination.data + 16, event->generation);
    put_u64(destination.data + 24, event->presented_at_ms);
    *out_size = DISPLAY_PRESENTED_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_display_presented(
    const IpcMessage *message, GuiDisplayPresentedEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != GUI_DISPLAY_PRESENTED ||
        message->descriptor != IPC_NO_DESCRIPTOR || message->payload.data == NULL ||
        message->payload.size != DISPLAY_PRESENTED_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->request_id = get_u64(message->payload.data);
    out_event->display_id = get_u64(message->payload.data + 8);
    out_event->generation = get_u64(message->payload.data + 16);
    out_event->presented_at_ms = get_u64(message->payload.data + 24);
    return out_event->request_id != 0 && out_event->display_id != 0 &&
           out_event->generation != 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_display_present_failed(
    const GuiDisplayPresentFailedEvent *event, Bytes destination,
    usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < DISPLAY_PRESENT_FAILED_SIZE || event->request_id == 0 ||
        event->display_id == 0 || event->generation == 0 ||
        event->error == CORE_OK || !error_is_known(event->error)) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->request_id);
    put_u64(destination.data + 8, event->display_id);
    put_u64(destination.data + 16, event->generation);
    put_u32(destination.data + 24, (u32)event->error);
    *out_size = DISPLAY_PRESENT_FAILED_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_display_present_failed(
    const IpcMessage *message, GuiDisplayPresentFailedEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != GUI_DISPLAY_PRESENT_FAILED ||
        message->descriptor != IPC_NO_DESCRIPTOR || message->payload.data == NULL ||
        message->payload.size != DISPLAY_PRESENT_FAILED_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->request_id = get_u64(message->payload.data);
    out_event->display_id = get_u64(message->payload.data + 8);
    out_event->generation = get_u64(message->payload.data + 16);
    out_event->error = (ErrorCode)get_u32(message->payload.data + 24);
    if (out_event->request_id == 0 || out_event->display_id == 0 ||
        out_event->generation == 0 || out_event->error == CORE_OK ||
        !error_is_known(out_event->error)) {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

ErrorCode gui_encode_display_backend_lost(
    const GuiDisplayBackendLostEvent *event, Bytes destination,
    usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < DISPLAY_BACKEND_LOST_SIZE || event->serial == 0 ||
        event->error == CORE_OK || !error_is_known(event->error)) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->serial);
    put_u32(destination.data + 8, (u32)event->error);
    *out_size = DISPLAY_BACKEND_LOST_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_display_backend_lost(
    const IpcMessage *message, GuiDisplayBackendLostEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != GUI_DISPLAY_BACKEND_LOST ||
        message->descriptor != IPC_NO_DESCRIPTOR || message->payload.data == NULL ||
        message->payload.size != DISPLAY_BACKEND_LOST_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->serial = get_u64(message->payload.data);
    out_event->error = (ErrorCode)get_u32(message->payload.data + 8);
    if (out_event->serial == 0 || out_event->error == CORE_OK ||
        !error_is_known(out_event->error)) {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

ErrorCode gui_encode_versioned_request(GuiRequestPrefix prefix,
                                       Bytes destination, usize *out_size)
{
    ErrorCode result;
    if (out_size == NULL || destination.data == NULL ||
        destination.size < VERSIONED_REQUEST_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(prefix);
    if (result != CORE_OK) {
        return result;
    }
    encode_prefix(destination.data, prefix);
    *out_size = VERSIONED_REQUEST_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_versioned_request(const IpcMessage *message,
                                       IpcMessageType expected_type,
                                       GuiRequestPrefix *out_prefix)
{
    if (message == NULL || out_prefix == NULL || message->type != expected_type ||
        message->descriptor != IPC_NO_DESCRIPTOR || message->payload.data == NULL ||
        message->payload.size != VERSIONED_REQUEST_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    return decode_prefix(message->payload.data, out_prefix);
}

ErrorCode gui_encode_display_id_request(GuiRequestPrefix prefix,
                                        u64 display_id, Bytes destination,
                                        usize *out_size)
{
    ErrorCode result;
    if (out_size == NULL || destination.data == NULL ||
        destination.size < DISPLAY_ID_REQUEST_SIZE || display_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(prefix);
    if (result != CORE_OK) {
        return result;
    }
    encode_prefix(destination.data, prefix);
    put_u64(destination.data + 10, display_id);
    *out_size = DISPLAY_ID_REQUEST_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_display_id_request(const IpcMessage *message,
                                        IpcMessageType expected_type,
                                        GuiRequestPrefix *out_prefix,
                                        u64 *out_display_id)
{
    ErrorCode result;
    if (message == NULL || out_prefix == NULL || out_display_id == NULL ||
        message->type != expected_type || message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != DISPLAY_ID_REQUEST_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, out_prefix);
    *out_display_id = get_u64(message->payload.data + 10);
    if (result != CORE_OK) {
        return result;
    }
    return *out_display_id == 0 ? CORE_INVALID_ARGUMENT : CORE_OK;
}

ErrorCode gui_encode_display_info(const GuiDisplayInfo *info,
                                  Bytes destination, usize *out_size)
{
    usize mode_bytes;
    usize size;
    usize index;
    ErrorCode result;

    if (info == NULL || out_size == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_display_info(info);
    if (result != CORE_OK) {
        return result;
    }
    result = core_size_multiply(info->mode_count, sizeof(GuiDisplayMode),
                                &mode_bytes);
    if (result == CORE_OK) {
        result = core_size_add(DISPLAY_INFO_FIXED_SIZE + info->name_length,
                               mode_bytes, &size);
    }
    if (result != CORE_OK) {
        return result;
    }
    if (destination.data == NULL || destination.size < size) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, info->display_id);
    put_u64(destination.data + 8, info->generation);
    put_u32(destination.data + 16, info->flags);
    put_u32(destination.data + 20, info->physical_width_mm);
    put_u32(destination.data + 24, info->physical_height_mm);
    put_u32(destination.data + 28, info->current_mode.width_px);
    put_u32(destination.data + 32, info->current_mode.height_px);
    put_u32(destination.data + 36, info->current_mode.refresh_millihz);
    put_u16(destination.data + 40, info->name_length);
    put_u16(destination.data + 42, info->mode_count);
    for (index = 0; index < info->name_length; ++index) {
        destination.data[44 + index] = info->name[index];
    }
    for (index = 0; index < info->mode_count; ++index) {
        u8 *mode = destination.data + 44 + info->name_length + index * 12;
        put_u32(mode, info->modes[index].width_px);
        put_u32(mode + 4, info->modes[index].height_px);
        put_u32(mode + 8, info->modes[index].refresh_millihz);
    }
    *out_size = size;
    return CORE_OK;
}

ErrorCode gui_decode_display_info(Bytes source, GuiDisplayInfo *out_info,
                                  usize *out_consumed)
{
    usize mode_bytes;
    usize size;
    usize index;
    ErrorCode result;

    if (out_info == NULL || out_consumed == NULL || source.data == NULL ||
        source.size < DISPLAY_INFO_FIXED_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_info->name_length = get_u16(source.data + 40);
    out_info->mode_count = get_u16(source.data + 42);
    if (out_info->name_length == 0 ||
        out_info->name_length > GUI_DISPLAY_NAME_MAX ||
        out_info->mode_count > GUI_DISPLAY_MODE_MAX) {
        return CORE_INVALID_ARGUMENT;
    }
    result = core_size_multiply(out_info->mode_count, sizeof(GuiDisplayMode),
                                &mode_bytes);
    if (result == CORE_OK) {
        result = core_size_add(DISPLAY_INFO_FIXED_SIZE + out_info->name_length,
                               mode_bytes, &size);
    }
    if (result != CORE_OK || size > source.size) {
        return CORE_INVALID_ARGUMENT;
    }
    out_info->display_id = get_u64(source.data);
    out_info->generation = get_u64(source.data + 8);
    out_info->flags = get_u32(source.data + 16);
    out_info->physical_width_mm = get_u32(source.data + 20);
    out_info->physical_height_mm = get_u32(source.data + 24);
    out_info->current_mode.width_px = get_u32(source.data + 28);
    out_info->current_mode.height_px = get_u32(source.data + 32);
    out_info->current_mode.refresh_millihz = get_u32(source.data + 36);
    for (index = 0; index < out_info->name_length; ++index) {
        out_info->name[index] = source.data[44 + index];
    }
    for (index = 0; index < out_info->mode_count; ++index) {
        const u8 *mode = source.data + 44 + out_info->name_length + index * 12;
        out_info->modes[index].width_px = get_u32(mode);
        out_info->modes[index].height_px = get_u32(mode + 4);
        out_info->modes[index].refresh_millihz = get_u32(mode + 8);
    }
    result = validate_display_info(out_info);
    if (result != CORE_OK) {
        return result;
    }
    *out_consumed = size;
    return CORE_OK;
}

ErrorCode gui_decode_display_list_header(const IpcMessage *message,
                                         GuiDisplayListHeader *out_header,
                                         usize *out_first_display)
{
    usize offset = DISPLAY_LIST_FIXED_SIZE;
    usize index;

    if (message == NULL || out_header == NULL || out_first_display == NULL ||
        message->type != GUI_DISPLAY_LIST_RESULT ||
        message->descriptor != IPC_NO_DESCRIPTOR || message->payload.data == NULL ||
        message->payload.size < DISPLAY_LIST_FIXED_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_header->result.error = (ErrorCode)get_u32(message->payload.data);
    out_header->result.request_id = get_u64(message->payload.data + 4);
    out_header->snapshot_serial = get_u64(message->payload.data + 12);
    out_header->display_count = get_u16(message->payload.data + 20);
    if (!error_is_known(out_header->result.error) ||
        out_header->result.request_id == 0 ||
        out_header->display_count > GUI_DISPLAY_LIST_MAX ||
        (out_header->result.error != CORE_OK &&
         (out_header->snapshot_serial != 0 || out_header->display_count != 0))) {
        return CORE_INVALID_ARGUMENT;
    }
    for (index = 0; index < out_header->display_count; ++index) {
        GuiDisplayInfo scratch;
        usize consumed;
        Bytes rest = { message->payload.data + offset,
                       message->payload.size - offset };
        ErrorCode result = gui_decode_display_info(rest, &scratch, &consumed);
        if (result != CORE_OK) {
            return result;
        }
        offset += consumed;
    }
    if (offset != message->payload.size) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_first_display = DISPLAY_LIST_FIXED_SIZE;
    return CORE_OK;
}

ErrorCode gui_encode_display_list_header(const GuiDisplayListHeader *header,
                                         Bytes destination, usize *out_size)
{
    usize basic_size;
    ErrorCode result;

    if (header == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < DISPLAY_LIST_FIXED_SIZE ||
        header->display_count > GUI_DISPLAY_LIST_MAX ||
        (header->result.error != CORE_OK &&
         (header->snapshot_serial != 0 || header->display_count != 0))) {
        return CORE_INVALID_ARGUMENT;
    }
    result = gui_encode_basic_result(&header->result, destination, &basic_size);
    if (result != CORE_OK) {
        return result;
    }
    put_u64(destination.data + basic_size, header->snapshot_serial);
    put_u16(destination.data + basic_size + 8, header->display_count);
    *out_size = DISPLAY_LIST_FIXED_SIZE;
    return CORE_OK;
}

ErrorCode gui_encode_display_info_result(const GuiBasicResult *result,
                                         const GuiDisplayInfo *info,
                                         Bytes destination, usize *out_size)
{
    usize basic_size;
    usize info_size;
    ErrorCode code;

    if (result == NULL || out_size == NULL || destination.data == NULL ||
        (result->error == CORE_OK) != (info != NULL)) {
        return CORE_INVALID_ARGUMENT;
    }
    code = gui_encode_basic_result(result, destination, &basic_size);
    if (code != CORE_OK) {
        return code;
    }
    if (info == NULL) {
        *out_size = basic_size;
        return CORE_OK;
    }
    code = gui_encode_display_info(info,
        (Bytes){ destination.data + basic_size, destination.size - basic_size },
        &info_size);
    if (code != CORE_OK) {
        return code;
    }
    *out_size = basic_size + info_size;
    return CORE_OK;
}

ErrorCode gui_decode_display_info_result(const IpcMessage *message,
                                         IpcMessageType expected_type,
                                         GuiBasicResult *out_result,
                                         GuiDisplayInfo *out_info)
{
    usize consumed;
    ErrorCode result;

    if (message == NULL || out_result == NULL || out_info == NULL ||
        message->type != expected_type || message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL || message->payload.size < BASIC_RESULT_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_result->error = (ErrorCode)get_u32(message->payload.data);
    out_result->request_id = get_u64(message->payload.data + 4);
    if (!error_is_known(out_result->error) || out_result->request_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    if (out_result->error != CORE_OK) {
        return message->payload.size == BASIC_RESULT_SIZE ? CORE_OK :
               CORE_INVALID_ARGUMENT;
    }
    result = gui_decode_display_info(
        (Bytes){ message->payload.data + BASIC_RESULT_SIZE,
                 message->payload.size - BASIC_RESULT_SIZE },
        out_info, &consumed);
    if (result != CORE_OK) {
        return result;
    }
    return consumed + BASIC_RESULT_SIZE == message->payload.size ? CORE_OK :
           CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_display_info_event(u64 serial,
                                        const GuiDisplayInfo *info,
                                        Bytes destination, usize *out_size)
{
    usize info_size;
    ErrorCode result;
    if (serial == 0 || info == NULL || out_size == NULL ||
        destination.data == NULL || destination.size < 8) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, serial);
    result = gui_encode_display_info(info,
        (Bytes){ destination.data + 8, destination.size - 8 }, &info_size);
    if (result != CORE_OK) {
        return result;
    }
    *out_size = 8 + info_size;
    return CORE_OK;
}

ErrorCode gui_decode_display_info_event(const IpcMessage *message,
                                        IpcMessageType expected_type,
                                        u64 *out_serial,
                                        GuiDisplayInfo *out_info)
{
    usize consumed;
    ErrorCode result;
    Bytes info;

    if (message == NULL || out_serial == NULL || out_info == NULL ||
        message->type != expected_type ||
        (expected_type != GUI_DISPLAY_ADDED &&
         expected_type != GUI_DISPLAY_CHANGED) ||
        message->descriptor != IPC_NO_DESCRIPTOR || message->payload.data == NULL ||
        message->payload.size < 8 + DISPLAY_INFO_FIXED_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_serial = get_u64(message->payload.data);
    info.data = message->payload.data + 8;
    info.size = message->payload.size - 8;
    result = gui_decode_display_info(info, out_info, &consumed);
    if (result != CORE_OK) {
        return result;
    }
    return *out_serial != 0 && consumed == info.size ? CORE_OK :
           CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_display_removed(const GuiDisplayRemovedEvent *event,
                                     Bytes destination, usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < DISPLAY_REMOVED_SIZE || event->serial == 0 ||
        event->display_id == 0 || event->last_generation == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->serial);
    put_u64(destination.data + 8, event->display_id);
    put_u64(destination.data + 16, event->last_generation);
    *out_size = DISPLAY_REMOVED_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_display_removed(const IpcMessage *message,
                                     GuiDisplayRemovedEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != GUI_DISPLAY_REMOVED ||
        message->descriptor != IPC_NO_DESCRIPTOR || message->payload.data == NULL ||
        message->payload.size != DISPLAY_REMOVED_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->serial = get_u64(message->payload.data);
    out_event->display_id = get_u64(message->payload.data + 8);
    out_event->last_generation = get_u64(message->payload.data + 16);
    return out_event->serial != 0 && out_event->display_id != 0 &&
           out_event->last_generation != 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
}

static bool input_device_classes_valid(GuiInputDeviceClass classes)
{
    return classes != 0 && (classes & ~INPUT_DEVICE_CLASSES_KNOWN) == 0;
}

static bool io_input_kind_valid(GuiIoInputEventKind kind)
{
    return kind >= GUI_IO_INPUT_KEY && kind <= GUI_IO_INPUT_RAW;
}

static ErrorCode io_frame_size(u16 count, usize *out_size)
{
    usize event_bytes;
    ErrorCode result = core_size_multiply((usize)count, IO_INPUT_EVENT_SIZE,
                                          &event_bytes);
    if (result != CORE_OK) return result;
    return core_size_add(IO_INPUT_FRAME_BASE_SIZE, event_bytes, out_size);
}

ErrorCode gui_encode_io_subscribe(
    const GuiIoSubscribeRequest *request, Bytes destination, usize *out_size)
{
    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < IO_SUBSCRIBE_SIZE ||
        request->protocol_version != GUI_PROTOCOL_VERSION ||
        !input_device_classes_valid(request->device_class_mask)) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u16(destination.data, request->protocol_version);
    put_u32(destination.data + 2, request->device_class_mask);
    *out_size = IO_SUBSCRIBE_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_io_subscribe(
    const IpcMessage *message, GuiIoSubscribeRequest *out_request)
{
    if (message == NULL || out_request == NULL ||
        message->type != GUI_IO_SUBSCRIBE ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != IO_SUBSCRIBE_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_request->protocol_version = get_u16(message->payload.data);
    out_request->device_class_mask = get_u32(message->payload.data + 2);
    if (out_request->protocol_version != GUI_PROTOCOL_VERSION) {
        return CORE_UNSUPPORTED;
    }
    return input_device_classes_valid(out_request->device_class_mask) ?
           CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_io_result(const GuiIoResult *result,
                               Bytes destination, usize *out_size)
{
    if (result == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < IO_RESULT_SIZE || !error_is_known(result->error)) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u32(destination.data, (u32)result->error);
    *out_size = IO_RESULT_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_io_result(const IpcMessage *message,
                               IpcMessageType expected_type,
                               GuiIoResult *out_result)
{
    if (message == NULL || out_result == NULL ||
        message->type != expected_type ||
        (expected_type != GUI_IO_SUBSCRIBE_RESULT &&
         expected_type != GUI_IO_UNSUBSCRIBE_RESULT) ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != IO_RESULT_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_result->error = (ErrorCode)get_u32(message->payload.data);
    return error_is_known(out_result->error) ? CORE_OK :
           CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_io_device_event(
    const GuiIoDeviceEvent *event, Bytes destination, usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < IO_DEVICE_EVENT_SIZE || event->device_id == 0 ||
        !input_device_classes_valid(event->classes)) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->device_id);
    put_u16(destination.data + 8, event->bus);
    put_u16(destination.data + 10, event->vendor);
    put_u16(destination.data + 12, event->product);
    put_u16(destination.data + 14, event->version);
    put_u32(destination.data + 16, event->classes);
    *out_size = IO_DEVICE_EVENT_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_io_device_event(
    const IpcMessage *message, IpcMessageType expected_type,
    GuiIoDeviceEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != expected_type ||
        (expected_type != GUI_IO_DEVICE_ADDED &&
         expected_type != GUI_IO_DEVICE_REMOVED) ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != IO_DEVICE_EVENT_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->device_id = get_u64(message->payload.data);
    out_event->bus = get_u16(message->payload.data + 8);
    out_event->vendor = get_u16(message->payload.data + 10);
    out_event->product = get_u16(message->payload.data + 12);
    out_event->version = get_u16(message->payload.data + 14);
    out_event->classes = get_u32(message->payload.data + 16);
    return out_event->device_id != 0 &&
           input_device_classes_valid(out_event->classes) ?
           CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_io_input_frame(
    const GuiIoInputFrameEvent *event, Bytes destination, usize *out_size)
{
    usize size;
    usize index;
    ErrorCode result;

    if (event == NULL || out_size == NULL || event->event_count == 0 ||
        event->event_count > GUI_IO_INPUT_FRAME_MAX_EVENTS ||
        event->device_id == 0 || event->sequence == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = io_frame_size(event->event_count, &size);
    if (result != CORE_OK) return result;
    if (destination.data == NULL || destination.size < size) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->device_id);
    put_u64(destination.data + 8, event->sequence);
    put_u16(destination.data + 16, event->event_count);
    for (index = 0; index < event->event_count; ++index) {
        usize offset = IO_INPUT_FRAME_BASE_SIZE + index * IO_INPUT_EVENT_SIZE;
        if (!io_input_kind_valid(event->events[index].kind)) {
            return CORE_INVALID_ARGUMENT;
        }
        destination.data[offset] = event->events[index].kind;
        put_u16(destination.data + offset + 1, event->events[index].code);
        put_u32(destination.data + offset + 3, (u32)event->events[index].value);
    }
    *out_size = size;
    return CORE_OK;
}

ErrorCode gui_decode_io_input_frame(
    const IpcMessage *message, GuiIoInputFrameEvent *out_event)
{
    usize expected;
    usize index;
    ErrorCode result;

    if (message == NULL || out_event == NULL ||
        message->type != GUI_IO_INPUT_FRAME ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size < IO_INPUT_FRAME_BASE_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->device_id = get_u64(message->payload.data);
    out_event->sequence = get_u64(message->payload.data + 8);
    out_event->event_count = get_u16(message->payload.data + 16);
    if (out_event->event_count == 0 ||
        out_event->event_count > GUI_IO_INPUT_FRAME_MAX_EVENTS ||
        out_event->device_id == 0 || out_event->sequence == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = io_frame_size(out_event->event_count, &expected);
    if (result != CORE_OK || expected != message->payload.size) {
        return CORE_INVALID_ARGUMENT;
    }
    for (index = 0; index < out_event->event_count; ++index) {
        usize offset = IO_INPUT_FRAME_BASE_SIZE + index * IO_INPUT_EVENT_SIZE;
        out_event->events[index].kind = message->payload.data[offset];
        out_event->events[index].code = get_u16(message->payload.data + offset + 1);
        out_event->events[index].value =
            (i32)get_u32(message->payload.data + offset + 3);
        if (!io_input_kind_valid(out_event->events[index].kind)) {
            return CORE_INVALID_ARGUMENT;
        }
    }
    return CORE_OK;
}

static bool scene_geometry_valid(const GuiSceneGeometryRequest *request)
{
    if (request->window_id == 0 ||
        request->geometry.width == 0 || request->geometry.height == 0 ||
        request->geometry.width > 16384 || request->geometry.height > 16384) {
        return false;
    }

    /* Scene coordinates are 24.8 fixed-point; fractional part is zero in v1. */
    if ((((u32)request->geometry.x) & 0xffu) != 0 ||
        (((u32)request->geometry.y) & 0xffu) != 0) {
        return false;
    }

    if ((request->clip_width == 0) != (request->clip_height == 0)) {
        return false;
    }

    return true;
}

ErrorCode gui_encode_controller_bind(
    const GuiControllerBindRequest *request, Bytes destination,
    usize *out_size)
{
    usize index;
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < CONTROLLER_BIND_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }

    result = validate_prefix(request->prefix);
    if (result != CORE_OK) {
        return result;
    }

    encode_prefix(destination.data, request->prefix);
    for (index = 0; index < GUI_CONTROLLER_TOKEN_SIZE; ++index) {
        destination.data[10 + index] = request->controller_token[index];
    }
    *out_size = CONTROLLER_BIND_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_controller_bind(
    const IpcMessage *message, GuiControllerBindRequest *out_request)
{
    usize index;
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_COMPOSITOR_BIND_CONTROLLER ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != CONTROLLER_BIND_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }

    result = decode_prefix(message->payload.data, &out_request->prefix);
    if (result != CORE_OK) {
        return result;
    }

    for (index = 0; index < GUI_CONTROLLER_TOKEN_SIZE; ++index) {
        out_request->controller_token[index] = message->payload.data[10 + index];
    }
    return CORE_OK;
}

ErrorCode gui_encode_scene_bind(
    const GuiSceneBindRequest *request, Bytes destination, usize *out_size)
{
    usize index;
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SCENE_BIND_SIZE ||
        request->window_id == 0 || request->surface_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }

    result = validate_prefix(request->prefix);
    if (result != CORE_OK) {
        return result;
    }

    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->window_id);
    put_u64(destination.data + 18, request->surface_id);
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        destination.data[26 + index] = request->surface_bind_token[index];
    }
    *out_size = SCENE_BIND_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_scene_bind(
    const IpcMessage *message, GuiSceneBindRequest *out_request)
{
    usize index;
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_SCENE_BIND_WINDOW ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL || message->payload.size != SCENE_BIND_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }

    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->window_id = get_u64(message->payload.data + 10);
    out_request->surface_id = get_u64(message->payload.data + 18);
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        out_request->surface_bind_token[index] = message->payload.data[26 + index];
    }

    if (result != CORE_OK) {
        return result;
    }
    return out_request->window_id != 0 && out_request->surface_id != 0 ?
           CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_scene_geometry(
    const GuiSceneGeometryRequest *request, Bytes destination,
    usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SCENE_GEOMETRY_SIZE || !scene_geometry_valid(request)) {
        return CORE_INVALID_ARGUMENT;
    }

    result = validate_prefix(request->prefix);
    if (result != CORE_OK) {
        return result;
    }

    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->window_id);
    put_u32(destination.data + 18, (u32)request->geometry.x);
    put_u32(destination.data + 22, (u32)request->geometry.y);
    put_u32(destination.data + 26, request->geometry.width);
    put_u32(destination.data + 30, request->geometry.height);
    put_u32(destination.data + 34, (u32)request->clip_x);
    put_u32(destination.data + 38, (u32)request->clip_y);
    put_u32(destination.data + 42, request->clip_width);
    put_u32(destination.data + 46, request->clip_height);
    *out_size = SCENE_GEOMETRY_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_scene_geometry(
    const IpcMessage *message, GuiSceneGeometryRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_SCENE_SET_GEOMETRY ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != SCENE_GEOMETRY_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }

    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->window_id = get_u64(message->payload.data + 10);
    out_request->geometry.x = (i32)get_u32(message->payload.data + 18);
    out_request->geometry.y = (i32)get_u32(message->payload.data + 22);
    out_request->geometry.width = get_u32(message->payload.data + 26);
    out_request->geometry.height = get_u32(message->payload.data + 30);
    out_request->clip_x = (i32)get_u32(message->payload.data + 34);
    out_request->clip_y = (i32)get_u32(message->payload.data + 38);
    out_request->clip_width = get_u32(message->payload.data + 42);
    out_request->clip_height = get_u32(message->payload.data + 46);

    if (result != CORE_OK) {
        return result;
    }
    return scene_geometry_valid(out_request) ? CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_scene_visibility(
    const GuiSceneVisibilityRequest *request, Bytes destination,
    usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SCENE_VISIBILITY_SIZE || request->window_id == 0 ||
        request->visibility > GUI_SCENE_VISIBLE) {
        return CORE_INVALID_ARGUMENT;
    }

    result = validate_prefix(request->prefix);
    if (result != CORE_OK) {
        return result;
    }

    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->window_id);
    put_u32(destination.data + 18, request->visibility);
    *out_size = SCENE_VISIBILITY_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_scene_visibility(
    const IpcMessage *message, GuiSceneVisibilityRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_SCENE_SET_VISIBILITY ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != SCENE_VISIBILITY_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }

    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->window_id = get_u64(message->payload.data + 10);
    out_request->visibility = get_u32(message->payload.data + 18);

    if (result != CORE_OK) {
        return result;
    }
    return out_request->window_id != 0 &&
           out_request->visibility <= GUI_SCENE_VISIBLE ?
           CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_scene_stack(
    const GuiSceneStackRequest *request, Bytes destination,
    usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SCENE_STACK_SIZE || request->window_id == 0 ||
        request->layer < GUI_SCENE_LAYER_BACKGROUND ||
        request->layer > GUI_SCENE_LAYER_LOCK ||
        request->above_window_id == request->window_id) {
        return CORE_INVALID_ARGUMENT;
    }

    result = validate_prefix(request->prefix);
    if (result != CORE_OK) {
        return result;
    }

    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->window_id);
    put_u32(destination.data + 18, request->layer);
    put_u64(destination.data + 22, request->above_window_id);
    *out_size = SCENE_STACK_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_scene_stack(
    const IpcMessage *message, GuiSceneStackRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_SCENE_SET_STACK ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL || message->payload.size != SCENE_STACK_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }

    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->window_id = get_u64(message->payload.data + 10);
    out_request->layer = get_u32(message->payload.data + 18);
    out_request->above_window_id = get_u64(message->payload.data + 22);

    if (result != CORE_OK) {
        return result;
    }
    if (out_request->window_id == 0 ||
        out_request->layer < GUI_SCENE_LAYER_BACKGROUND ||
        out_request->layer > GUI_SCENE_LAYER_LOCK ||
        out_request->above_window_id == out_request->window_id) {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

ErrorCode gui_encode_scene_output(
    const GuiSceneOutputRequest *request, Bytes destination,
    usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SCENE_OUTPUT_SIZE || request->window_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }

    result = validate_prefix(request->prefix);
    if (result != CORE_OK) {
        return result;
    }

    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->window_id);
    put_u64(destination.data + 18, request->display_id);
    *out_size = SCENE_OUTPUT_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_scene_output(
    const IpcMessage *message, GuiSceneOutputRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_SCENE_SET_OUTPUT ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL || message->payload.size != SCENE_OUTPUT_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }

    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->window_id = get_u64(message->payload.data + 10);
    out_request->display_id = get_u64(message->payload.data + 18);
    if (result != CORE_OK) {
        return result;
    }
    return out_request->window_id != 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_scene_window(
    const GuiSceneWindowRequest *request, Bytes destination,
    usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SCENE_WINDOW_SIZE || request->window_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }

    result = validate_prefix(request->prefix);
    if (result != CORE_OK) {
        return result;
    }

    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->window_id);
    *out_size = SCENE_WINDOW_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_scene_window(
    const IpcMessage *message, GuiSceneWindowRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_SCENE_REMOVE_WINDOW ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL || message->payload.size != SCENE_WINDOW_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }

    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->window_id = get_u64(message->payload.data + 10);
    if (result != CORE_OK) {
        return result;
    }
    return out_request->window_id != 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_scene_binding_lost(
    const GuiSceneBindingLostEvent *event, Bytes destination,
    usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SCENE_BINDING_LOST_SIZE ||
        event->serial == 0 || event->window_id == 0 || event->surface_id == 0 ||
        (event->reason != GUI_SCENE_BINDING_LOST_SURFACE_DESTROYED &&
         event->reason != GUI_SCENE_BINDING_LOST_CLIENT_DISCONNECTED)) {
        return CORE_INVALID_ARGUMENT;
    }

    put_u64(destination.data, event->serial);
    put_u64(destination.data + 8, event->window_id);
    put_u64(destination.data + 16, event->surface_id);
    put_u32(destination.data + 24, event->reason);
    *out_size = SCENE_BINDING_LOST_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_scene_binding_lost(
    const IpcMessage *message, GuiSceneBindingLostEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != GUI_SCENE_BINDING_LOST ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != SCENE_BINDING_LOST_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }

    out_event->serial = get_u64(message->payload.data);
    out_event->window_id = get_u64(message->payload.data + 8);
    out_event->surface_id = get_u64(message->payload.data + 16);
    out_event->reason = get_u32(message->payload.data + 24);

    if (out_event->serial == 0 || out_event->window_id == 0 ||
        out_event->surface_id == 0 ||
        (out_event->reason != GUI_SCENE_BINDING_LOST_SURFACE_DESTROYED &&
         out_event->reason != GUI_SCENE_BINDING_LOST_CLIENT_DISCONNECTED)) {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

static bool window_role_valid(GuiWindowRole role)
{
    return role >= GUI_WINDOW_ROLE_APP && role <= GUI_WINDOW_ROLE_IME;
}

static bool window_role_parent_valid(GuiWindowRole role, u64 parent_id)
{
    if (role == GUI_WINDOW_ROLE_DIALOG || role == GUI_WINDOW_ROLE_TRANSIENT) {
        return parent_id != 0;
    }
    return parent_id == 0;
}

static bool window_flags_valid(GuiWindowFlags flags)
{
    return (flags & ~WINDOW_FLAGS_KNOWN) == 0;
}

static bool modifiers_valid(GuiModifierMask modifiers)
{
    return (modifiers & ~MODIFIER_MASK_KNOWN) == 0;
}

static bool lifecycle_valid(GuiWindowLifecycle lifecycle)
{
    return lifecycle >= GUI_WINDOW_LIFECYCLE_HIDDEN &&
           lifecycle <= GUI_WINDOW_LIFECYCLE_CLOSING;
}

static bool lifecycle_reason_valid(GuiWindowLifecycleReason reason)
{
    return reason >= GUI_WINDOW_REASON_SHELL_ACTIVATION &&
           reason <= GUI_WINDOW_REASON_CLIENT_REQUEST;
}

static bool navigation_action_valid(GuiNavigationAction action)
{
    return action >= GUI_NAVIGATION_BACK && action <= GUI_NAVIGATION_OVERVIEW;
}

static bool close_response_valid(GuiCloseResponse response)
{
    return response >= GUI_CLOSE_ALLOW && response <= GUI_CLOSE_DEFER;
}

static bool touch_phase_valid(GuiTouchPhase phase)
{
    return phase >= GUI_TOUCH_DOWN && phase <= GUI_TOUCH_CANCEL;
}

static bool pointer_phase_valid(GuiPointerPhase phase)
{
    return phase >= GUI_POINTER_MOVE && phase <= GUI_POINTER_CANCEL;
}

static bool key_phase_valid(GuiKeyPhase phase)
{
    return phase >= GUI_KEY_DOWN && phase <= GUI_KEY_REPEAT;
}

static ErrorCode string_payload_size(usize base, u16 length,
                                     usize *out_size)
{
    return core_size_add(base, (usize)length, out_size);
}

static ErrorCode validate_window_create_request(
    const GuiWindowCreateRequest *request)
{
    ErrorCode result = validate_prefix(request->prefix);

    if (result != CORE_OK) return result;
    if (request->surface_id == 0 || !window_role_valid(request->role) ||
        !window_role_parent_valid(request->role, request->parent_id) ||
        !window_flags_valid(request->flags) ||
        request->title_length > GUI_WINDOW_TITLE_MAX ||
        !utf8_is_valid(request->title, request->title_length)) {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

ErrorCode gui_encode_window_create(
    const GuiWindowCreateRequest *request, Bytes destination, usize *out_size)
{
    usize index;
    usize size;
    ErrorCode result;

    if (request == NULL || out_size == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_window_create_request(request);
    if (result == CORE_OK) {
        result = string_payload_size(WINDOW_CREATE_BASE_SIZE,
                                     request->title_length, &size);
    }
    if (result != CORE_OK) return result;
    if (destination.data == NULL || destination.size < size) {
        return CORE_INVALID_ARGUMENT;
    }

    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->surface_id);
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        destination.data[18 + index] = request->surface_bind_token[index];
    }
    destination.data[34] = request->role;
    put_u64(destination.data + 35, request->parent_id);
    put_u32(destination.data + 43, request->flags);
    put_u16(destination.data + 47, request->title_length);
    for (index = 0; index < request->title_length; ++index) {
        destination.data[49 + index] = request->title[index];
    }
    *out_size = size;
    return CORE_OK;
}

ErrorCode gui_decode_window_create(
    const IpcMessage *message, GuiWindowCreateRequest *out_request)
{
    usize index;
    usize expected;
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_WINDOW_CREATE ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size < WINDOW_CREATE_BASE_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_request->title_length = get_u16(message->payload.data + 47);
    if (out_request->title_length > GUI_WINDOW_TITLE_MAX) {
        return CORE_INVALID_ARGUMENT;
    }
    result = string_payload_size(WINDOW_CREATE_BASE_SIZE,
                                 out_request->title_length, &expected);
    if (result != CORE_OK || expected != message->payload.size) {
        return CORE_INVALID_ARGUMENT;
    }

    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->surface_id = get_u64(message->payload.data + 10);
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        out_request->surface_bind_token[index] = message->payload.data[18 + index];
    }
    out_request->role = message->payload.data[34];
    out_request->parent_id = get_u64(message->payload.data + 35);
    out_request->flags = get_u32(message->payload.data + 43);
    for (index = 0; index < out_request->title_length; ++index) {
        out_request->title[index] = message->payload.data[49 + index];
    }
    if (result != CORE_OK) return result;
    return validate_window_create_request(out_request);
}

ErrorCode gui_encode_window_create_result(
    const GuiWindowCreateResult *result, Bytes destination, usize *out_size)
{
    usize basic_size;
    ErrorCode code;

    if (result == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < WINDOW_CREATE_RESULT_SIZE ||
        ((result->result.error == CORE_OK) != (result->window_id != 0))) {
        return CORE_INVALID_ARGUMENT;
    }
    code = gui_encode_basic_result(&result->result, destination, &basic_size);
    if (code != CORE_OK) return code;
    put_u64(destination.data + basic_size, result->window_id);
    *out_size = WINDOW_CREATE_RESULT_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_window_create_result(
    const IpcMessage *message, GuiWindowCreateResult *out_result)
{
    if (message == NULL || out_result == NULL ||
        message->type != GUI_WINDOW_CREATE_RESULT ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != WINDOW_CREATE_RESULT_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_result->result.error = (ErrorCode)get_u32(message->payload.data);
    out_result->result.request_id = get_u64(message->payload.data + 4);
    out_result->window_id = get_u64(message->payload.data + 12);
    if (!error_is_known(out_result->result.error) ||
        out_result->result.request_id == 0 ||
        ((out_result->result.error == CORE_OK) !=
         (out_result->window_id != 0))) {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

ErrorCode gui_encode_window_id_request(
    const GuiWindowIdRequest *request, Bytes destination, usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < WINDOW_ID_REQUEST_SIZE || request->window_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result != CORE_OK) return result;
    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->window_id);
    *out_size = WINDOW_ID_REQUEST_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_window_id_request(
    const IpcMessage *message, IpcMessageType expected_type,
    GuiWindowIdRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL || message->type != expected_type ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != WINDOW_ID_REQUEST_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->window_id = get_u64(message->payload.data + 10);
    if (result != CORE_OK) return result;
    return out_request->window_id != 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_window_set_title(
    const GuiWindowSetTitleRequest *request, Bytes destination,
    usize *out_size)
{
    usize index;
    usize size;
    ErrorCode result;

    if (request == NULL || out_size == NULL) return CORE_INVALID_ARGUMENT;
    result = validate_prefix(request->prefix);
    if (result == CORE_OK &&
        (request->window_id == 0 ||
         request->title_length > GUI_WINDOW_TITLE_MAX ||
         !utf8_is_valid(request->title, request->title_length))) {
        result = CORE_INVALID_ARGUMENT;
    }
    if (result == CORE_OK) {
        result = string_payload_size(WINDOW_SET_TITLE_BASE_SIZE,
                                     request->title_length, &size);
    }
    if (result != CORE_OK) return result;
    if (destination.data == NULL || destination.size < size) {
        return CORE_INVALID_ARGUMENT;
    }
    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->window_id);
    put_u16(destination.data + 18, request->title_length);
    for (index = 0; index < request->title_length; ++index) {
        destination.data[20 + index] = request->title[index];
    }
    *out_size = size;
    return CORE_OK;
}

ErrorCode gui_decode_window_set_title(
    const IpcMessage *message, GuiWindowSetTitleRequest *out_request)
{
    usize index;
    usize expected;
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_WINDOW_SET_TITLE ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size < WINDOW_SET_TITLE_BASE_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_request->title_length = get_u16(message->payload.data + 18);
    if (out_request->title_length > GUI_WINDOW_TITLE_MAX) {
        return CORE_INVALID_ARGUMENT;
    }
    result = string_payload_size(WINDOW_SET_TITLE_BASE_SIZE,
                                 out_request->title_length, &expected);
    if (result != CORE_OK || expected != message->payload.size) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->window_id = get_u64(message->payload.data + 10);
    for (index = 0; index < out_request->title_length; ++index) {
        out_request->title[index] = message->payload.data[20 + index];
    }
    if (result != CORE_OK) return result;
    if (out_request->window_id == 0 ||
        out_request->title_length > GUI_WINDOW_TITLE_MAX ||
        !utf8_is_valid(out_request->title, out_request->title_length)) {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

ErrorCode gui_encode_window_ack_configure(
    const GuiWindowAckConfigureRequest *request, Bytes destination,
    usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < WINDOW_ACK_CONFIGURE_SIZE ||
        request->window_id == 0 || request->configure_serial == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result != CORE_OK) return result;
    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->window_id);
    put_u64(destination.data + 18, request->configure_serial);
    *out_size = WINDOW_ACK_CONFIGURE_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_window_ack_configure(
    const IpcMessage *message, GuiWindowAckConfigureRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_WINDOW_ACK_CONFIGURE ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != WINDOW_ACK_CONFIGURE_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->window_id = get_u64(message->payload.data + 10);
    out_request->configure_serial = get_u64(message->payload.data + 18);
    if (result != CORE_OK) return result;
    return out_request->window_id != 0 && out_request->configure_serial != 0 ?
           CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_window_modal(
    const GuiWindowModalRequest *request, Bytes destination, usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < WINDOW_MODAL_SIZE || request->window_id == 0 ||
        request->parent_id == request->window_id) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result != CORE_OK) return result;
    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->window_id);
    put_u64(destination.data + 18, request->parent_id);
    *out_size = WINDOW_MODAL_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_window_modal(
    const IpcMessage *message, GuiWindowModalRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_WINDOW_SET_MODAL ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != WINDOW_MODAL_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->window_id = get_u64(message->payload.data + 10);
    out_request->parent_id = get_u64(message->payload.data + 18);
    if (result != CORE_OK) return result;
    return out_request->window_id != 0 &&
           out_request->parent_id != out_request->window_id ?
           CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_window_surface_rebind(
    const GuiWindowSurfaceRebindRequest *request, Bytes destination,
    usize *out_size)
{
    usize index;
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < WINDOW_SURFACE_REBIND_SIZE ||
        request->window_id == 0 || request->surface_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result != CORE_OK) return result;
    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->window_id);
    put_u64(destination.data + 18, request->surface_id);
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        destination.data[26 + index] = request->surface_bind_token[index];
    }
    *out_size = WINDOW_SURFACE_REBIND_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_window_surface_rebind(
    const IpcMessage *message, GuiWindowSurfaceRebindRequest *out_request)
{
    usize index;
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_WINDOW_SURFACE_REBIND ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != WINDOW_SURFACE_REBIND_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->window_id = get_u64(message->payload.data + 10);
    out_request->surface_id = get_u64(message->payload.data + 18);
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        out_request->surface_bind_token[index] = message->payload.data[26 + index];
    }
    if (result != CORE_OK) return result;
    return out_request->window_id != 0 && out_request->surface_id != 0 ?
           CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_shell_bind(
    const GuiShellBindRequest *request, Bytes destination, usize *out_size)
{
    usize index;
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SHELL_BIND_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result != CORE_OK) return result;
    encode_prefix(destination.data, request->prefix);
    for (index = 0; index < GUI_SHELL_TOKEN_SIZE; ++index) {
        destination.data[10 + index] = request->shell_token[index];
    }
    *out_size = SHELL_BIND_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_shell_bind(
    const IpcMessage *message, GuiShellBindRequest *out_request)
{
    usize index;
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_WINDOW_BIND_SHELL ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL || message->payload.size != SHELL_BIND_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    if (result != CORE_OK) return result;
    for (index = 0; index < GUI_SHELL_TOKEN_SIZE; ++index) {
        out_request->shell_token[index] = message->payload.data[10 + index];
    }
    return CORE_OK;
}

ErrorCode gui_encode_shell_activate(
    const GuiShellActivateRequest *request, Bytes destination, usize *out_size)
{
    GuiWindowIdRequest wrapped;

    if (request == NULL) return CORE_INVALID_ARGUMENT;
    wrapped.prefix = request->prefix;
    wrapped.window_id = request->window_id;
    return gui_encode_window_id_request(&wrapped, destination, out_size);
}

ErrorCode gui_decode_shell_activate(
    const IpcMessage *message, GuiShellActivateRequest *out_request)
{
    GuiWindowIdRequest decoded;
    ErrorCode result;

    if (out_request == NULL) return CORE_INVALID_ARGUMENT;
    result = gui_decode_window_id_request(message, GUI_SHELL_ACTIVATE, &decoded);
    if (result != CORE_OK) return result;
    out_request->prefix = decoded.prefix;
    out_request->window_id = decoded.window_id;
    return CORE_OK;
}

ErrorCode gui_encode_shell_insets(
    const GuiShellInsetsRequest *request, Bytes destination, usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SHELL_INSETS_SIZE || request->display_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result != CORE_OK) return result;
    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->display_id);
    put_u32(destination.data + 18, request->insets.top);
    put_u32(destination.data + 22, request->insets.right);
    put_u32(destination.data + 26, request->insets.bottom);
    put_u32(destination.data + 30, request->insets.left);
    *out_size = SHELL_INSETS_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_shell_insets(
    const IpcMessage *message, GuiShellInsetsRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_SHELL_SET_INSETS ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL || message->payload.size != SHELL_INSETS_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->display_id = get_u64(message->payload.data + 10);
    out_request->insets.top = get_u32(message->payload.data + 18);
    out_request->insets.right = get_u32(message->payload.data + 22);
    out_request->insets.bottom = get_u32(message->payload.data + 26);
    out_request->insets.left = get_u32(message->payload.data + 30);
    if (result != CORE_OK) return result;
    return out_request->display_id != 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_shell_navigate(
    const GuiShellNavigateRequest *request, Bytes destination,
    usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < SHELL_NAVIGATE_SIZE ||
        !navigation_action_valid(request->action)) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result != CORE_OK) return result;
    encode_prefix(destination.data, request->prefix);
    destination.data[10] = request->action;
    put_u64(destination.data + 11, request->target_id);
    *out_size = SHELL_NAVIGATE_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_shell_navigate(
    const IpcMessage *message, GuiShellNavigateRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_SHELL_NAVIGATE ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL || message->payload.size != SHELL_NAVIGATE_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->action = message->payload.data[10];
    out_request->target_id = get_u64(message->payload.data + 11);
    if (result != CORE_OK) return result;
    return navigation_action_valid(out_request->action) ? CORE_OK :
           CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_window_close_response(
    const GuiWindowCloseResponseRequest *request, Bytes destination,
    usize *out_size)
{
    ErrorCode result;

    if (request == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < WINDOW_CLOSE_RESPONSE_SIZE ||
        request->window_id == 0 || request->close_serial == 0 ||
        !close_response_valid(request->response)) {
        return CORE_INVALID_ARGUMENT;
    }
    result = validate_prefix(request->prefix);
    if (result != CORE_OK) return result;
    encode_prefix(destination.data, request->prefix);
    put_u64(destination.data + 10, request->window_id);
    put_u64(destination.data + 18, request->close_serial);
    destination.data[26] = request->response;
    *out_size = WINDOW_CLOSE_RESPONSE_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_window_close_response(
    const IpcMessage *message, GuiWindowCloseResponseRequest *out_request)
{
    ErrorCode result;

    if (message == NULL || out_request == NULL ||
        message->type != GUI_WINDOW_RESPOND_CLOSE ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != WINDOW_CLOSE_RESPONSE_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    result = decode_prefix(message->payload.data, &out_request->prefix);
    out_request->window_id = get_u64(message->payload.data + 10);
    out_request->close_serial = get_u64(message->payload.data + 18);
    out_request->response = message->payload.data[26];
    if (result != CORE_OK) return result;
    return out_request->window_id != 0 && out_request->close_serial != 0 &&
           close_response_valid(out_request->response) ?
           CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_window_configure(
    const GuiWindowConfigureEvent *event, Bytes destination, usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < WINDOW_CONFIGURE_SIZE || event->window_id == 0 ||
        event->configure_serial == 0 || event->width == 0 ||
        event->height == 0 || event->scale == 0 ||
        event->orientation > GUI_ORIENTATION_LEFT) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->window_id);
    put_u64(destination.data + 8, event->configure_serial);
    put_u32(destination.data + 16, event->width);
    put_u32(destination.data + 20, event->height);
    put_u32(destination.data + 24, event->scale);
    destination.data[28] = event->orientation;
    put_u32(destination.data + 29, event->safe_insets.top);
    put_u32(destination.data + 33, event->safe_insets.right);
    put_u32(destination.data + 37, event->safe_insets.bottom);
    put_u32(destination.data + 41, event->safe_insets.left);
    *out_size = WINDOW_CONFIGURE_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_window_configure(
    const IpcMessage *message, GuiWindowConfigureEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != GUI_WINDOW_CONFIGURE ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != WINDOW_CONFIGURE_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->window_id = get_u64(message->payload.data);
    out_event->configure_serial = get_u64(message->payload.data + 8);
    out_event->width = get_u32(message->payload.data + 16);
    out_event->height = get_u32(message->payload.data + 20);
    out_event->scale = get_u32(message->payload.data + 24);
    out_event->orientation = message->payload.data[28];
    out_event->safe_insets.top = get_u32(message->payload.data + 29);
    out_event->safe_insets.right = get_u32(message->payload.data + 33);
    out_event->safe_insets.bottom = get_u32(message->payload.data + 37);
    out_event->safe_insets.left = get_u32(message->payload.data + 41);
    return out_event->window_id != 0 && out_event->configure_serial != 0 &&
           out_event->width != 0 && out_event->height != 0 &&
           out_event->scale != 0 &&
           out_event->orientation <= GUI_ORIENTATION_LEFT ?
           CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_window_lifecycle(
    const GuiWindowLifecycleEvent *event, Bytes destination, usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < WINDOW_LIFECYCLE_SIZE || event->serial == 0 ||
        event->window_id == 0 || !lifecycle_valid(event->lifecycle) ||
        !lifecycle_reason_valid(event->reason)) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->serial);
    put_u64(destination.data + 8, event->window_id);
    destination.data[16] = event->lifecycle;
    destination.data[17] = event->reason;
    *out_size = WINDOW_LIFECYCLE_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_window_lifecycle(
    const IpcMessage *message, GuiWindowLifecycleEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != GUI_WINDOW_LIFECYCLE_CHANGED ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != WINDOW_LIFECYCLE_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->serial = get_u64(message->payload.data);
    out_event->window_id = get_u64(message->payload.data + 8);
    out_event->lifecycle = message->payload.data[16];
    out_event->reason = message->payload.data[17];
    return out_event->serial != 0 && out_event->window_id != 0 &&
           lifecycle_valid(out_event->lifecycle) &&
           lifecycle_reason_valid(out_event->reason) ?
           CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_window_close_event(
    const GuiWindowCloseEvent *event, Bytes destination, usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < WINDOW_CLOSE_SIZE || event->close_serial == 0 ||
        event->window_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->close_serial);
    put_u64(destination.data + 8, event->window_id);
    *out_size = WINDOW_CLOSE_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_window_close_event(
    const IpcMessage *message, GuiWindowCloseEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != GUI_WINDOW_CLOSE_REQUESTED ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL || message->payload.size != WINDOW_CLOSE_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->close_serial = get_u64(message->payload.data);
    out_event->window_id = get_u64(message->payload.data + 8);
    return out_event->close_serial != 0 && out_event->window_id != 0 ?
           CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_window_visibility(
    const GuiWindowVisibilityEvent *event, Bytes destination, usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < WINDOW_VISIBILITY_SIZE || event->serial == 0 ||
        event->window_id == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->serial);
    put_u64(destination.data + 8, event->window_id);
    destination.data[16] = event->visible ? 1 : 0;
    *out_size = WINDOW_VISIBILITY_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_window_visibility(
    const IpcMessage *message, GuiWindowVisibilityEvent *out_event)
{
    u8 visible;

    if (message == NULL || out_event == NULL ||
        message->type != GUI_WINDOW_VISIBILITY_CHANGED ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != WINDOW_VISIBILITY_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->serial = get_u64(message->payload.data);
    out_event->window_id = get_u64(message->payload.data + 8);
    visible = message->payload.data[16];
    out_event->visible = visible == 1;
    return out_event->serial != 0 && out_event->window_id != 0 &&
           visible <= 1 ? CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_window_pointer(
    const GuiWindowPointerEvent *event, Bytes destination, usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < WINDOW_POINTER_SIZE || event->window_id == 0 ||
        event->input_serial == 0 || !pointer_phase_valid(event->phase) ||
        !modifiers_valid(event->modifiers)) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->window_id);
    put_u64(destination.data + 8, event->input_serial);
    put_u64(destination.data + 16, event->timestamp_ms);
    put_u32(destination.data + 24, event->pointer_id);
    destination.data[28] = event->phase;
    put_u32(destination.data + 29, event->buttons);
    put_u32(destination.data + 33, event->modifiers);
    put_u32(destination.data + 37, (u32)event->x);
    put_u32(destination.data + 41, (u32)event->y);
    *out_size = WINDOW_POINTER_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_window_pointer(
    const IpcMessage *message, GuiWindowPointerEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != GUI_WINDOW_POINTER ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL || message->payload.size != WINDOW_POINTER_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->window_id = get_u64(message->payload.data);
    out_event->input_serial = get_u64(message->payload.data + 8);
    out_event->timestamp_ms = get_u64(message->payload.data + 16);
    out_event->pointer_id = get_u32(message->payload.data + 24);
    out_event->phase = message->payload.data[28];
    out_event->buttons = get_u32(message->payload.data + 29);
    out_event->modifiers = get_u32(message->payload.data + 33);
    out_event->x = (i32)get_u32(message->payload.data + 37);
    out_event->y = (i32)get_u32(message->payload.data + 41);
    return out_event->window_id != 0 && out_event->input_serial != 0 &&
           pointer_phase_valid(out_event->phase) &&
           modifiers_valid(out_event->modifiers) ?
           CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_window_key(
    const GuiWindowKeyEvent *event, Bytes destination, usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < WINDOW_KEY_SIZE || event->window_id == 0 ||
        event->input_serial == 0 || !key_phase_valid(event->phase) ||
        !modifiers_valid(event->modifiers)) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->window_id);
    put_u64(destination.data + 8, event->input_serial);
    put_u64(destination.data + 16, event->timestamp_ms);
    put_u32(destination.data + 24, event->key_code);
    destination.data[28] = event->phase;
    put_u32(destination.data + 29, event->modifiers);
    *out_size = WINDOW_KEY_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_window_key(
    const IpcMessage *message, GuiWindowKeyEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != GUI_WINDOW_KEY ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL || message->payload.size != WINDOW_KEY_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->window_id = get_u64(message->payload.data);
    out_event->input_serial = get_u64(message->payload.data + 8);
    out_event->timestamp_ms = get_u64(message->payload.data + 16);
    out_event->key_code = get_u32(message->payload.data + 24);
    out_event->phase = message->payload.data[28];
    out_event->modifiers = get_u32(message->payload.data + 29);
    return out_event->window_id != 0 && out_event->input_serial != 0 &&
           key_phase_valid(out_event->phase) &&
           modifiers_valid(out_event->modifiers) ?
           CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_window_touch(
    const GuiWindowTouchEvent *event, Bytes destination, usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < WINDOW_TOUCH_SIZE || event->window_id == 0 ||
        event->input_serial == 0 || !touch_phase_valid(event->phase)) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->window_id);
    put_u64(destination.data + 8, event->input_serial);
    put_u64(destination.data + 16, event->timestamp_ms);
    put_u32(destination.data + 24, event->touch_id);
    destination.data[28] = event->phase;
    put_u32(destination.data + 29, (u32)event->x);
    put_u32(destination.data + 33, (u32)event->y);
    *out_size = WINDOW_TOUCH_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_window_touch(
    const IpcMessage *message, GuiWindowTouchEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != GUI_WINDOW_TOUCH ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL || message->payload.size != WINDOW_TOUCH_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->window_id = get_u64(message->payload.data);
    out_event->input_serial = get_u64(message->payload.data + 8);
    out_event->timestamp_ms = get_u64(message->payload.data + 16);
    out_event->touch_id = get_u32(message->payload.data + 24);
    out_event->phase = message->payload.data[28];
    out_event->x = (i32)get_u32(message->payload.data + 29);
    out_event->y = (i32)get_u32(message->payload.data + 33);
    return out_event->window_id != 0 && out_event->input_serial != 0 &&
           touch_phase_valid(out_event->phase) ?
           CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_window_service_event(
    const GuiWindowServiceEvent *event, Bytes destination, usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < WINDOW_SERVICE_SIZE || event->serial == 0 ||
        event->window_id == 0 || !lifecycle_reason_valid(event->reason)) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->serial);
    put_u64(destination.data + 8, event->window_id);
    destination.data[16] = event->reason;
    *out_size = WINDOW_SERVICE_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_window_service_event(
    const IpcMessage *message, GuiWindowServiceEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != GUI_WINDOW_RENDERING_SUSPENDED ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL || message->payload.size != WINDOW_SERVICE_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->serial = get_u64(message->payload.data);
    out_event->window_id = get_u64(message->payload.data + 8);
    out_event->reason = message->payload.data[16];
    return out_event->serial != 0 && out_event->window_id != 0 &&
           lifecycle_reason_valid(out_event->reason) ?
           CORE_OK : CORE_INVALID_ARGUMENT;
}

ErrorCode gui_encode_window_navigation(
    const GuiWindowNavigationEvent *event, Bytes destination, usize *out_size)
{
    if (event == NULL || out_size == NULL || destination.data == NULL ||
        destination.size < WINDOW_NAVIGATION_SIZE || event->serial == 0 ||
        event->window_id == 0 || !navigation_action_valid(event->action)) {
        return CORE_INVALID_ARGUMENT;
    }
    put_u64(destination.data, event->serial);
    put_u64(destination.data + 8, event->window_id);
    destination.data[16] = event->action;
    *out_size = WINDOW_NAVIGATION_SIZE;
    return CORE_OK;
}

ErrorCode gui_decode_window_navigation(
    const IpcMessage *message, GuiWindowNavigationEvent *out_event)
{
    if (message == NULL || out_event == NULL ||
        message->type != GUI_WINDOW_NAVIGATION_REQUESTED ||
        message->descriptor != IPC_NO_DESCRIPTOR ||
        message->payload.data == NULL ||
        message->payload.size != WINDOW_NAVIGATION_SIZE) {
        return CORE_INVALID_ARGUMENT;
    }
    out_event->serial = get_u64(message->payload.data);
    out_event->window_id = get_u64(message->payload.data + 8);
    out_event->action = message->payload.data[16];
    return out_event->serial != 0 && out_event->window_id != 0 &&
           navigation_action_valid(out_event->action) ?
           CORE_OK : CORE_INVALID_ARGUMENT;
}
