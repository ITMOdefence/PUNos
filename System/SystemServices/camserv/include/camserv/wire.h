#ifndef PUNOS_CAMSERV_WIRE_H
#define PUNOS_CAMSERV_WIRE_H
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
/* Subscription v2: one frame in shared memory, followed by explicit unsubscribe. */
#define CAM_FRAME_SHARED 11u
#define CAM_SNAPSHOT_MAX_SIZE ((usize)16 * 1024 * 1024)

#define CAM_STATE_UNAVAILABLE 0u
#define CAM_STATE_STREAMING 1u
#define CAM_CONFIG_SIZE 20u
#define CAM_INFO_SIZE 25u
#define CAM_FRAME_HEADER_SIZE 20u

#endif
