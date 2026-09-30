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

ErrorCode camera_open(const CameraConfig *config, CameraHandle **out_camera)
{
    if (config == NULL || config->path == NULL || config->path->data == NULL ||
        out_camera == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    MemoryBlock block;
    ErrorCode result = memory_allocate(sizeof(CameraHandle), &block);
    if (result != CORE_OK) {
        return result;
    }

    CameraHandle *camera = (CameraHandle *)block.data;
    camera->v4l2_device = NULL;
    camera->state = CAMERA_STATE_OPEN;
    camera->outstanding_frames = 0;
    camera->configured_frames = 0;

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

ErrorCode camera_get_fd(CameraHandle *camera, i32 *out_fd)
{
    if (camera == NULL || out_fd == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (camera->v4l2_device == NULL) {
        return CORE_INVALID_STATE;
    }
    return v4l2_get_fd(camera->v4l2_device, out_fd);
}

ErrorCode camera_get_configurations(CameraHandle *camera, CameraConfigurationList *out_configurations)
{
    if (camera == NULL || out_configurations == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (camera->v4l2_device == NULL) {
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

    MemoryBlock block;
    result = memory_allocate(v4l2_list.count * sizeof(CameraStreamConfig), &block);
    if (result != CORE_OK) {
        (void)v4l2_stream_config_list_destroy(&v4l2_list);
        return result;
    }

    CameraStreamConfig *items = (CameraStreamConfig *)block.data;
    usize count = 0;

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

    if (count == 0) {
        memory_free(&block);
        return CORE_UNSUPPORTED;
    }

    out_configurations->items = items;
    out_configurations->count = count;
    return CORE_OK;
}

ErrorCode camera_configuration_list_destroy(CameraConfigurationList *configurations)
{
    if (configurations == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (configurations->items == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    MemoryBlock block;
    block.data = (u8 *)configurations->items;
    block.size = configurations->count * sizeof(CameraStreamConfig);
    (void)memory_free(&block);

    configurations->items = NULL;
    configurations->count = 0;
    return CORE_OK;
}

ErrorCode camera_configure(CameraHandle *camera, const CameraStreamConfig *requested_config,
                           CameraStreamConfig *out_actual_config)
{
    if (camera == NULL || requested_config == NULL || out_actual_config == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (camera->v4l2_device == NULL) {
        return CORE_INVALID_STATE;
    }
    if (camera->state == CAMERA_STATE_DEVICE_LOST) {
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
        return result;
    }

    CameraPixelFormat format;
    if (!camera_format_from_fourcc(actual.pixel_format, &format)) {
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

ErrorCode camera_start(CameraHandle *camera)
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

ErrorCode camera_dequeue_frame(CameraHandle *camera, CameraReadStatus *out_status, CameraFrame *out_frame)
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
        out_frame->buffer_id = buffer.index;
        out_frame->data = buffer.data;
        out_frame->timestamp = buffer.timestamp;
        out_frame->sequence = buffer.sequence;
        camera->outstanding_frames += 1;
        *out_status = CAMERA_READ_STATUS_FRAME_READY;
        return CORE_OK;
    case V4L2_DEQUEUE_STATUS_NO_BUFFER:
        *out_status = CAMERA_READ_STATUS_NO_FRAME;
        return CORE_OK;
    case V4L2_DEQUEUE_STATUS_DEVICE_LOST:
        camera->state = CAMERA_STATE_DEVICE_LOST;
        /* Every frame still held by the caller is now meaningless. */
        camera->outstanding_frames = 0;
        *out_status = CAMERA_READ_STATUS_DEVICE_LOST;
        return CORE_OK;
    default:
        return CORE_IO_ERROR;
    }
}

ErrorCode camera_frame_release(CameraHandle *camera, CameraFrame *frame)
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

    frame->buffer_id = 0;
    frame->data.data = NULL;
    frame->data.size = 0;
    frame->timestamp = 0;
    frame->sequence = 0;
    return CORE_OK;
}

ErrorCode camera_stop(CameraHandle *camera)
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
        return result;
    }

    camera->state = CAMERA_STATE_CONFIGURED;
    camera->configured_frames = 0;
    return CORE_OK;
}

ErrorCode camera_close(CameraHandle **camera)
{
    if (camera == NULL || *camera == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    CameraHandle *target = *camera;
    ErrorCode result = CORE_OK;

    /* A lost device may be closed with frames still outstanding; a live one
     * may not (contract 11). */
    if (target->state != CAMERA_STATE_DEVICE_LOST && target->outstanding_frames > 0) {
        return CORE_INVALID_STATE;
    }

    if (target->v4l2_device != NULL) {
        result = v4l2_close(&target->v4l2_device);
    }

    MemoryBlock block;
    block.data = (u8 *)target;
    block.size = sizeof(CameraHandle);
    (void)memory_free(&block);

    *camera = NULL;
    return result;
}
