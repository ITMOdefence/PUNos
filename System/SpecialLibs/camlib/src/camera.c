#include <special/camera.h>

#include <punos/syscall/constants.h>
#include <punos/syscall/syscall.h>
#include <sys/memory.h>
#include <sys/string.h>
#include <sys/v4l2.h>

/* Internal state; the specification names the four states but does not expose
 * this enumeration. */
typedef enum CameraState {
    CAMERA_STATE_OPEN = 0,
    CAMERA_STATE_CONFIGURED = 1,
    CAMERA_STATE_STREAMING = 2,
    CAMERA_STATE_DEVICE_LOST = 3
} CameraState;

struct CameraHandle {
    V4l2DeviceHandle v4l2_device;
    CameraState state;
    usize outstanding_frames;
    usize configured_frames; /* MMAP buffers held by the device */
    /* Keep both the issued set and the identity of each issued frame. An id
     * alone cannot distinguish another camera or an earlier use of a buffer. */
    u64 issued_buffers;
    V4l2Buffer issued_frames[V4L2_BUFFER_COUNT];
};

static V4l2PixelFormat camera_fourcc(CameraPixelFormat format)
{
    switch (format) {
    case CAMERA_PIXEL_FORMAT_YUYV:
        return (V4l2PixelFormat)0x56595559u; /* 'Y' 'U' 'Y' 'V' */
    case CAMERA_PIXEL_FORMAT_MJPEG:
        return (V4l2PixelFormat)0x47504a4du; /* 'M' 'J' 'P' 'G' */
    case CAMERA_PIXEL_FORMAT_NV12:
        return (V4l2PixelFormat)0x3231564eu; /* 'N' 'V' '1' '2' */
    default:
        return 0;
    }
}

static bool camera_format_from_fourcc(V4l2PixelFormat fourcc, CameraPixelFormat *out_format)
{
    switch (fourcc) {
    case 0x56595559u:
        *out_format = CAMERA_PIXEL_FORMAT_YUYV;
        return true;
    case 0x47504a4du:
        *out_format = CAMERA_PIXEL_FORMAT_MJPEG;
        return true;
    case 0x3231564eu:
        *out_format = CAMERA_PIXEL_FORMAT_NV12;
        return true;
    default:
        return false;
    }
}

/* Only /dev/video* is a V4L2 capture device; anything else would reach open()
 * and fail there with a code this contract does not promise. */
#define CAMERA_DEVICE_PATH_PREFIX "/dev/video"

static ErrorCode camera_validate_path(const String *path)
{
    usize prefix_length = sizeof(CAMERA_DEVICE_PATH_PREFIX) - 1;
    usize index;

    if (path == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (path->data == NULL || path->length == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    if (path->length <= prefix_length) {
        return CORE_INVALID_PATH;
    }
    for (index = 0; index < prefix_length; ++index) {
        if (path->data[index] != CAMERA_DEVICE_PATH_PREFIX[index]) {
            return CORE_INVALID_PATH;
        }
    }
    for (index = prefix_length; index < path->length; ++index) {
        if (path->data[index] < (u8)'0' || path->data[index] > (u8)'9') {
            return CORE_INVALID_PATH;
        }
    }
    return CORE_OK;
}

ErrorCode camera_open(const CameraConfig *config, CameraHandle *out_camera)
{
    if (out_camera == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    {
        ErrorCode validation = camera_validate_path(config != NULL ? config->path : NULL);

        if (validation != CORE_OK) {
            return validation;
        }
    }

    MemoryBlock block;
    ErrorCode result = memory_allocate(sizeof(struct CameraHandle), &block);
    if (result != CORE_OK) {
        return result;
    }

    CameraHandle camera = (CameraHandle)block.data;
    camera->v4l2_device = NULL;
    camera->state = CAMERA_STATE_OPEN;
    camera->outstanding_frames = 0;
    camera->configured_frames = 0;
    camera->issued_buffers = 0;

    V4l2DeviceConfig v4l2_config;
    v4l2_config.path = config->path;

    result = v4l2_open(&v4l2_config, &camera->v4l2_device);
    if (result != CORE_OK) {
        memory_free(&block);
        return result;
    }

    *out_camera = camera;
    return CORE_OK;
}

ErrorCode camera_get_fd(CameraHandle camera, i32 *out_fd)
{
    if (camera == NULL || out_fd == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (camera->v4l2_device == NULL || camera->state == CAMERA_STATE_DEVICE_LOST) {
        return CORE_INVALID_STATE;
    }
    return v4l2_get_fd(camera->v4l2_device, out_fd);
}

ErrorCode camera_get_configurations(CameraHandle camera, CameraConfigurationList *out_configurations)
{
    if (camera == NULL || out_configurations == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (camera->v4l2_device == NULL ||
        (camera->state != CAMERA_STATE_OPEN && camera->state != CAMERA_STATE_CONFIGURED)) {
        return CORE_INVALID_STATE;
    }

    V4l2StreamConfigList v4l2_list;
    ErrorCode result = v4l2_get_capture_configurations(camera->v4l2_device, &v4l2_list);
    if (result != CORE_OK) {
        return result;
    }

    out_configurations->items = NULL;
    out_configurations->count = 0;

    if (v4l2_list.count == 0) {
        (void)v4l2_stream_config_list_destroy(&v4l2_list);
        return CORE_OK;
    }

    usize count = 0;
    for (usize index = 0; index < v4l2_list.count; ++index) {
        CameraPixelFormat format;
        if (camera_format_from_fourcc(v4l2_list.items[index].pixel_format, &format)) {
            count += 1;
        }
    }
    if (count == 0) {
        (void)v4l2_stream_config_list_destroy(&v4l2_list);
        return CORE_UNSUPPORTED;
    }

    usize allocation_size;
    if (core_size_multiply(count, sizeof(CameraStreamConfig), &allocation_size) != CORE_OK) {
        (void)v4l2_stream_config_list_destroy(&v4l2_list);
        return CORE_OVERFLOW;
    }

    MemoryBlock block;
    result = memory_allocate(allocation_size, &block);
    if (result != CORE_OK) {
        (void)v4l2_stream_config_list_destroy(&v4l2_list);
        return result;
    }

    CameraStreamConfig *items = (CameraStreamConfig *)block.data;
    count = 0;

    for (usize index = 0; index < v4l2_list.count; ++index) {
        CameraPixelFormat format;

        /* Formats outside the supported set are skipped, not an error: a
         * device may well offer more than this version handles. */
        if (!camera_format_from_fourcc(v4l2_list.items[index].pixel_format, &format)) {
            continue;
        }

        items[count].format = format;
        items[count].width = v4l2_list.items[index].width;
        items[count].height = v4l2_list.items[index].height;
        items[count].frames_per_second = v4l2_list.items[index].frames_per_second;
        items[count].max_frame_bytes = v4l2_list.items[index].max_frame_bytes;
        count += 1;
    }

    (void)v4l2_stream_config_list_destroy(&v4l2_list);

    out_configurations->items = items;
    out_configurations->count = count;
    return CORE_OK;
}

ErrorCode camera_configuration_list_destroy(CameraConfigurationList *configurations)
{
    if (configurations == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (configurations->items == NULL && configurations->count != 0) {
        return CORE_INVALID_ARGUMENT;
    }

    MemoryBlock block;
    block.data = (u8 *)configurations->items;
    if (core_size_multiply(configurations->count, sizeof(CameraStreamConfig), &block.size) != CORE_OK) {
        return CORE_OVERFLOW;
    }
    ErrorCode result = memory_free(&block);
    if (result != CORE_OK) {
        return result;
    }

    configurations->items = NULL;
    configurations->count = 0;
    return CORE_OK;
}

ErrorCode camera_configure(CameraHandle camera, const CameraStreamConfig *requested_config,
                           CameraStreamConfig *out_actual_config)
{
    if (camera == NULL || requested_config == NULL || out_actual_config == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (camera->v4l2_device == NULL) {
        return CORE_INVALID_STATE;
    }
    if (camera->state != CAMERA_STATE_OPEN && camera->state != CAMERA_STATE_CONFIGURED) {
        /* The state table allows configure() only in Open and Configured.
         * Reconfiguring while Streaming would drop the MMAP buffers the
         * driver is still filling. */
        return CORE_INVALID_STATE;
    }
    if (camera->outstanding_frames > 0) {
        return CORE_INVALID_STATE;
    }
    if (requested_config->format < CAMERA_PIXEL_FORMAT_YUYV ||
        requested_config->format > CAMERA_PIXEL_FORMAT_NV12) {
        return CORE_INVALID_ARGUMENT;
    }
    if (requested_config->width < CAMERA_MIN_WIDTH ||
        requested_config->height < CAMERA_MIN_HEIGHT) {
        return CORE_INVALID_ARGUMENT;
    }
    if (requested_config->frames_per_second < CAMERA_MIN_FRAMES_PER_SECOND) {
        return CORE_INVALID_ARGUMENT;
    }
    if (requested_config->max_frame_bytes != 0) {
        return CORE_INVALID_ARGUMENT;
    }

    V4l2StreamConfig requested;
    requested.pixel_format = camera_fourcc(requested_config->format);
    requested.width = requested_config->width;
    requested.height = requested_config->height;
    requested.frames_per_second = requested_config->frames_per_second;
    requested.max_frame_bytes = 0;

    V4l2StreamConfig actual;
    ErrorCode result = v4l2_configure_capture(camera->v4l2_device, &requested, &actual);
    if (result != CORE_OK) {
        /* The lower layer may have already released the old configuration. */
        camera->state = CAMERA_STATE_OPEN;
        camera->configured_frames = 0;
        return result;
    }

    CameraPixelFormat format;
    if (!camera_format_from_fourcc(actual.pixel_format, &format)) {
        camera->state = CAMERA_STATE_OPEN;
        camera->configured_frames = 0;
        return CORE_UNSUPPORTED;
    }

    out_actual_config->format = format;
    out_actual_config->width = actual.width;
    out_actual_config->height = actual.height;
    out_actual_config->frames_per_second = actual.frames_per_second;
    out_actual_config->max_frame_bytes = actual.max_frame_bytes;

    camera->state = CAMERA_STATE_CONFIGURED;
    camera->configured_frames = (usize)V4L2_BUFFER_COUNT;
    return CORE_OK;
}

ErrorCode camera_start(CameraHandle camera)
{
    if (camera == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (camera->v4l2_device == NULL) {
        return CORE_INVALID_STATE;
    }
    if (camera->state != CAMERA_STATE_CONFIGURED) {
        return CORE_INVALID_STATE;
    }

    ErrorCode result = v4l2_start_capture(camera->v4l2_device);
    if (result != CORE_OK) {
        return result;
    }

    camera->state = CAMERA_STATE_STREAMING;
    return CORE_OK;
}

ErrorCode camera_dequeue_frame(CameraHandle camera, CameraReadStatus *out_status, CameraFrame *out_frame)
{
    if (camera == NULL || out_status == NULL || out_frame == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (camera->v4l2_device == NULL) {
        return CORE_INVALID_STATE;
    }
    if (camera->state != CAMERA_STATE_STREAMING) {
        return CORE_INVALID_STATE;
    }

    V4l2DequeueStatus v4l2_status = V4L2_DEQUEUE_STATUS_NO_BUFFER;
    V4l2Buffer buffer;

    ErrorCode result = v4l2_dequeue_buffer(camera->v4l2_device, &v4l2_status, &buffer);
    if (result != CORE_OK) {
        return result;
    }

    switch (v4l2_status) {
    case V4L2_DEQUEUE_STATUS_BUFFER_READY:
        if (buffer.index >= camera->configured_frames || buffer.index >= V4L2_BUFFER_COUNT ||
            (camera->issued_buffers & (((u64)1) << buffer.index)) != 0) {
            /* The driver named a buffer this camera never configured. */
            return CORE_IO_ERROR;
        }
        out_frame->buffer_id = buffer.index;
        out_frame->data = buffer.data;
        out_frame->timestamp = buffer.timestamp;
        out_frame->sequence = buffer.sequence;
        camera->outstanding_frames += 1;
        camera->issued_buffers |= ((u64)1) << buffer.index;
        camera->issued_frames[buffer.index] = buffer;
        *out_status = CAMERA_READ_STATUS_FRAME_READY;
        return CORE_OK;
    case V4L2_DEQUEUE_STATUS_NO_BUFFER:
        *out_status = CAMERA_READ_STATUS_NO_FRAME;
        return CORE_OK;
    case V4L2_DEQUEUE_STATUS_DEVICE_LOST:
        camera->state = CAMERA_STATE_DEVICE_LOST;
        /* Every frame still held by the caller is now meaningless. */
        camera->outstanding_frames = 0;
        camera->issued_buffers = 0;
        *out_status = CAMERA_READ_STATUS_DEVICE_LOST;
        return CORE_OK;
    default:
        return CORE_IO_ERROR;
    }
}

ErrorCode camera_frame_release(CameraHandle camera, CameraFrame *frame)
{
    if (camera == NULL || frame == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (camera->v4l2_device == NULL) {
        return CORE_INVALID_STATE;
    }
    if (camera->state != CAMERA_STATE_STREAMING) {
        return CORE_INVALID_STATE;
    }
    if (camera->outstanding_frames == 0) {
        return CORE_INVALID_STATE;
    }
    if (frame->buffer_id >= camera->configured_frames) {
        return CORE_INVALID_ARGUMENT;
    }
    if ((camera->issued_buffers & (((u64)1) << frame->buffer_id)) == 0) {
        /* This camera never handed out that buffer: the frame was already
         * released, was never dequeued, or belongs to another camera. */
        return CORE_INVALID_ARGUMENT;
    }

    const V4l2Buffer *issued = &camera->issued_frames[frame->buffer_id];
    if (frame->data.data != issued->data.data || frame->data.size != issued->data.size ||
        frame->timestamp != issued->timestamp || frame->sequence != issued->sequence) {
        return CORE_INVALID_ARGUMENT;
    }

    V4l2Buffer buffer;
    buffer.index = frame->buffer_id;
    buffer.data = frame->data;
    buffer.timestamp = frame->timestamp;
    buffer.sequence = frame->sequence;

    ErrorCode result = v4l2_queue_buffer(camera->v4l2_device, &buffer);
    if (result != CORE_OK) {
        return result;
    }

    camera->outstanding_frames -= 1;
    camera->issued_buffers &= ~(((u64)1) << frame->buffer_id);

    frame->buffer_id = 0;
    frame->data.data = NULL;
    frame->data.size = 0;
    frame->timestamp = 0;
    frame->sequence = 0;
    return CORE_OK;
}

ErrorCode camera_stop(CameraHandle camera)
{
    if (camera == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (camera->v4l2_device == NULL) {
        return CORE_INVALID_STATE;
    }
    if (camera->state != CAMERA_STATE_STREAMING) {
        return CORE_INVALID_STATE;
    }
    if (camera->outstanding_frames > 0) {
        return CORE_INVALID_STATE;
    }

    ErrorCode result = v4l2_stop_capture(camera->v4l2_device);
    if (result != CORE_OK) {
        /* Cleanup may already have consumed the mappings. There is no safe
         * capture operation to retry; retain ownership for close only. */
        camera->state = CAMERA_STATE_DEVICE_LOST;
        camera->configured_frames = 0;
        camera->issued_buffers = 0;
        return result;
    }

    /* v4l2_stop_capture() unmaps the MMAP buffers, so nothing of the previous
     * configuration is left and the camera is back to Open. Going to
     * Configured here would let camera_start() be called on a device the
     * lower layer reports as unconfigured. */
    camera->state = CAMERA_STATE_OPEN;
    camera->configured_frames = 0;
    camera->issued_buffers = 0;
    return CORE_OK;
}

ErrorCode camera_close(CameraHandle *camera)
{
    if (camera == NULL || *camera == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    CameraHandle target = *camera;
    ErrorCode result = CORE_OK;

    /* The state table allows close() in Open, Configured and DeviceLost, but
     * not while Streaming: closing under the driver's feet would release the
     * MMAP buffers without a STREAMOFF first. */
    if (target->state == CAMERA_STATE_STREAMING) {
        return CORE_INVALID_STATE;
    }
    if (target->state != CAMERA_STATE_DEVICE_LOST && target->outstanding_frames > 0) {
        return CORE_INVALID_STATE;
    }

    if (target->v4l2_device != NULL) {
        result = v4l2_close(&target->v4l2_device);
        if (target->v4l2_device != NULL) {
            /* A rejected close retains ownership: the caller must be able to
             * retry rather than losing the only reference to the device. */
            return result != CORE_OK ? result : CORE_INVALID_STATE;
        }
    }

    MemoryBlock block;
    block.data = (u8 *)target;
    block.size = sizeof(struct CameraHandle);
    (void)memory_free(&block);

    *camera = NULL;
    return result;
}
