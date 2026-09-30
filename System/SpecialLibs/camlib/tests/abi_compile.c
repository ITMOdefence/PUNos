/*
 * Compile-time checks of the camlib ABI against DevDoc/SpecialLibs/camlib.md.
 * This file only has to compile; it produces no runnable program.
 */
#include <special/camera.h>

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)

_Static_assert(SAME_TYPE((CameraPixelFormat)0, u32), "CameraPixelFormat size");
_Static_assert(SAME_TYPE((CameraReadStatus)0, u32), "CameraReadStatus size");
_Static_assert(sizeof(CameraStreamConfig) == 20, "CameraStreamConfig ABI");
_Static_assert(sizeof(CameraConfigurationList) == 16, "CameraConfigurationList ABI");
_Static_assert(sizeof(CameraFrame) == 40, "CameraFrame ABI");

_Static_assert(CAMERA_BUFFER_COUNT == 4, "CAMERA_BUFFER_COUNT");
_Static_assert(CAMERA_MIN_WIDTH == 1, "CAMERA_MIN_WIDTH");
_Static_assert(CAMERA_MIN_HEIGHT == 1, "CAMERA_MIN_HEIGHT");
_Static_assert(CAMERA_MIN_FRAMES_PER_SECOND == 1, "CAMERA_MIN_FRAMES_PER_SECOND");

_Static_assert(CAMERA_PIXEL_FORMAT_YUYV == 0, "CAMERA_PIXEL_FORMAT_YUYV");
_Static_assert(CAMERA_PIXEL_FORMAT_MJPEG == 1, "CAMERA_PIXEL_FORMAT_MJPEG");
_Static_assert(CAMERA_PIXEL_FORMAT_NV12 == 2, "CAMERA_PIXEL_FORMAT_NV12");

_Static_assert(CAMERA_READ_STATUS_FRAME_READY == 0, "CAMERA_READ_STATUS_FRAME_READY");
_Static_assert(CAMERA_READ_STATUS_NO_FRAME == 1, "CAMERA_READ_STATUS_NO_FRAME");
_Static_assert(CAMERA_READ_STATUS_DEVICE_LOST == 2, "CAMERA_READ_STATUS_DEVICE_LOST");

int main(void)
{
    return 0;
}
