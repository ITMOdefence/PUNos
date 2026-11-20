#ifndef PUNOS_CAMSERV_PROTOCOL_H
#define PUNOS_CAMSERV_PROTOCOL_H
#include <special/camera.h>
#include <special/ipc.h>

#include <camserv/wire.h>

ErrorCode cam_request_error(const IpcMessage *request, u32 *out_result_type);
void cam_encode_config(u8 out[CAM_CONFIG_SIZE], const CameraStreamConfig *config);
void cam_encode_info(u8 out[CAM_INFO_SIZE], ErrorCode error,
                     const CameraStreamConfig *config);
ErrorCode cam_encode_frame(u8 *out, usize capacity, const CameraFrame *frame,
                           usize *out_size);
#endif
