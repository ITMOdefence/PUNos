/*
 * A fake libv4l2 for the camlib runtime test.
 *
 * The real library talks to a V4L2 driver through ioctl() and MMAP, so the
 * paths that matter here - a frame arriving, the device going away, buffers
 * being released - need a /dev/video* node that a build host is not
 * guaranteed to have. This file provides the same public functions without a
 * device, so those paths run everywhere.
 *
 * It is linked before libpunos_v4l2.a, which is therefore never pulled in.
 * What it verifies is camlib's own logic: state transitions, frame ownership
 * and error propagation. It says nothing about the real V4L2 ABI, which is
 * libv4l2's own test's business.
 */

#include <special/camera.h>

#include <punos/syscall/constants.h>
#include <punos/syscall/syscall.h>
#include <sys/memory.h>

/* FourCC of the one format this fake reports. */
#define FAKE_FOURCC_YUYV ((V4l2PixelFormat)0x56595559u)

#define FAKE_BUFFER_COUNT ((usize)V4L2_BUFFER_COUNT)
#define FAKE_FRAME_BYTES ((usize)4096)
#define FAKE_FD ((i32)4242)

struct FakeV4l2Device {
    MemoryBlock block;
    bool configured;
    bool streaming;
    usize buffer_count;
    usize outstanding_count;

    /* What the next v4l2_dequeue_buffer() reports. */
    bool device_lost;
    bool frame_ready;
    usize next_index;
    u64 next_sequence;

    /* Stands in for the MMAP buffers the driver would map. */
    MemoryBlock frame_block;

    /* How often v4l2_start_capture() was asked to start a device that has no
     * buffers. camlib must never do that: the old code left the camera in
     * Configured after a stop, so the next start() reached this function and
     * was refused here instead of being caught one layer up. */
    usize start_refusals;
};

static struct FakeV4l2Device *the_device;

/* Survives the device so a test can read it after close(). */
static usize start_refusals_total;
static usize next_config_count = 1;
static bool unsupported_configs;
static bool close_rejected;
static bool stop_failed;
static bool queue_failed;
static bool fractional_rate;
static bool mixed_rates;

void fake_v4l2_fail_stop(void) { stop_failed = true; }
void fake_v4l2_fail_queue(void) { queue_failed = true; }
void fake_v4l2_fractional_rate(bool value) { fractional_rate = value; }
void fake_v4l2_mixed_rates(bool value) { mixed_rates = value; }

void fake_v4l2_configurations(usize count, bool unsupported)
{
    next_config_count = count;
    unsupported_configs = unsupported;
}

void fake_v4l2_reject_close(void) { close_rejected = true; }

/* --- test control surface, used by the runtime test ---------------------- */

void fake_v4l2_forget(void)
{
    the_device = NULL;
    start_refusals_total = 0;
}

/* Makes the next dequeue report DeviceLost, once. */
void fake_v4l2_lose_device(void)
{
    if (the_device != NULL) {
        the_device->device_lost = true;
    }
}

/* Arms one frame on buffer next_index, or disarms with ready == false. */
void fake_v4l2_arm_frame(bool ready, usize index)
{
    if (the_device == NULL) {
        return;
    }
    the_device->frame_ready = ready;
    the_device->device_lost = false;
    the_device->next_index = index;
}

/* How many buffers the device currently holds, i.e. whether it is configured. */
bool fake_v4l2_is_configured(void)
{
    return the_device != NULL && the_device->configured;
}

/* How many buffers the caller still has to give back. */
usize fake_v4l2_outstanding(void)
{
    return the_device != NULL ? the_device->outstanding_count : 0;
}

/* How often a start was asked of a device with no buffers. */
usize fake_v4l2_start_refusals(void)
{
    return start_refusals_total;
}

/* --- the libv4l2 public API ----------------------------------------------- */

/* The one path this fake pretends to have a driver behind. Any other path
 * answers CORE_NOT_FOUND, the way a machine with no such node would. */
static bool path_is_known_device(const String *path)
{
    static const char known[] = "/dev/video0";
    usize length = sizeof(known) - 1;
    usize index;

    if (path->length != length) {
        return false;
    }
    for (index = 0; index < length; ++index) {
        if (path->data[index] != (u8)known[index]) {
            return false;
        }
    }
    return true;
}

ErrorCode v4l2_open(const V4l2DeviceConfig *config, V4l2DeviceHandle *out_device)
{
    if (config == NULL || out_device == NULL || config->path == NULL ||
        config->path->data == NULL || config->path->length == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    if (!path_is_known_device(config->path)) {
        return CORE_NOT_FOUND;
    }

    MemoryBlock block;
    ErrorCode result = memory_allocate(sizeof(struct FakeV4l2Device), &block);
    if (result != CORE_OK) {
        return result;
    }

    struct FakeV4l2Device *device = (struct FakeV4l2Device *)block.data;
    device->block = block;
    device->configured = false;
    device->streaming = false;
    device->buffer_count = 0;
    device->outstanding_count = 0;
    device->device_lost = false;
    device->frame_ready = false;
    device->next_index = 0;
    device->next_sequence = 1;
    device->frame_block.data = NULL;
    device->frame_block.size = 0;
    device->start_refusals = 0;

    the_device = device;
    *out_device = (V4l2DeviceHandle)device;
    return CORE_OK;
}

ErrorCode v4l2_get_fd(V4l2DeviceHandle device, i32 *out_fd)
{
    if (device == NULL || out_fd == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_fd = FAKE_FD;
    return CORE_OK;
}

ErrorCode v4l2_get_capture_configurations(V4l2DeviceHandle device,
                                          V4l2StreamConfigList *out_configurations)
{
    if (device == NULL || out_configurations == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    MemoryBlock block;
    ErrorCode result = memory_allocate(next_config_count * sizeof(V4l2StreamConfig), &block);
    if (result != CORE_OK) {
        return result;
    }

    V4l2StreamConfig *item = (V4l2StreamConfig *)block.data;
    for (usize i = 0; i < next_config_count; ++i) {
        item[i].pixel_format = (!unsupported_configs && (i == 0 || (mixed_rates && i == 1)))
                                   ? FAKE_FOURCC_YUYV : 0;
        item[i].width = 640;
        item[i].height = 480;
        item[i].frames_per_second = (fractional_rate || mixed_rates) && i == 0 ? 0 : 30;
        item[i].max_frame_bytes = (u32)FAKE_FRAME_BYTES;
        item[i].frame_rate_numerator = (fractional_rate || mixed_rates) && i == 0 ? 30000 : 30;
        item[i].frame_rate_denominator = (fractional_rate || mixed_rates) && i == 0 ? 1001 : 1;
    }

    out_configurations->items = item;
    out_configurations->count = next_config_count;
    return CORE_OK;
}

ErrorCode v4l2_stream_config_list_destroy(V4l2StreamConfigList *configurations)
{
    if (configurations == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (configurations->items != NULL) {
        MemoryBlock block;

        block.data = (u8 *)configurations->items;
        block.size = configurations->count * sizeof(V4l2StreamConfig);
        (void)memory_free(&block);
    }
    configurations->items = NULL;
    configurations->count = 0;
    return CORE_OK;
}

ErrorCode v4l2_configure_capture(V4l2DeviceHandle handle, const V4l2StreamConfig *requested_config,
                                 V4l2StreamConfig *out_actual_config)
{
    if (handle == NULL || requested_config == NULL || out_actual_config == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    struct FakeV4l2Device *device = (struct FakeV4l2Device *)handle;

    if (device->outstanding_count != 0) {
        return CORE_INVALID_STATE;
    }

    /* Mirrors the real library: STREAMOFF first, buffers are replaced. */
    if (device->frame_block.data != NULL) {
        MemoryBlock block = device->frame_block;

        (void)memory_free(&block);
        device->frame_block.data = NULL;
        device->frame_block.size = 0;
    }
    device->buffer_count = 0;

    MemoryBlock block;
    ErrorCode result = memory_allocate(FAKE_FRAME_BYTES, &block);
    if (result != CORE_OK) {
        return result;
    }
    device->frame_block = block;
    device->buffer_count = FAKE_BUFFER_COUNT;
    device->configured = true;

    out_actual_config->pixel_format = requested_config->pixel_format;
    out_actual_config->width = requested_config->width;
    out_actual_config->height = requested_config->height;
    out_actual_config->frames_per_second = fractional_rate ? 0 :
                                           requested_config->frames_per_second;
    out_actual_config->max_frame_bytes = (u32)FAKE_FRAME_BYTES;
    out_actual_config->frame_rate_numerator = fractional_rate ? 30000 : 30;
    out_actual_config->frame_rate_denominator = fractional_rate ? 1001 : 1;
    return CORE_OK;
}

ErrorCode v4l2_start_capture(V4l2DeviceHandle handle)
{
    if (handle == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (!((struct FakeV4l2Device *)handle)->configured) {
        ((struct FakeV4l2Device *)handle)->start_refusals += 1;
        start_refusals_total += 1;
        return CORE_INVALID_STATE;
    }
    ((struct FakeV4l2Device *)handle)->streaming = true;
    return CORE_OK;
}

ErrorCode v4l2_dequeue_buffer(V4l2DeviceHandle handle, V4l2DequeueStatus *out_status,
                              V4l2Buffer *out_buffer)
{
    if (handle == NULL || out_status == NULL || out_buffer == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    struct FakeV4l2Device *device = (struct FakeV4l2Device *)handle;

    if (device->device_lost) {
        /* The fixed libv4l2 forgets the buffers the driver took with it. */
        device->device_lost = false;
        device->streaming = false;
        device->outstanding_count = 0;
        *out_status = V4L2_DEQUEUE_STATUS_DEVICE_LOST;
        return CORE_OK;
    }

    if (!device->frame_ready) {
        *out_status = V4L2_DEQUEUE_STATUS_NO_BUFFER;
        return CORE_OK;
    }

    device->frame_ready = false;
    if (device->next_index >= device->buffer_count) {
        return CORE_IO_ERROR;
    }

    out_buffer->index = (u32)device->next_index;
    out_buffer->data.data = device->frame_block.data;
    out_buffer->data.size = device->frame_block.size;
    out_buffer->timestamp = device->next_sequence;
    out_buffer->sequence = device->next_sequence;
    device->next_sequence += 1;
    device->outstanding_count += 1;
    *out_status = V4L2_DEQUEUE_STATUS_BUFFER_READY;
    return CORE_OK;
}

ErrorCode v4l2_queue_buffer(V4l2DeviceHandle handle, V4l2Buffer *buffer)
{
    if (handle == NULL || buffer == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    struct FakeV4l2Device *device = (struct FakeV4l2Device *)handle;

    if (queue_failed) {
        queue_failed = false;
        return CORE_IO_ERROR;
    }

    if (buffer->index >= device->buffer_count) {
        return CORE_INVALID_ARGUMENT;
    }
    if (device->outstanding_count > 0) {
        device->outstanding_count -= 1;
    }
    buffer->index = 0;
    buffer->data.data = NULL;
    buffer->data.size = 0;
    buffer->timestamp = 0;
    buffer->sequence = 0;
    return CORE_OK;
}

ErrorCode v4l2_stop_capture(V4l2DeviceHandle handle)
{
    if (handle == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    struct FakeV4l2Device *device = (struct FakeV4l2Device *)handle;

    if (device->outstanding_count != 0) {
        return CORE_INVALID_STATE;
    }

    device->streaming = false;
    if (device->frame_block.data != NULL) {
        MemoryBlock block = device->frame_block;

        (void)memory_free(&block);
        device->frame_block.data = NULL;
        device->frame_block.size = 0;
    }
    device->buffer_count = 0;
    device->configured = false;
    if (stop_failed) {
        stop_failed = false;
        return CORE_IO_ERROR;
    }
    return CORE_OK;
}

ErrorCode v4l2_close(V4l2DeviceHandle *device)
{
    if (device == NULL || *device == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    struct FakeV4l2Device *target = (struct FakeV4l2Device *)*device;
    if (close_rejected) {
        close_rejected = false;
        return CORE_INVALID_STATE;
    }
    if (target->outstanding_count != 0) {
        return CORE_INVALID_STATE;
    }

    if (target->frame_block.data != NULL) {
        MemoryBlock frame = target->frame_block;

        (void)memory_free(&frame);
    }
    {
        MemoryBlock block = target->block;

        (void)memory_free(&block);
    }
    if (the_device == target) { the_device = NULL; }
    *device = NULL;
    return CORE_OK;
}

ErrorCode v4l2_abort(V4l2DeviceHandle *device)
{
    if (device == NULL || *device == NULL) return CORE_INVALID_ARGUMENT;
    struct FakeV4l2Device *target = (struct FakeV4l2Device *)*device;
    target->outstanding_count = 0;
    close_rejected = false;
    return v4l2_close(device);
}
