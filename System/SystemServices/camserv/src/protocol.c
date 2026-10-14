#include <camserv/protocol.h>
#include "../../common/wire.h"

ErrorCode cam_request_error(const IpcMessage *request, u32 *out_result_type)
{
    if (request == NULL || out_result_type == NULL) return CORE_INVALID_ARGUMENT;
    switch (request->type) {
    case CAM_GET_INFO:
        *out_result_type = CAM_GET_INFO_RESULT;
        return request->payload.size == 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
    case CAM_SUBSCRIBE:
        *out_result_type = CAM_SUBSCRIBE_RESULT;
        if (request->payload.size != 2) return CORE_INVALID_ARGUMENT;
        return wire_read(request->payload.data, 2) == 1 ? CORE_OK : CORE_UNSUPPORTED;
    case CAM_UNSUBSCRIBE:
        *out_result_type = CAM_UNSUBSCRIBE_RESULT;
        return request->payload.size == 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
    default:
        *out_result_type = CAM_ERROR;
        return CORE_INVALID_ARGUMENT;
    }
}

void cam_encode_config(u8 out[CAM_CONFIG_SIZE], const CameraStreamConfig *config)
{
    u32 format = config->format == CAMERA_PIXEL_FORMAT_YUYV ? 1u :
                 config->format == CAMERA_PIXEL_FORMAT_MJPEG ? 2u : 3u;
    wire_write(out, 4, format);
    wire_write(out + 4, 4, config->width);
    wire_write(out + 8, 4, config->height);
    wire_write(out + 12, 4, config->frames_per_second);
    wire_write(out + 16, 4, config->max_frame_bytes);
}

void cam_encode_info(u8 out[CAM_INFO_SIZE], ErrorCode error,
                     const CameraStreamConfig *config)
{
    wire_write(out, 4, error);
    out[4] = config == NULL ? CAM_STATE_UNAVAILABLE : CAM_STATE_STREAMING;
    for (usize i = 5; i < CAM_INFO_SIZE; ++i) out[i] = 0;
    if (config != NULL) cam_encode_config(out + 5, config);
}

ErrorCode cam_encode_frame(u8 *out, usize capacity, const CameraFrame *frame,
                           usize *out_size)
{
    if (out == NULL || frame == NULL || out_size == NULL ||
        frame->data.size > IPC_MESSAGE_MAX_SIZE - CAM_FRAME_HEADER_SIZE ||
        capacity < CAM_FRAME_HEADER_SIZE + frame->data.size ||
        (frame->data.size && frame->data.data == NULL)) return CORE_INVALID_ARGUMENT;
    wire_write(out, 8, frame->sequence);
    wire_write(out + 8, 8, frame->timestamp);
    wire_write(out + 16, 4, frame->data.size);
    for (usize i = 0; i < frame->data.size; ++i) out[CAM_FRAME_HEADER_SIZE + i] = frame->data.data[i];
    *out_size = CAM_FRAME_HEADER_SIZE + frame->data.size;
    return CORE_OK;
}
