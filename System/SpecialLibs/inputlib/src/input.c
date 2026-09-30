#include <special/input.h>

#include <punos/syscall/constants.h>
#include <punos/syscall/syscall.h>
#include <sys/input-device.h>
#include <sys/memory.h>
#include <sys/string.h>

struct InputDeviceHandle {
    InputRawDeviceHandle raw_device;
    InputDeviceInfo info;
    InputRawEvent raw_events[INPUT_FRAME_MAX_EVENTS];
    InputEvent events[INPUT_FRAME_MAX_EVENTS];
    usize event_count;
};

static InputEventKind input_classify(u16 type, u16 code)
{
    if (type == INPUT_RAW_EVENT_SYN) {
        if (code == INPUT_RAW_SYN_REPORT) {
            return INPUT_EVENT_KIND_SYNC_REPORT;
        }
        if (code == INPUT_RAW_SYN_DROPPED) {
            return INPUT_EVENT_KIND_SYNC_LOST;
        }
        return INPUT_EVENT_KIND_RAW;
    }
    if (type == INPUT_RAW_EVENT_KEY) {
        return INPUT_EVENT_KIND_KEY;
    }
    if (type == INPUT_RAW_EVENT_REL) {
        return INPUT_EVENT_KIND_RELATIVE_AXIS;
    }
    if (type == INPUT_RAW_EVENT_ABS) {
        return INPUT_EVENT_KIND_ABSOLUTE_AXIS;
    }
    return INPUT_EVENT_KIND_RAW;
}

/* A device's capabilities decide which INPUT_DEVICE_* class it reports; a
 * device matching none of them is INPUT_DEVICE_UNKNOWN. */
static InputDeviceClass input_classes_from_capabilities(InputRawCapabilityMask capabilities)
{
    InputDeviceClass classes = INPUT_DEVICE_UNKNOWN;

    if ((capabilities & INPUT_RAW_CAP_KEY) != 0) {
        classes |= INPUT_DEVICE_KEYBOARD;
    }
    if ((capabilities & INPUT_RAW_CAP_RELATIVE) != 0) {
        classes |= INPUT_DEVICE_POINTER;
    }
    if ((capabilities & INPUT_RAW_CAP_ABSOLUTE) != 0) {
        classes |= INPUT_DEVICE_TOUCH;
    }
    return classes;
}

static void input_reset_frame(InputDeviceHandle *device)
{
    device->event_count = 0;
}

ErrorCode input_device_open(const InputDeviceConfig *config, InputDeviceHandle **out_device)
{
    if (config == NULL || config->path == NULL || config->path->data == NULL ||
        out_device == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    MemoryBlock block;
    ErrorCode result = memory_allocate(sizeof(InputDeviceHandle), &block);
    if (result != CORE_OK) {
        return result;
    }

    InputDeviceHandle *device = (InputDeviceHandle *)block.data;
    device->raw_device = NULL;
    device->event_count = 0;

    InputRawDeviceConfig raw_config;
    raw_config.path = config->path;

    result = input_raw_device_open(&raw_config, &device->raw_device);
    if (result != CORE_OK) {
        memory_free(&block);
        return result;
    }

    InputRawDeviceInfo raw_info;
    result = input_raw_device_get_info(device->raw_device, &raw_info);
    if (result != CORE_OK) {
        (void)input_raw_device_close(&device->raw_device);
        memory_free(&block);
        return result;
    }

    device->info.bus = raw_info.bus;
    device->info.vendor = raw_info.vendor;
    device->info.product = raw_info.product;
    device->info.version = raw_info.version;
    device->info.classes = input_classes_from_capabilities(raw_info.capabilities);

    *out_device = device;
    return CORE_OK;
}

ErrorCode input_device_get_fd(InputDeviceHandle *device, i32 *out_fd)
{
    if (device == NULL || out_fd == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (device->raw_device == NULL) {
        return CORE_INVALID_STATE;
    }
    return input_raw_device_get_fd(device->raw_device, out_fd);
}

ErrorCode input_device_get_info(InputDeviceHandle *device, InputDeviceInfo *out_info)
{
    if (device == NULL || out_info == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (device->raw_device == NULL) {
        return CORE_INVALID_STATE;
    }
    *out_info = device->info;
    return CORE_OK;
}

/*
 * Copies one already collected frame into an owning array and hands it to the
 * caller. The events live in the handle, so they must be duplicated: the
 * contract gives the frame's array to the caller.
 */
static ErrorCode input_hand_frame_to_caller(InputDeviceHandle *device, InputFrame *out_frame)
{
    MemoryBlock block;
    ErrorCode result = memory_allocate(device->event_count * sizeof(InputEvent), &block);
    if (result != CORE_OK) {
        return result;
    }

    memory_copy((Bytes){ block.data, device->event_count * sizeof(InputEvent) },
                (Bytes){ (u8 *)device->events, device->event_count * sizeof(InputEvent) });

    out_frame->events = (InputEvent *)block.data;
    out_frame->count = device->event_count;

    input_reset_frame(device);
    return CORE_OK;
}

ErrorCode input_device_read_frame(InputDeviceHandle *device, InputReadStatus *out_status,
                                  InputFrame *out_frame)
{
    if (device == NULL || out_status == NULL || out_frame == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (device->raw_device == NULL) {
        return CORE_INVALID_STATE;
    }

    input_reset_frame(device);

    for (;;) {
        InputRawReadStatus raw_status = INPUT_RAW_READ_STATUS_NO_EVENTS;
        usize raw_count = 0;

        ErrorCode result = input_raw_device_read(device->raw_device, device->raw_events,
                                                 INPUT_FRAME_MAX_EVENTS, &raw_status, &raw_count);
        if (result != CORE_OK) {
            input_reset_frame(device);
            return result;
        }

        if (raw_status == INPUT_RAW_READ_STATUS_DEVICE_LOST) {
            input_reset_frame(device);
            *out_status = INPUT_READ_STATUS_DEVICE_LOST;
            return CORE_OK;
        }

        if (raw_status == INPUT_RAW_READ_STATUS_NO_EVENTS) {
            if (device->event_count == 0) {
                *out_status = INPUT_READ_STATUS_NO_FRAME;
                return CORE_OK;
            }
            break;
        }

        for (usize index = 0; index < raw_count; ++index) {
            InputRawEvent *raw = &device->raw_events[index];
            InputEventKind kind = input_classify(raw->type, raw->code);

            if (kind == INPUT_EVENT_KIND_SYNC_LOST) {
                /* Everything collected so far is unreliable: the contract asks
                 * for a frame holding only the SyncLost event. */
                device->events[0].kind = INPUT_EVENT_KIND_SYNC_LOST;
                device->events[0].code = INPUT_RAW_SYN_DROPPED;
                device->events[0].value = 0;
                device->event_count = 1;

                result = input_hand_frame_to_caller(device, out_frame);
                if (result != CORE_OK) {
                    return result;
                }
                *out_status = INPUT_READ_STATUS_FRAME_READY;
                return CORE_OK;
            }

            if (device->event_count >= INPUT_FRAME_MAX_EVENTS) {
                /* An unterminated frame this long is treated as a lost sync. */
                device->events[0].kind = INPUT_EVENT_KIND_SYNC_LOST;
                device->events[0].code = INPUT_RAW_SYN_DROPPED;
                device->events[0].value = 0;
                device->event_count = 1;

                result = input_hand_frame_to_caller(device, out_frame);
                if (result != CORE_OK) {
                    return result;
                }
                *out_status = INPUT_READ_STATUS_FRAME_READY;
                return CORE_OK;
            }

            device->events[device->event_count].kind = kind;
            device->events[device->event_count].code = raw->code;
            device->events[device->event_count].value = raw->value;
            device->event_count += 1;

            if (kind == INPUT_EVENT_KIND_SYNC_REPORT) {
                result = input_hand_frame_to_caller(device, out_frame);
                if (result != CORE_OK) {
                    return result;
                }
                *out_status = INPUT_READ_STATUS_FRAME_READY;
                return CORE_OK;
            }
        }
    }

    ErrorCode result = input_hand_frame_to_caller(device, out_frame);
    if (result != CORE_OK) {
        return result;
    }
    *out_status = INPUT_READ_STATUS_FRAME_READY;
    return CORE_OK;
}

ErrorCode input_frame_destroy(InputFrame *frame)
{
    if (frame == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (frame->count > 0) {
        MemoryBlock block;
        block.data = (u8 *)frame->events;
        block.size = frame->count * sizeof(InputEvent);
        (void)memory_free(&block);
    }

    frame->events = NULL;
    frame->count = 0;
    return CORE_OK;
}

ErrorCode input_device_close(InputDeviceHandle **device)
{
    if (device == NULL || *device == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    InputDeviceHandle *target = *device;
    ErrorCode result = CORE_OK;

    if (target->raw_device != NULL) {
        result = input_raw_device_close(&target->raw_device);
    }

    MemoryBlock block;
    block.data = (u8 *)target;
    block.size = sizeof(InputDeviceHandle);
    (void)memory_free(&block);

    *device = NULL;
    return result;
}
