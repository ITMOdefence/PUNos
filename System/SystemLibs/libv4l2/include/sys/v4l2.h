#ifndef PUNOS_SYS_V4L2_H
#define PUNOS_SYS_V4L2_H

/*
 * libv4l2 (sys/v4l2) - Linux V4L2 video capture with streaming I/O and
 * MMAP buffers.
 *
 * Specification: DevDoc/SystemLibs/libv4l2.md
 *
 * Encapsulates the V4L2 ABI by hand: PUNos is freestanding and cannot
 * include the host's <linux/videodev2.h>. Only video capture with
 * MMAP-buffered streaming I/O is supported; video output, VBI/metadata
 * capture, read/write I/O and multi-planar formats are not. The only
 * syscall wrappers used are open(), close(), ioctl(), mmap() and munmap().
 * All operations return ErrorCode; see <sys/core.h> for the conventions.
 * After DeviceLost, only v4l2_close() is allowed. Capture configuration and
 * enumeration are refused while streaming; starting twice is refused too.
 */

#include <sys/core.h>
#include <sys/string.h>
#include <sys/time.h>

/* Opaque handle of an open V4L2 capture device. Closed exactly once with
 * v4l2_close(). */
typedef struct V4l2DeviceHandle *V4l2DeviceHandle;

typedef struct V4l2DeviceConfig {
    const String *path; /* non-empty /dev/video* path, no NUL */
} V4l2DeviceConfig;

/* FourCC reported by the driver; this library never interprets it. */
typedef u32 V4l2PixelFormat;

/*
 * 0 is valid only in a request (v4l2_configure_capture()'s requested_config,
 * where max_frame_bytes must be 0); the actual configuration returned by
 * v4l2_configure_capture() and by v4l2_get_capture_configurations() always
 * has a positive max_frame_bytes.
 */
typedef struct V4l2StreamConfig {
    V4l2PixelFormat pixel_format;
    u32 width;
    u32 height;
    u32 frames_per_second;
    u32 max_frame_bytes;
} V4l2StreamConfig;

/* Owning array released with v4l2_stream_config_list_destroy(). */
typedef struct V4l2StreamConfigList {
    V4l2StreamConfig *items;
    usize count;
} V4l2StreamConfigList;

typedef struct V4l2Buffer {
    u32 index;
    Bytes data; /* non-owning view of the MMAP buffer's captured bytes */
    MonotonicTime timestamp;
    u64 sequence;
} V4l2Buffer;

/*
 * The specification lists these values by name only, without pinning
 * numbers. This library numbers them in the order the specification's
 * table lists them, starting at 0.
 */
typedef enum V4l2DequeueStatus {
    V4L2_DEQUEUE_STATUS_BUFFER_READY = 0,
    V4L2_DEQUEUE_STATUS_NO_BUFFER = 1,
    V4L2_DEQUEUE_STATUS_DEVICE_LOST = 2
} V4l2DequeueStatus;

#define V4L2_BUFFER_COUNT ((u32)4)
#define V4L2_MIN_WIDTH ((u32)1)
#define V4L2_MIN_HEIGHT ((u32)1)
#define V4L2_MIN_FRAMES_PER_SECOND ((u32)1)

_Static_assert(sizeof(V4l2StreamConfig) == 20, "V4l2StreamConfig ABI mismatch");
_Static_assert(sizeof(V4l2Buffer) == 40, "V4l2Buffer ABI mismatch"); /* index(4)+pad(4)+Bytes(16)+timestamp(8)+sequence(8) */
_Static_assert(sizeof(V4l2DequeueStatus) == 4, "V4l2DequeueStatus ABI mismatch");

/*
 * Opens the V4L2 video capture device at config->path in non-blocking mode
 * (contract 7).
 *
 * Errors: CORE_INVALID_ARGUMENT if config, config->path or out_device is
 * invalid; codes from core_error_from_errno() if open() fails; errors of
 * libmem.
 */
ErrorCode v4l2_open(const V4l2DeviceConfig *config, V4l2DeviceHandle *out_device);

/*
 * Stores device's non-owning file descriptor in *out_fd. The caller never
 * closes it (contract 6).
 *
 * Errors: CORE_INVALID_ARGUMENT if device or out_fd is NULL.
 */
ErrorCode v4l2_get_fd(V4l2DeviceHandle device, i32 *out_fd);

/*
 * Enumerates every discrete (pixel format, size, frame rate) combination
 * the driver reports and stores an owning list in *out_configurations.
 *
 * Errors: CORE_INVALID_ARGUMENT if device or out_configurations is NULL;
 * CORE_UNSUPPORTED if the driver reports a continuous or stepwise size or
 * frame-rate range anywhere, or sizeimage cannot be determined for some
 * combination without starting capture (contract 12); codes from
 * core_error_from_errno() for an unexpected ioctl() failure; errors of
 * libmem.
 */
ErrorCode v4l2_get_capture_configurations(V4l2DeviceHandle device,
                                          V4l2StreamConfigList *out_configurations);

/*
 * Releases configurations through libmem and sets it to { NULL, 0 }.
 *
 * Errors: CORE_INVALID_ARGUMENT if configurations is NULL; errors of libmem.
 */
ErrorCode v4l2_stream_config_list_destroy(V4l2StreamConfigList *configurations);

/*
 * Sets the video capture format and frame rate from requested_config
 * (max_frame_bytes must be 0), allocates and MMAPs the driver's capture
 * buffers, and stores the configuration the driver actually applied in
 * *out_actual_config, whose max_frame_bytes is the size of one MMAP buffer.
 * Replaces any buffers from an earlier v4l2_configure_capture() on the same
 * device.
 *
 * Errors: CORE_INVALID_ARGUMENT if device, requested_config or
 * out_actual_config is invalid, or requested_config's fields are out of
 * range (contract 11); CORE_INVALID_STATE if a V4l2Buffer from this device
 * is still outstanding (contract 9); codes from core_error_from_errno() for
 * an unexpected ioctl()/mmap() failure; errors of libmem.
 */
ErrorCode v4l2_configure_capture(V4l2DeviceHandle device, const V4l2StreamConfig *requested_config,
                                 V4l2StreamConfig *out_actual_config);

/*
 * Queues every configured MMAP buffer and starts streaming.
 *
 * Errors: CORE_INVALID_ARGUMENT if device is NULL; CORE_INVALID_STATE if
 * device has not been configured with v4l2_configure_capture(); codes from
 * core_error_from_errno() for an unexpected ioctl() failure.
 */
ErrorCode v4l2_start_capture(V4l2DeviceHandle device);

/*
 * Tries once, without waiting, to dequeue one ready buffer. Stores
 * BufferReady (and the buffer, in *out_buffer) if one was ready, NoBuffer
 * if the driver has none ready yet, or DeviceLost if the device is gone;
 * *out_buffer is defined only when *out_status is BufferReady.
 *
 * Errors: CORE_INVALID_ARGUMENT if device, out_status or out_buffer is
 * NULL; codes from core_error_from_errno() for an unexpected ioctl()
 * failure.
 */
ErrorCode v4l2_dequeue_buffer(V4l2DeviceHandle device, V4l2DequeueStatus *out_status,
                              V4l2Buffer *out_buffer);

/*
 * Returns a previously dequeued buffer to the driver's queue and sets
 * *buffer to an invalid, empty V4l2Buffer (its data no longer usable).
 *
 * Errors: CORE_INVALID_ARGUMENT if device or buffer is NULL, or
 * buffer->index is out of range, or its fields do not match an outstanding
 * buffer issued by this device; CORE_INVALID_STATE if not streaming; codes from
 * core_error_from_errno() for an unexpected ioctl() failure.
 */
ErrorCode v4l2_queue_buffer(V4l2DeviceHandle device, V4l2Buffer *buffer);

/*
 * Stops streaming and releases the MMAP buffers.
 * A STREAMOFF failure retains the mappings because DMA may still be active.
 * A subsequent cleanup failure may consume them; close the device on error.
 *
 * Errors: CORE_INVALID_ARGUMENT if device is NULL; CORE_INVALID_STATE if a
 * V4l2Buffer from this device is still outstanding (contract 9); codes
 * from core_error_from_errno() for an unexpected ioctl()/munmap() failure.
 */
ErrorCode v4l2_stop_capture(V4l2DeviceHandle device);

/*
 * Closes device: closes the file descriptor to stop DMA, unmaps MMAP buffers,
 * releases internal state and sets *device to NULL.
 *
 * Errors: CORE_INVALID_ARGUMENT if device or *device is NULL;
 * CORE_INVALID_STATE if a V4l2Buffer from this device is still outstanding
 * (contract 9); codes from core_error_from_errno() for an unexpected
 * close()/munmap() failure (the handle is still released).
 */
ErrorCode v4l2_close(V4l2DeviceHandle *device);

#endif
