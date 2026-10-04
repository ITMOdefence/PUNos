/*
 * A fake libinputdevice for the inputlib runtime test.
 *
 * The real library reads /dev/input/event* and the host is not guaranteed to
 * have a readable one, and a real device would not hand out the exact event
 * sequences these checks are about. This file provides the same public
 * functions over a queue the test fills in by hand, so the frame-boundary
 * rules can be exercised anywhere.
 *
 * Linked before libpunos_inputdevice.a, which is therefore not pulled in.
 */

#include <special/input.h>

#include <punos/syscall/constants.h>
#include <punos/syscall/syscall.h>
#include <sys/input-device.h>
#include <sys/memory.h>

#define FAKE_MAX_EVENTS ((usize)512)
#define FAKE_FD ((i32)4343)

struct FakeInputDevice {
    MemoryBlock block;
    InputRawDeviceInfo info;
    bool open;

    /* Events handed out by input_raw_device_read(), oldest first. */
    InputRawEvent queue[FAKE_MAX_EVENTS];
    usize count;
    usize pos;
    bool lost;
};

static struct FakeInputDevice *the_device;

/* Capabilities the next open() reports. inputlib reads them once, when the
 * device is opened, so a test has to choose before opening. */
static InputRawCapabilityMask next_capabilities =
    INPUT_RAW_CAP_KEY | INPUT_RAW_CAP_KEYBOARD_KEYS;

/* --- test control surface ------------------------------------------------- */

/* Forgets the device, so the next input_device_open() starts clean. */
void fake_input_forget(void)
{
    if (the_device != NULL) {
        MemoryBlock block = the_device->block;

        (void)memory_free(&block);
    }
    the_device = NULL;
}

/* Chooses the capability bits the next open() will report. */
void fake_input_set_capabilities(InputRawCapabilityMask capabilities)
{
    next_capabilities = capabilities;
}

/* Replaces the pending events with raw ones. */
void fake_input_queue(const InputRawEvent *events, usize count)
{
    if (the_device == NULL) {
        return;
    }
    the_device->count = 0;
    the_device->pos = 0;
    for (usize index = 0; index < count && index < FAKE_MAX_EVENTS; ++index) {
        the_device->queue[index] = events[index];
    }
    the_device->count = count;
}

/* Makes the next read report DeviceLost. */
void fake_input_lose_device(void)
{
    if (the_device != NULL) {
        the_device->lost = true;
    }
}

/* Events still waiting to be read. */
usize fake_input_pending(void)
{
    return the_device != NULL ? the_device->count - the_device->pos : 0;
}

/* --- the libinputdevice public API ---------------------------------------- */

/* The one path this fake pretends to have a device behind. */
static bool path_is_known_device(const String *path)
{
    static const char known[] = "/dev/input/event0";
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

ErrorCode input_raw_device_open(const InputRawDeviceConfig *config,
                                InputRawDeviceHandle *out_device)
{
    if (config == NULL || out_device == NULL || config->path == NULL ||
        config->path->data == NULL || config->path->length == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    if (!path_is_known_device(config->path)) {
        return CORE_NOT_FOUND;
    }

    MemoryBlock block;
    ErrorCode result = memory_allocate(sizeof(struct FakeInputDevice), &block);
    if (result != CORE_OK) {
        return result;
    }

    struct FakeInputDevice *device = (struct FakeInputDevice *)block.data;
    device->block = block;
    device->info.bus = 0;
    device->info.vendor = 0;
    device->info.product = 0;
    device->info.version = 0;
    device->info.capabilities = next_capabilities;
    device->open = true;
    device->count = 0;
    device->pos = 0;
    device->lost = false;

    the_device = device;
    *out_device = (InputRawDeviceHandle)device;
    return CORE_OK;
}

ErrorCode input_raw_device_get_fd(InputRawDeviceHandle device, i32 *out_fd)
{
    if (device == NULL || out_fd == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_fd = FAKE_FD;
    return CORE_OK;
}

ErrorCode input_raw_device_get_info(InputRawDeviceHandle device, InputRawDeviceInfo *out_info)
{
    if (device == NULL || out_info == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_info = ((struct FakeInputDevice *)device)->info;
    return CORE_OK;
}

ErrorCode input_raw_device_read(InputRawDeviceHandle handle, InputRawEvent *out_events,
                                usize events_capacity, InputRawReadStatus *out_status,
                                usize *out_count)
{
    if (handle == NULL || out_status == NULL || out_count == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (events_capacity != 0 && out_events == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    struct FakeInputDevice *device = (struct FakeInputDevice *)handle;

    if (device->lost) {
        device->lost = false;
        *out_status = INPUT_RAW_READ_STATUS_DEVICE_LOST;
        return CORE_OK;
    }

    usize available = device->count - device->pos;
    if (available == 0) {
        *out_status = INPUT_RAW_READ_STATUS_NO_EVENTS;
        return CORE_OK;
    }

    /* Hands out a whole batch, the way a real read() does, so a single call
     * can carry more than one SYN_REPORT. */
    usize take = available;
    if (take > events_capacity) {
        take = events_capacity;
    }
    for (usize index = 0; index < take; ++index) {
        out_events[index] = device->queue[device->pos + index];
    }
    device->pos += take;
    *out_count = take;
    *out_status = INPUT_RAW_READ_STATUS_EVENTS_READY;
    return CORE_OK;
}

ErrorCode input_raw_device_close(InputRawDeviceHandle *device)
{
    if (device == NULL || *device == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    MemoryBlock block = ((struct FakeInputDevice *)*device)->block;

    (void)memory_free(&block);
    the_device = NULL;
    *device = NULL;
    return CORE_OK;
}
