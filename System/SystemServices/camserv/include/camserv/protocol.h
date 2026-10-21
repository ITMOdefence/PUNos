#ifndef PUNOS_CAMSERV_PROTOCOL_H
#define PUNOS_CAMSERV_PROTOCOL_H
#include <special/camera.h>
#include <special/ipc.h>

#define CAM_GET_INFO 1u
#define CAM_GET_INFO_RESULT 2u
#define CAM_SUBSCRIBE 3u
#define CAM_SUBSCRIBE_RESULT 4u
#define CAM_UNSUBSCRIBE 5u
#define CAM_UNSUBSCRIBE_RESULT 6u
#define CAM_READY 7u
#define CAM_FRAME 8u
#define CAM_DEVICE_LOST 9u
#define CAM_ERROR 10u

#define CAM_STATE_UNAVAILABLE 0u
#define CAM_STATE_STREAMING 1u
#define CAM_CONFIG_SIZE 20u
#define CAM_INFO_SIZE 25u
#define CAM_FRAME_HEADER_SIZE 20u

ErrorCode cam_request_error(const IpcMessage *request, u32 *out_result_type);
void cam_encode_config(u8 out[CAM_CONFIG_SIZE], const CameraStreamConfig *config);
void cam_encode_info(u8 out[CAM_INFO_SIZE], ErrorCode error,
                     const CameraStreamConfig *config);
ErrorCode cam_encode_frame(u8 *out, usize capacity, const CameraFrame *frame,
                           usize *out_size);
#endif
