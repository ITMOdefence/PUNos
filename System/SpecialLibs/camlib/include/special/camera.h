#ifndef PUNOS_SPECIAL_CAMERA_H
#define PUNOS_SPECIAL_CAMERA_H

/*
 * camlib (special/camera) - raw frame capture from one Linux V4L2 camera.
 *
 * Specification: DevDoc/SpecialLibs/camlib.md
 *
 * The library is a technical adapter over V4L2, used by camserv. It opens an
 * explicitly chosen /dev/video* device, configures the capture stream,
 * manages the V4L2 MMAP buffers and hands ready frames to camserv without
 * copying their pixels.
 *
 * The V4L2 ABI, ioctl() and the MMAP buffers are reached only through
 * sys/v4l2, so no V4L2 structure is spelled out here. Discovery of cameras,
 * choosing a device, permissions and reconnecting are camserv's business.
 *
 * Bytes are passed through untouched. Nothing here decodes MJPEG, converts a
 * pixel format, resizes a frame or applies a filter; camserv handles the
 * frame data in whatever format the driver produced.
 *
 * The configuration is exactly what camserv asks for. The library never goes
 * looking for a "best" resolution, frame rate or format: the values that were
 * really applied, including a positive max_frame_bytes, come back through
 * out_actual_config.
 * Fractional V4L2 rates cannot fit this v1 integer-FPS API. Enumeration skips
 * them and returns CORE_UNSUPPORTED if no representable modes remain;
 * applying a fractional rate returns CORE_UNSUPPORTED. Rates are not rounded.
 *
 * Conventions for every operation that can fail:
 *   - it returns ErrorCode, and CORE_OK is the only success value;
 *   - results are written through out_* pointers;
 *   - out_* is defined only on CORE_OK, except out_status in
 *     camera_dequeue_frame(), which is defined on CORE_OK, and out_frame,
 *     which is defined only when out_status is CAMERA_READ_STATUS_FRAME_READY;
 *   - an operation not allowed in the camera's current state returns
 *     CORE_INVALID_STATE.
 *
 * States and what each one allows:
 *
 *   Open        get_fd, get_configurations, configure, close
 *   Configured  get_fd, get_configurations, configure, start, close
 *   Streaming   get_fd, dequeue_frame, frame_release, stop, abort
 *   DeviceLost  close, abort
 *
 * Buffer ownership: a CameraFrame does not own its bytes. frame.data points
 * into a buffer the driver owns and stays valid only until
 * camera_frame_release() gives that buffer back. Every FrameReady frame must
 * be released exactly once. While the camera streams and at least one frame
 * is still out, camera_stop(), camera_configure() and camera_close() return
 * CORE_INVALID_STATE. On the way to DeviceLost every outstanding frame
 * becomes meaningless and camera_close() is allowed without releasing them.
 * If frame_release fails with a frame outstanding, camera_abort() closes the
 * fd before unmapping and invalidates every outstanding frame.
 *
 * Threading: one handle belongs to the thread that opened it. Concurrent
 * operations on one handle are not allowed; camserv serialises them with
 * libsync.
 *
 * Readiness: the device is opened O_NONBLOCK | O_CLOEXEC, so
 * camera_dequeue_frame() never waits for a frame and reports
 * CAMERA_READ_STATUS_NO_FRAME instead. camserv registers the descriptor from
 * camera_get_fd() in sys/eventloop; EVENT_READABLE means a dequeue may be
 * attempted, while EVENT_HANGUP or EVENT_ERROR mean the handle must be read
 * once more to obtain CAMERA_READ_STATUS_DEVICE_LOST and then closed.
 *
 * Only single-image capture is in scope. Video file recording, audio, camera
 * controls, several cameras at once and UI are all outside this library.
 */

#include <sys/core.h>
#include <sys/memory.h>
#include <sys/string.h>
#include <sys/time.h>
#include <sys/v4l2.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                */
/* ------------------------------------------------------------------------ */

/* Number of MMAP buffers the library asks the driver for on one stream. */
#define CAMERA_BUFFER_COUNT ((u32)4)

/* Smallest accepted frame width and height, in pixels. */
#define CAMERA_MIN_WIDTH ((u32)1)
#define CAMERA_MIN_HEIGHT ((u32)1)

/* Smallest accepted frame rate, in frames per second. */
#define CAMERA_MIN_FRAMES_PER_SECOND ((u32)1)

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/*
 * Opaque context for one open V4L2 device. Created by camera_open() and
 * released exactly once by camera_close(), which writes NULL back through
 * its pointer. Never build one by hand.
 */
typedef struct CameraHandle *CameraHandle;

/* What to open. path is not owned: it only has to stay valid for the
 * camera_open() call. */
typedef struct CameraConfig {
    const String *path;
} CameraConfig;

/*
 * Pixel layout of the stream. The specification lists these values by name
 * only, without pinning numbers. This library numbers them in the order the
 * table below lists them, starting at 0; the numbers are then part of the
 * ABI.
 *
 *   CameraPixelFormat   V4L2 FourCC   Data                     Typical use
 *   Yuyv                YUYV          uncompressed YUV 4:2:2    base USB webcam
 *   Mjpeg               MJPG          one compressed JPEG frame compact transfer
 *   Nv12                NV12          uncompressed YUV 4:2:0    video pipeline
 *
 * A device may report other FourCC codes; they are left out of
 * camera_get_configurations() rather than treated as an error.
 */
typedef enum CameraPixelFormat {
    CAMERA_PIXEL_FORMAT_YUYV = 0,
    CAMERA_PIXEL_FORMAT_MJPEG = 1,
    CAMERA_PIXEL_FORMAT_NV12 = 2
} CameraPixelFormat;

/*
 * Parameters of one stream. In a request passed to camera_configure(),
 * max_frame_bytes must be 0 - the caller does not know the buffer size yet.
 * In a configuration returned by camera_configure() or listed by
 * camera_get_configurations(), max_frame_bytes is always positive: it is the
 * size of one MMAP buffer as the driver reported it.
 */
typedef struct CameraStreamConfig {
    CameraPixelFormat format;
    u32 width;
    u32 height;
    u32 frames_per_second;
    u32 max_frame_bytes;
} CameraStreamConfig;

/*
 * Every discrete (format, size, frame rate) combination the camera supports.
 * Owning array, released with camera_configuration_list_destroy(). A camera
 * with no usable combination yields { NULL, 0 }.
 */
typedef struct CameraConfigurationList {
    CameraStreamConfig *items;
    usize count;
} CameraConfigurationList;

/*
 * One ready frame. This is a view, not a copy: data points into a buffer the
 * driver owns and is only valid until camera_frame_release() hands the buffer
 * back. All fields identify the issued frame and must be returned unchanged.
 */
typedef struct CameraFrame {
    u32 buffer_id;
    Bytes data;
    MonotonicTime timestamp;
    u64 sequence;
} CameraFrame;

/* Outcome of one camera_dequeue_frame() attempt. */
typedef enum CameraReadStatus {
    CAMERA_READ_STATUS_FRAME_READY = 0,
    CAMERA_READ_STATUS_NO_FRAME = 1,
    CAMERA_READ_STATUS_DEVICE_LOST = 2
} CameraReadStatus;

_Static_assert(sizeof(CameraPixelFormat) == 4, "CameraPixelFormat ABI mismatch");
_Static_assert(sizeof(CameraStreamConfig) == 20, "CameraStreamConfig ABI mismatch");
_Static_assert(sizeof(CameraConfigurationList) == 16, "CameraConfigurationList ABI mismatch");
_Static_assert(sizeof(CameraFrame) == 40, "CameraFrame ABI mismatch");
_Static_assert(sizeof(CameraReadStatus) == 4, "CameraReadStatus ABI mismatch");

/* ------------------------------------------------------------------------ */
/* Operations                                                               */
/* ------------------------------------------------------------------------ */

/*
 * Opens the V4L2 capture device at config->path without blocking and creates
 * a handle in state Open.
 *
 * Errors: CORE_INVALID_ARGUMENT if config, config->path, config->path->data
 * or out_camera is invalid; codes from core_error_from_errno() if the device
 * cannot be opened (CORE_NOT_FOUND if the path does not exist,
 * CORE_PERMISSION_DENIED if the process may not open it, CORE_UNSUPPORTED if
 * it is not a V4L2 capture device); errors of libmem.
 */
ErrorCode camera_open(const CameraConfig *config, CameraHandle *out_camera);

/*
 * Stores the camera's file descriptor in *out_fd. The descriptor belongs to
 * the library, not to the caller: register it in sys/eventloop, but never
 * close it.
 *
 * Errors: CORE_INVALID_ARGUMENT if camera or out_fd is NULL; CORE_INVALID_
 * STATE if the camera is in DeviceLost.
 */
ErrorCode camera_get_fd(CameraHandle camera, i32 *out_fd);

/*
 * Stores every discrete (format, size, frame rate) combination the camera
 * supports in *out_configurations, as an owning list the caller must release
 * with camera_configuration_list_destroy(). Each entry carries a positive
 * max_frame_bytes.
 *
 * Combinations in a format this version does not handle are left out. A
 * camera that reports a continuous or stepwise range instead of discrete
 * values cannot be described this way and is reported as unsupported.
 *
 * Errors: CORE_INVALID_ARGUMENT if camera or out_configurations is NULL;
 * CORE_INVALID_STATE outside Open or Configured; CORE_UNSUPPORTED if the driver
 * reports ranges rather than discrete values, or offers no combination in a
 * supported format; codes from core_error_from_errno() for an unexpected
 * ioctl() failure; errors of libmem.
 */
ErrorCode camera_get_configurations(CameraHandle camera, CameraConfigurationList *out_configurations);

/*
 * Releases a list from camera_get_configurations() and leaves it { NULL, 0 }.
 *
 * An empty { NULL, 0 } list may also be destroyed.
 * Errors: CORE_INVALID_ARGUMENT if configurations is NULL or inconsistent;
 * CORE_OVERFLOW for an invalid count; errors of libmem.
 */
ErrorCode camera_configuration_list_destroy(CameraConfigurationList *configurations);

/*
 * Applies exactly the requested configuration, allocates and MMAPs the
 * capture buffers, and stores what the driver really applied in
 * *out_actual_config - whose max_frame_bytes is positive. Buffers from an
 * earlier camera_configure() on the same camera are replaced.
 *
 * Errors: CORE_INVALID_ARGUMENT if camera, requested_config or
 * out_actual_config is NULL, if format is not a CameraPixelFormat value, or
 * if width, height or frames_per_second is below its minimum;
 * CORE_INVALID_STATE if a frame from this camera is still outstanding, or the
 * camera was lost; CORE_UNSUPPORTED if the device cannot provide the
 * requested combination; codes from core_error_from_errno() for an unexpected
 * ioctl() or mmap() failure; errors of libmem.
 */
ErrorCode camera_configure(CameraHandle camera, const CameraStreamConfig *requested_config,
                           CameraStreamConfig *out_actual_config);

/*
 * Queues every configured buffer with the driver and starts capture, moving
 * the camera to Streaming.
 *
 * Errors: CORE_INVALID_ARGUMENT if camera is NULL; CORE_INVALID_STATE if the
 * camera has not been configured, or was lost; codes from
 * core_error_from_errno() for an unexpected ioctl() failure.
 */
ErrorCode camera_start(CameraHandle camera);

/*
 * Takes one ready buffer from the driver without waiting. On
 * CAMERA_READ_STATUS_FRAME_READY, out_frame describes the buffer and must be
 * handed back exactly once with camera_frame_release(). On
 * CAMERA_READ_STATUS_NO_FRAME the driver has nothing ready and out_frame is
 * not written. On CAMERA_READ_STATUS_DEVICE_LOST capture stopped and the
 * camera may then only be closed.
 *
 * Errors: CORE_INVALID_ARGUMENT if camera, out_status or out_frame is NULL;
 * CORE_INVALID_STATE if the camera is not streaming; codes from
 * core_error_from_errno() for an unexpected ioctl() failure.
 */
ErrorCode camera_dequeue_frame(CameraHandle camera, CameraReadStatus *out_status, CameraFrame *out_frame);

/*
 * Gives a buffer back to the driver and leaves the frame invalid, so its data
 * must not be read again. A frame that was never dequeued, or one belonging
 * to another camera, is rejected.
 *
 * Errors: CORE_INVALID_ARGUMENT if camera or frame is NULL, or if
 * frame->buffer_id is out of range for this camera; CORE_INVALID_STATE if the
 * camera is not streaming, or no frame is outstanding; codes from
 * core_error_from_errno() for an unexpected ioctl() failure.
 */
ErrorCode camera_frame_release(CameraHandle camera, CameraFrame *frame);

/*
 * Stops capture and releases the MMAP buffers, returning the camera to
 * Open. Configure again before starting another capture. If stopping fails,
 * the camera becomes unusable (DeviceLost) and may only be closed.
 *
 * Errors: CORE_INVALID_ARGUMENT if camera is NULL; CORE_INVALID_STATE if the
 * camera is not streaming, or a frame is still outstanding; codes from
 * core_error_from_errno() for an unexpected ioctl() or munmap() failure.
 */
ErrorCode camera_stop(CameraHandle camera);

/*
 * Releases every internal resource, closes the device and writes NULL
 * through *camera. A lost camera may be closed with frames still
 * outstanding; a live one may not.
 *
 * Errors: CORE_INVALID_ARGUMENT if camera or *camera is NULL; CORE_INVALID_
 * STATE if a frame from this camera is still outstanding; codes from
 * core_error_from_errno() for an unexpected close() or munmap() failure (the
 * handle is still released).
 */
ErrorCode camera_close(CameraHandle *camera);

/* Emergency shutdown after frame_release fails: closes the device before
 * unmapping, invalidates all outstanding frames and consumes the handle.
 * The caller must not access any previously issued frame afterwards. */
ErrorCode camera_abort(CameraHandle *camera);

#endif
