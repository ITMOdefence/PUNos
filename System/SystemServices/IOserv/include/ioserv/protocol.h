#ifndef PUNOS_IOSERV_PROTOCOL_H
#define PUNOS_IOSERV_PROTOCOL_H
#include <special/input.h>
#include <special/ipc.h>

#define IO_SUBSCRIBE 1u
#define IO_SUBSCRIBE_RESULT 2u
#define IO_UNSUBSCRIBE 3u
#define IO_UNSUBSCRIBE_RESULT 4u
#define IO_INPUT_FRAME 5u
#define IO_DEVICE_ADDED 6u
#define IO_DEVICE_REMOVED 7u
#define IO_ERROR 8u

#define IO_DEVICE_EVENT_SIZE 20u
#define IO_FRAME_HEADER_SIZE 18u
#define IO_EVENT_SIZE 7u

ErrorCode io_request_error(const IpcMessage *request, u32 *out_result_type,
                           InputDeviceClass *out_mask);
void io_encode_device(u8 out[IO_DEVICE_EVENT_SIZE], u64 id,
                      const InputDeviceInfo *info);
ErrorCode io_encode_frame(u8 *out, usize capacity, u64 id, u64 sequence,
                          const InputFrame *frame, usize *out_size);
#endif
