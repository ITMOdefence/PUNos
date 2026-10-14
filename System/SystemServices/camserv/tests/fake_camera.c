/* Linked only into the integration-test executable. */
#include <special/camera.h>
#include <special/ipc.h>
#include <punos/syscall/syscall.h>

#ifndef TRACE_PATH
#define TRACE_PATH "/tmp/camserv-fake.trace"
#endif

struct CameraHandle {
    i32 fd;
    u32 scenario;
    u64 sequence;
    u32 max_bytes;
    bool outstanding;
    bool burst_started;
};
static struct CameraHandle fake;
static u8 frame_data[IPC_MESSAGE_MAX_SIZE];
i32 fake_event_fd = -1;

static void trace(const char *text)
{
    i32 fd = open(TRACE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;
    usize size = 0; while (text[size]) ++size;
    (void)write(fd, text, size);
    (void)close(fd);
}

static void trace_path(const String *path)
{
    i32 fd = open(TRACE_PATH ".path", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;
    (void)write(fd, path->data, path->length);
    (void)close(fd);
}

ErrorCode camera_open(const CameraConfig *config, CameraHandle *out_camera)
{
    if (!config || !config->path || !out_camera) return CORE_INVALID_ARGUMENT;
    const String *path = config->path;
    if (path->length != 11 || !path->data ||
        path->data[10] < '0' || path->data[10] > '9') return CORE_INVALID_PATH;
    u32 scenario = (u32)(path->data[10] - '0');
    const char *expected_paths[] = {
        "/dev/video0", "/dev/video1", "/dev/video2", "/dev/video3", "/dev/video4",
        "/dev/video5", "/dev/video6", "/dev/video7", "/dev/video8", "/dev/video9"
    };
    for (usize i = 0; i < path->length; ++i)
        if (path->data[i] != expected_paths[scenario][i]) return CORE_INVALID_PATH;
    trace_path(path);
    trace("open\n");
    if (scenario == 1) return CORE_NOT_FOUND;
    fake = (struct CameraHandle){ .scenario = scenario, .fd = -1 };
    *out_camera = &fake;
    return CORE_OK;
}

ErrorCode camera_get_fd(CameraHandle camera, i32 *out_fd)
{
    if (!camera || !out_fd) return CORE_INVALID_ARGUMENT;
    *out_fd = camera->fd;
    return CORE_OK;
}

ErrorCode camera_configure(CameraHandle camera, const CameraStreamConfig *requested,
                           CameraStreamConfig *actual)
{
    if (!camera || !requested || !actual) return CORE_INVALID_ARGUMENT;
    trace("configure\n");
    if (camera->scenario == 3) return CORE_UNSUPPORTED;
    *actual = *requested;
    actual->max_frame_bytes = camera->scenario == 2 ? IPC_MESSAGE_MAX_SIZE :
                              camera->scenario == 7 ? 262144 :
                              camera->scenario == 9 ? IPC_MESSAGE_MAX_SIZE - 20 : 4;
    camera->max_bytes = actual->max_frame_bytes;
    return CORE_OK;
}

ErrorCode camera_start(CameraHandle camera)
{
    if (!camera) return CORE_INVALID_ARGUMENT;
    trace("start\n");
    if (camera->scenario == 4) return CORE_IO_ERROR;
    camera->fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (camera->fd < 0) return CORE_IO_ERROR;
    fake_event_fd = camera->fd;
    struct itimerspec timer = { 0 };
    timer.it_value.tv_nsec = 500000000;
    timer.it_interval.tv_nsec = camera->scenario == 8 ? 0 :
                                 camera->scenario == 7 ? 10000000 : 50000000;
    if (timerfd_settime(camera->fd, 0, &timer, NULL) != 0) return CORE_IO_ERROR;
    return CORE_OK;
}

ErrorCode camera_dequeue_frame(CameraHandle camera, CameraReadStatus *status,
                               CameraFrame *frame)
{
    if (!camera || !status || !frame) return CORE_INVALID_ARGUMENT;
    u64 ticks;
    if (camera->scenario == 8) {
        if (!camera->burst_started) {
            if (read(camera->fd, &ticks, sizeof(ticks)) != sizeof(ticks)) {
                *status = CAMERA_READ_STATUS_NO_FRAME;
                return CORE_OK;
            }
            camera->burst_started = true;
        }
    } else if (read(camera->fd, &ticks, sizeof(ticks)) != sizeof(ticks)) {
        *status = CAMERA_READ_STATUS_NO_FRAME;
        return CORE_OK;
    }
    if (camera->scenario == 8 && camera->sequence == 80) {
#ifdef FAKE_BURST_END_NO_FRAME
        *status = CAMERA_READ_STATUS_NO_FRAME;
        return CORE_OK;
#else
        *status = CAMERA_READ_STATUS_DEVICE_LOST;
        trace("lost\n");
        return CORE_OK;
#endif
    }
    if (camera->scenario == 5 && camera->sequence == 2) {
        *status = CAMERA_READ_STATUS_DEVICE_LOST;
        trace("lost\n");
        return CORE_OK;
    }
    *status = CAMERA_READ_STATUS_FRAME_READY;
    frame->buffer_id = 1;
    frame->sequence = ++camera->sequence;
    frame->timestamp = 1000 + frame->sequence;
    frame_data[0] = 0xa1; frame_data[1] = 0; frame_data[2] = 0xff; frame_data[3] = 0x5a;
    frame->data.data = frame_data;
    frame->data.size = camera->scenario == 6 ? 5 : camera->max_bytes;
    camera->outstanding = true;
    return CORE_OK;
}

ErrorCode camera_frame_release(CameraHandle camera, CameraFrame *frame)
{
    if (!camera || !frame || !camera->outstanding) return CORE_INVALID_STATE;
    if (camera->scenario != 7) trace("release\n");
    camera->outstanding = false;
    frame->data.data = NULL;
    frame->data.size = 0;
    return CORE_OK;
}

ErrorCode camera_stop(CameraHandle camera)
{
    if (!camera) return CORE_INVALID_ARGUMENT;
    trace("stop\n");
    return camera->outstanding ? CORE_INVALID_STATE : CORE_OK;
}

ErrorCode camera_close(CameraHandle *camera)
{
    if (!camera || !*camera) return CORE_INVALID_ARGUMENT;
    trace("close\n");
    if ((*camera)->fd >= 0) (void)close((*camera)->fd);
    *camera = NULL;
    return CORE_OK;
}
