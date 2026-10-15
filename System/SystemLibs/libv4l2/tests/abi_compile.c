/*
 * Compile-time checks of the libv4l2 ABI against DevDoc/SystemLibs/libv4l2.md.
 * This file only has to compile; it produces no runnable program.
 */
#include <sys/v4l2.h>

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)

_Static_assert(SAME_TYPE((V4l2PixelFormat)0, u32), "V4l2PixelFormat must be u32");

_Static_assert(V4L2_BUFFER_COUNT == 4, "V4L2_BUFFER_COUNT");
_Static_assert(V4L2_MIN_WIDTH == 1, "V4L2_MIN_WIDTH");
_Static_assert(V4L2_MIN_HEIGHT == 1, "V4L2_MIN_HEIGHT");
_Static_assert(V4L2_MIN_FRAMES_PER_SECOND == 1, "V4L2_MIN_FRAMES_PER_SECOND");

/*
 * V4l2DequeueStatus values are not pinned by the specification; this
 * library numbers them in the order the specification's table lists them.
 */
_Static_assert(V4L2_DEQUEUE_STATUS_BUFFER_READY == 0, "V4L2_DEQUEUE_STATUS_BUFFER_READY");
_Static_assert(V4L2_DEQUEUE_STATUS_NO_BUFFER == 1, "V4L2_DEQUEUE_STATUS_NO_BUFFER");
_Static_assert(V4L2_DEQUEUE_STATUS_DEVICE_LOST == 2, "V4L2_DEQUEUE_STATUS_DEVICE_LOST");

/* V4l2StreamConfig layout: pixel_format, width, height, fps, max_frame_bytes. */
_Static_assert(__builtin_offsetof(V4l2StreamConfig, pixel_format) == 0,
               "V4l2StreamConfig.pixel_format offset");
_Static_assert(__builtin_offsetof(V4l2StreamConfig, width) == 4, "V4l2StreamConfig.width offset");
_Static_assert(__builtin_offsetof(V4l2StreamConfig, height) == 8,
               "V4l2StreamConfig.height offset");
_Static_assert(__builtin_offsetof(V4l2StreamConfig, frames_per_second) == 12,
               "V4l2StreamConfig.frames_per_second offset");
_Static_assert(__builtin_offsetof(V4l2StreamConfig, max_frame_bytes) == 16,
               "V4l2StreamConfig.max_frame_bytes offset");
_Static_assert(__builtin_offsetof(V4l2StreamConfig, frame_rate_numerator) == 20,
               "V4l2StreamConfig.frame_rate_numerator offset");
_Static_assert(__builtin_offsetof(V4l2StreamConfig, frame_rate_denominator) == 24,
               "V4l2StreamConfig.frame_rate_denominator offset");

/* V4l2Buffer layout: index, data (Bytes, 8-byte aligned), timestamp, sequence. */
_Static_assert(__builtin_offsetof(V4l2Buffer, index) == 0, "V4l2Buffer.index offset");
_Static_assert(__builtin_offsetof(V4l2Buffer, data) == 8, "V4l2Buffer.data offset");
_Static_assert(__builtin_offsetof(V4l2Buffer, timestamp) == 24, "V4l2Buffer.timestamp offset");
_Static_assert(__builtin_offsetof(V4l2Buffer, sequence) == 32, "V4l2Buffer.sequence offset");
_Static_assert(SAME_TYPE(((V4l2Buffer *)0)->data, Bytes), "V4l2Buffer.data type");
_Static_assert(SAME_TYPE(((V4l2Buffer *)0)->timestamp, MonotonicTime), "V4l2Buffer.timestamp type");

_Static_assert(sizeof(V4l2DeviceHandle) == sizeof(void *), "V4l2DeviceHandle must be a pointer");
_Static_assert(sizeof(V4l2StreamConfigList) == 16, "V4l2StreamConfigList ABI mismatch");
