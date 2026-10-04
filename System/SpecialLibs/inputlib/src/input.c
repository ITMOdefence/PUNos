#include <special/input.h>
#include <sys/input-device.h>
#include <sys/memory.h>

struct InputDeviceHandle {
    MemoryBlock block;
    InputRawDeviceHandle raw;
    InputDeviceInfo info;
    InputEvent events[INPUT_FRAME_MAX_EVENTS];
    usize count;
    bool ready, discarding, lost;
};

ErrorCode input_device_open(const InputDeviceConfig *config, InputDeviceHandle *out_device)
{
    MemoryBlock block;
    InputRawDeviceInfo info;
    u8 key_codes[INPUT_RAW_KEY_BITMAP_SIZE];
    InputDeviceHandle device;
    ErrorCode result;
    if (config == NULL || config->path == NULL || out_device == NULL)
        return CORE_INVALID_ARGUMENT;
    const char *path;
    static const char prefix[] = "/dev/input/event";
    if (string_c_str(config->path, &path) != CORE_OK || config->path->length == 0)
        return CORE_INVALID_ARGUMENT;
    if (config->path->length <= sizeof(prefix) - 1) return CORE_INVALID_PATH;
    for (usize i = 0; i < sizeof(prefix) - 1; ++i)
        if (path[i] != prefix[i]) return CORE_INVALID_PATH;
    for (usize i = sizeof(prefix) - 1; i < config->path->length; ++i)
        if (path[i] < '0' || path[i] > '9') return CORE_INVALID_PATH;
    result = memory_allocate(sizeof(*device), &block);
    if (result != CORE_OK) return result;
    device = (InputDeviceHandle)block.data;
    device->block = block;
    result = input_raw_device_open(&(InputRawDeviceConfig){config->path}, &device->raw);
    if (result == CORE_OK) result = input_raw_device_get_info(device->raw, &info);
    if (result == CORE_OK) result = input_raw_device_get_key_codes(device->raw,
                                                                 (Bytes){key_codes, sizeof(key_codes)});
    if (result != CORE_OK) {
        if (device->raw != NULL) (void)input_raw_device_close(&device->raw);
        (void)memory_free(&block);
        return result;
    }
    device->info = (InputDeviceInfo){info.bus, info.vendor, info.product, info.version, 0};
    /* Linux KEY_* ranges exclude BTN_MISC..BTN_GEAR_UP, BTN_DPAD_* and
     * BTN_TRIGGER_HAPPY*. A mouse button alone must not imply a keyboard. */
    for (usize code = 1; code < INPUT_RAW_KEY_BITMAP_SIZE * 8; ++code) {
        if ((key_codes[code / 8] & (1u << (code % 8))) == 0) continue;
        if (code < 0x100 || (code >= 0x160 && code < 0x220) ||
            (code >= 0x224 && code < 0x2c0)) device->info.classes |= INPUT_DEVICE_KEYBOARD;
        if (code >= 0x110 && code <= 0x117) device->info.classes |= INPUT_DEVICE_POINTER;
    }
    if (info.capabilities & INPUT_RAW_CAP_RELATIVE) device->info.classes |= INPUT_DEVICE_POINTER;
    if (info.capabilities & INPUT_RAW_CAP_ABSOLUTE) device->info.classes |= INPUT_DEVICE_TOUCH;
    *out_device = device;
    return CORE_OK;
}

ErrorCode input_device_get_fd(InputDeviceHandle device, i32 *out_fd)
{
    if (out_fd == NULL) return CORE_INVALID_ARGUMENT;
    if (device == NULL) return CORE_INVALID_STATE;
    return input_raw_device_get_fd(device->raw, out_fd);
}

ErrorCode input_device_get_info(InputDeviceHandle device, InputDeviceInfo *out_info)
{
    if (out_info == NULL) return CORE_INVALID_ARGUMENT;
    if (device == NULL) return CORE_INVALID_STATE;
    *out_info = device->info;
    return CORE_OK;
}

static InputEvent normalize(InputRawEvent raw)
{
    InputEvent event = {INPUT_EVENT_KIND_RAW, raw.code, raw.value};
    switch (raw.type) {
    case INPUT_RAW_EVENT_KEY: event.kind = INPUT_EVENT_KIND_KEY; break;
    case INPUT_RAW_EVENT_REL: event.kind = INPUT_EVENT_KIND_RELATIVE_AXIS; break;
    case INPUT_RAW_EVENT_ABS: event.kind = INPUT_EVENT_KIND_ABSOLUTE_AXIS; break;
    case INPUT_RAW_EVENT_SYN:
        if (raw.code == INPUT_RAW_SYN_REPORT) {
            event.kind = INPUT_EVENT_KIND_SYNC_REPORT;
            event.value = 0;
        }
        break;
    }
    return event;
}

ErrorCode input_device_read_frame(InputDeviceHandle device, InputReadStatus *out_status,
                                 InputFrame *out_frame)
{
    ErrorCode result;
    MemoryBlock frame;
    if (out_status == NULL || out_frame == NULL) return CORE_INVALID_ARGUMENT;
    if (device == NULL) return CORE_INVALID_STATE;
    while (!device->ready && !device->lost) {
        InputRawEvent raw;
        InputRawReadStatus status;
        usize count;
        result = input_raw_device_read(device->raw, &raw, 1, &status, &count);
        if (result != CORE_OK) return result;
        if (status == INPUT_RAW_READ_STATUS_NO_EVENTS) {
            *out_status = INPUT_READ_STATUS_NO_FRAME;
            return CORE_OK;
        }
        if (status == INPUT_RAW_READ_STATUS_DEVICE_LOST) {
            device->lost = true;
            device->count = 0;
            break;
        }
        if (count != 1) return CORE_IO_ERROR;
        if (device->discarding) {
            if (raw.type == INPUT_RAW_EVENT_SYN && raw.code == INPUT_RAW_SYN_REPORT)
                device->discarding = false;
            continue;
        }
        if ((raw.type == INPUT_RAW_EVENT_SYN && raw.code == INPUT_RAW_SYN_DROPPED) ||
            device->count == INPUT_FRAME_MAX_EVENTS) {
            device->events[0] = (InputEvent){INPUT_EVENT_KIND_SYNC_LOST, INPUT_RAW_SYN_DROPPED, 0};
            device->count = 1;
            device->ready = true;
            device->discarding = !(raw.type == INPUT_RAW_EVENT_SYN && raw.code == INPUT_RAW_SYN_REPORT);
        } else {
            device->events[device->count++] = normalize(raw);
            device->ready = raw.type == INPUT_RAW_EVENT_SYN && raw.code == INPUT_RAW_SYN_REPORT;
        }
    }
    if (device->lost) {
        *out_status = INPUT_READ_STATUS_DEVICE_LOST;
        return CORE_OK;
    }
    /* Preserve a completed frame if allocation fails: the next call can retry. */
    result = memory_allocate(device->count * sizeof(InputEvent), &frame);
    if (result != CORE_OK) return result;
    (void)memory_copy((Bytes){frame.data, frame.size},
                      (Bytes){(u8 *)device->events, frame.size});
    *out_frame = (InputFrame){(InputEvent *)frame.data, device->count};
    *out_status = INPUT_READ_STATUS_FRAME_READY;
    device->ready = false;
    device->count = 0;
    return CORE_OK;
}

ErrorCode input_frame_destroy(InputFrame *frame)
{
    ErrorCode result;
    MemoryBlock block;
    if (frame == NULL || frame->count > INPUT_FRAME_MAX_EVENTS ||
        (frame->events == NULL) != (frame->count == 0)) return CORE_INVALID_ARGUMENT;
    block = (MemoryBlock){(u8 *)frame->events, frame->count * sizeof(InputEvent)};
    result = memory_free(&block);
    if (result == CORE_OK) *frame = (InputFrame){NULL, 0};
    return result;
}

ErrorCode input_device_close(InputDeviceHandle *device)
{
    MemoryBlock block;
    ErrorCode result;
    if (device == NULL) return CORE_INVALID_ARGUMENT;
    if (*device == NULL) return CORE_INVALID_STATE;
    block = (*device)->block;
    result = input_raw_device_close(&(*device)->raw);
    (void)memory_free(&block);
    *device = NULL;
    return result;
}
