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
#define CONTROLLER_BIND_SIZE ((usize)42)
#define SCENE_BIND_SIZE ((usize)42)
#define SCENE_GEOMETRY_SIZE ((usize)50)
#define SCENE_VISIBILITY_SIZE ((usize)22)
#define SCENE_STACK_SIZE ((usize)30)
#define SCENE_OUTPUT_SIZE ((usize)26)
#define SCENE_WINDOW_SIZE ((usize)18)
#define SCENE_BINDING_LOST_SIZE ((usize)28)
#define DISPLAY_FLAGS_KNOWN (GUI_DISPLAY_CONNECTED | GUI_DISPLAY_ENABLED | \
                             GUI_DISPLAY_PRIMARY)
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