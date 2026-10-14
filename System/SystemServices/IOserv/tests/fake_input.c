/* Linked only into the integration-test executable. */
#include <special/input.h>
#include <punos/syscall/syscall.h>

#ifndef TRACE_PATH
#define TRACE_PATH "/tmp/ioserv-fake.trace"
#endif

struct InputDeviceHandle {
    i32 fd;
    u32 scenario;
    u64 frames;
    usize pending;
};
static struct InputDeviceHandle handles[10];
static InputEvent events[INPUT_FRAME_MAX_EVENTS];
i32 fake_event_fd = -1;

static void trace(const char *text)
{
    i32 fd = open(TRACE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;
    usize size = 0; while (text[size]) ++size;
    (void)write(fd, text, size);
    (void)close(fd);
}

ErrorCode input_device_open(const InputDeviceConfig *config, InputDeviceHandle *out)
{
    if (!config || !config->path || !out) return CORE_INVALID_ARGUMENT;
    const String *path = config->path;
    if (path->length != 17 || !path->data || path->data[16] < '0' || path->data[16] > '9')
        return CORE_INVALID_PATH;
    trace("open\n");
    u32 scenario = (u32)(path->data[16] - '0');
    if (scenario == 3) return CORE_NOT_FOUND;
    struct InputDeviceHandle *handle = &handles[scenario];
    *handle = (struct InputDeviceHandle){ .scenario = scenario };
    handle->fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (handle->fd < 0) return CORE_IO_ERROR;
    if (scenario == 4) fake_event_fd = handle->fd;
    struct itimerspec timer = { 0 };
    timer.it_value.tv_nsec = 500000000;
    timer.it_interval.tv_nsec = 50000000;
    if (timerfd_settime(handle->fd, 0, &timer, NULL) != 0) {
        (void)close(handle->fd); return CORE_IO_ERROR;
    }
    *out = handle;
    return CORE_OK;
}

ErrorCode input_device_get_fd(InputDeviceHandle device, i32 *out_fd)
{
    if (!device || !out_fd) return CORE_INVALID_ARGUMENT;
    *out_fd = device->fd;
    return CORE_OK;
}

ErrorCode input_device_get_info(InputDeviceHandle device, InputDeviceInfo *out)
{
    if (!device || !out) return CORE_INVALID_ARGUMENT;
    *out = (InputDeviceInfo){ .bus = 0x1234, .vendor = 0x5678,
                              .product = (u16)device->scenario, .version = 1,
                              .classes = device->scenario == 1 ? INPUT_DEVICE_POINTER :
                                         device->scenario == 2 ? INPUT_DEVICE_TOUCH :
                                         INPUT_DEVICE_KEYBOARD };
    return CORE_OK;
}

ErrorCode input_device_read_frame(InputDeviceHandle device, InputReadStatus *status,
                                  InputFrame *frame)
{
    if (!device || !status || !frame) return CORE_INVALID_ARGUMENT;
    if (device->pending == 0) {
        u64 ticks;
        if (read(device->fd, &ticks, sizeof(ticks)) != sizeof(ticks)) {
            *status = INPUT_READ_STATUS_NO_FRAME;
            return CORE_OK;
        }
        device->pending = device->scenario == 6 ? 128 : 1;
    }
    --device->pending;
    if (device->scenario == 4 && device->frames == 2) {
        *status = INPUT_READ_STATUS_DEVICE_LOST;
        trace("lost\n");
        return CORE_OK;
    }
    *status = INPUT_READ_STATUS_FRAME_READY;
    ++device->frames;
    if (device->scenario == 5 && device->frames == 2) {
        events[0] = (InputEvent){ INPUT_EVENT_KIND_SYNC_LOST, 0, 0 };
        frame->count = 1;
        trace("sync_lost\n");
    } else {
        events[0] = (InputEvent){ device->scenario == 1 ? INPUT_EVENT_KIND_RELATIVE_AXIS :
                                   device->scenario == 2 ? INPUT_EVENT_KIND_ABSOLUTE_AXIS :
                                   INPUT_EVENT_KIND_KEY,
                                   0x123, -3 };
        events[1] = (InputEvent){ INPUT_EVENT_KIND_SYNC_REPORT, 0, 0 };
        frame->count = device->scenario == 6 ? INPUT_FRAME_MAX_EVENTS : 2;
        if (device->scenario == 6)
            for (usize i = 1; i < INPUT_FRAME_MAX_EVENTS - 1; ++i)
                events[i] = (InputEvent){ INPUT_EVENT_KIND_KEY, (u16)i, 1 };
        events[frame->count - 1] = (InputEvent){ INPUT_EVENT_KIND_SYNC_REPORT, 0, 0 };
    }
    frame->events = events;
    return CORE_OK;
}

ErrorCode input_frame_destroy(InputFrame *frame)
{
    if (!frame || !frame->events) return CORE_INVALID_ARGUMENT;
    if (frame->count < INPUT_FRAME_MAX_EVENTS) trace("destroy\n");
    frame->events = NULL; frame->count = 0;
    return CORE_OK;
}

ErrorCode input_device_close(InputDeviceHandle *device)
{
    if (!device || !*device) return CORE_INVALID_ARGUMENT;
    trace("close\n");
    (void)close((*device)->fd);
    *device = NULL;
    return CORE_OK;
}
