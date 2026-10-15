#include <ioserv/protocol.h>
#include "../../common/wire.h"

ErrorCode io_request_error(const IpcMessage *request, u32 *out_result_type,
                           InputDeviceClass *out_mask)
{
    if (request == NULL || out_result_type == NULL || out_mask == NULL)
        return CORE_INVALID_ARGUMENT;
    switch (request->type) {
    case IO_SUBSCRIBE:
        *out_result_type = IO_SUBSCRIBE_RESULT;
        if (request->payload.size != 6) return CORE_INVALID_ARGUMENT;
        if (wire_read(request->payload.data, 2) != 1) return CORE_UNSUPPORTED;
        *out_mask = (u32)wire_read(request->payload.data + 2, 4);
        if (*out_mask == 0) return CORE_INVALID_ARGUMENT;
        return (*out_mask & ~(INPUT_DEVICE_KEYBOARD | INPUT_DEVICE_POINTER |
                              INPUT_DEVICE_TOUCH)) ? CORE_UNSUPPORTED : CORE_OK;
    case IO_UNSUBSCRIBE:
        *out_result_type = IO_UNSUBSCRIBE_RESULT;
        return request->payload.size == 0 ? CORE_OK : CORE_INVALID_ARGUMENT;
    default:
        *out_result_type = IO_ERROR;
        return CORE_INVALID_ARGUMENT;
    }
}

void io_encode_device(u8 out[IO_DEVICE_EVENT_SIZE], u64 id,
                      const InputDeviceInfo *info)
{
    wire_write(out, 8, id);
    wire_write(out + 8, 2, info->bus);
    wire_write(out + 10, 2, info->vendor);
    wire_write(out + 12, 2, info->product);
    wire_write(out + 14, 2, info->version);
    wire_write(out + 16, 4, info->classes);
}

ErrorCode io_encode_frame(u8 *out, usize capacity, u64 id, u64 sequence,
                          const InputFrame *frame, usize *out_size)
{
    if (out == NULL || frame == NULL || out_size == NULL || frame->events == NULL ||
        frame->count == 0 || frame->count > INPUT_FRAME_MAX_EVENTS ||
        capacity < IO_FRAME_HEADER_SIZE + IO_EVENT_SIZE * frame->count)
        return CORE_INVALID_ARGUMENT;
    wire_write(out, 8, id);
    wire_write(out + 8, 8, sequence);
    wire_write(out + 16, 2, frame->count);
    for (usize i = 0; i < frame->count; ++i) {
        usize at = IO_FRAME_HEADER_SIZE + IO_EVENT_SIZE * i;
        if (frame->events[i].kind > INPUT_EVENT_KIND_RAW) return CORE_INVALID_ARGUMENT;
        out[at] = (u8)frame->events[i].kind + 1;
        wire_write(out + at + 1, 2, frame->events[i].code);
        wire_write(out + at + 3, 4, (u32)frame->events[i].value);
    }
    *out_size = IO_FRAME_HEADER_SIZE + IO_EVENT_SIZE * frame->count;
    return CORE_OK;
}
