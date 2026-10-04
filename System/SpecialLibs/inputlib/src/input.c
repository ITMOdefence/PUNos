#include <special/input.h>

#include <punos/syscall/constants.h>
#include <punos/syscall/syscall.h>
#include <sys/input-device.h>
#include <sys/memory.h>
#include <sys/string.h>

struct InputDeviceHandle {
    InputRawDeviceHandle raw_device;
    InputDeviceInfo info;
    /* Raw events already read from the kernel but not yet consumed into a
     * frame. One read() can return several frames, so the tail after the
     * first SYN_REPORT has to survive until the next call. */
    InputRawEvent pending[INPUT_FRAME_MAX_EVENTS];
    usize pending_count;
    usize pending_pos;
    /* Events of the frame currently being assembled. They stay here, without
     * their SYN_REPORT, until the kernel terminates it. */
    InputEvent events[INPUT_FRAME_MAX_EVENTS];
    usize event_count;
    /* Set after SYN_DROPPED: the kernel discards everything up to the next
     * SYN_REPORT, so those events belong to no frame. */
    bool resyncing;
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
 * device matching none of them is INPUT_DEVICE_UNKNOWN.
 *
 * EV_KEY on its own means nothing: a mouse reports its buttons that way too.
 * The keyboard class needs a code below BTN_MISC, which is what separates a
 * keyboard from a pointer that only has buttons. */
static InputDeviceClass input_classes_from_capabilities(InputRawCapabilityMask capabilities)
{
    InputDeviceClass classes = INPUT_DEVICE_UNKNOWN;

    if ((capabilities & INPUT_RAW_CAP_KEY) != 0) {
        if ((capabilities & INPUT_RAW_CAP_KEYBOARD_KEYS) != 0) {
            classes |= INPUT_DEVICE_KEYBOARD;
        }
        if ((capabilities & INPUT_RAW_CAP_POINTER_BUTTONS) != 0) {
            classes |= INPUT_DEVICE_POINTER;
        }
    }
    if ((capabilities & INPUT_RAW_CAP_RELATIVE) != 0) {
        classes |= INPUT_DEVICE_POINTER;
    }
    if ((capabilities & INPUT_RAW_CAP_ABSOLUTE) != 0) {
        classes |= INPUT_DEVICE_TOUCH;
    }
    return classes;
}

static void input_reset_frame(InputDeviceHandle device)
{
    device->event_count = 0;
}

static void input_discard_all(InputDeviceHandle device)
{
    device->event_count = 0;
    device->pending_count = 0;
    device->pending_pos = 0;
    device->resyncing = false;
}

/* The library only opens /dev/input/event* : anything else would reach
 * open() and fail there with a code the contract does not promise. */
#define INPUT_DEVICE_PATH_PREFIX "/dev/input/event"

static ErrorCode input_validate_path(const String *path)
{
    usize prefix_length = sizeof(INPUT_DEVICE_PATH_PREFIX) - 1;
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
        if (path->data[index] != INPUT_DEVICE_PATH_PREFIX[index]) {
            return CORE_INVALID_PATH;
        }
    }
    /* At least one character has to follow the prefix: event0, event12, ... */
    for (index = prefix_length; index < path->length; ++index) {
        if (path->data[index] < (u8)'0' || path->data[index] > (u8)'9') {
            return CORE_INVALID_PATH;
        }
    }
    return CORE_OK;
}

ErrorCode input_device_open(const InputDeviceConfig *config, InputDeviceHandle *out_device)
{
    if (config == NULL || out_device == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    {
        ErrorCode validation = input_validate_path(config->path);

        if (validation != CORE_OK) {
            return validation;
        }
    }

    MemoryBlock block;
    ErrorCode result = memory_allocate(sizeof(struct InputDeviceHandle), &block);
    if (result != CORE_OK) {
        return result;
    }

    InputDeviceHandle device = (InputDeviceHandle)block.data;
    device->raw_device = NULL;
    device->event_count = 0;
    device->pending_count = 0;
    device->pending_pos = 0;
    device->resyncing = false;

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

ErrorCode input_device_get_fd(InputDeviceHandle device, i32 *out_fd)
{
    if (device == NULL || out_fd == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (device->raw_device == NULL) {
        return CORE_INVALID_STATE;
    }
    return input_raw_device_get_fd(device->raw_device, out_fd);
}

ErrorCode input_device_get_info(InputDeviceHandle device, InputDeviceInfo *out_info)
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
static ErrorCode input_hand_frame_to_caller(InputDeviceHandle device, InputFrame *out_frame)
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

/* Emits a frame holding only SyncLost. Whatever was collected is discarded
 * first: after a dropped sync it can no longer be trusted. */
static ErrorCode input_emit_sync_lost(InputDeviceHandle device, InputFrame *out_frame)
{
    device->events[0].kind = INPUT_EVENT_KIND_SYNC_LOST;
    device->events[0].code = INPUT_RAW_SYN_DROPPED;
    device->events[0].value = 0;
    device->event_count = 1;

    return input_hand_frame_to_caller(device, out_frame);
}

/*
 * Reads and assembles the next complete frame without waiting.
 *
 * Three things are kept across calls, because one read() and one call do not
 * have to line up:
 *   - a frame whose SYN_REPORT has not arrived yet stays accumulated, and
 *     NoFrame is reported until it does;
 *   - raw events read past the frame that was just returned stay pending;
 *   - after SYN_DROPPED every event up to the next SYN_REPORT is skipped,
 *     which is what the kernel itself does with them.
 */
ErrorCode input_device_read_frame(InputDeviceHandle device, InputReadStatus *out_status,
                                  InputFrame *out_frame)
{
    if (device == NULL || out_status == NULL || out_frame == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (device->raw_device == NULL) {
        return CORE_INVALID_STATE;
    }

    for (;;) {
        /* Refill the pending batch once the previous one is used up. */
        if (device->pending_pos >= device->pending_count) {
            InputRawReadStatus raw_status = INPUT_RAW_READ_STATUS_NO_EVENTS;
            usize raw_count = 0;

            ErrorCode result = input_raw_device_read(device->raw_device, device->pending,
                                                     INPUT_FRAME_MAX_EVENTS, &raw_status, &raw_count);
            if (result != CORE_OK) {
                input_discard_all(device);
                return result;
            }

            if (raw_status == INPUT_RAW_READ_STATUS_DEVICE_LOST) {
                input_discard_all(device);
                *out_status = INPUT_READ_STATUS_DEVICE_LOST;
                return CORE_OK;
            }

            if (raw_status == INPUT_RAW_READ_STATUS_NO_EVENTS) {
                /* An unterminated frame stays accumulated, so the next call can
                 * finish it. Nothing is handed over without its SYN_REPORT. */
                *out_status = INPUT_READ_STATUS_NO_FRAME;
                return CORE_OK;
            }

            device->pending_count = raw_count;
            device->pending_pos = 0;
        }

        while (device->pending_pos < device->pending_count) {
            InputRawEvent *raw = &device->pending[device->pending_pos];
            InputEventKind kind = input_classify(raw->type, raw->code);

            device->pending_pos += 1;

            if (kind == INPUT_EVENT_KIND_SYNC_LOST) {
                /* Neither what was collected nor what follows belongs to any
                 * frame until the kernel resynchronises. */
                ErrorCode result;

                device->resyncing = true;
                result = input_emit_sync_lost(device, out_frame);
                if (result != CORE_OK) {
                    return result;
                }
                *out_status = INPUT_READ_STATUS_FRAME_READY;
                return CORE_OK;
            }

            if (device->resyncing) {
                /* Events between SYN_DROPPED and the next SYN_REPORT are
                 * dropped by the kernel too, so they are dropped here. */
                if (kind == INPUT_EVENT_KIND_SYNC_REPORT) {
                    device->resyncing = false;
                }
                continue;
            }

            if (device->event_count >= INPUT_FRAME_MAX_EVENTS) {
                /* An unterminated frame this long is treated as a lost sync. */
                ErrorCode result;

                device->resyncing = true;
                result = input_emit_sync_lost(device, out_frame);
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
                ErrorCode result = input_hand_frame_to_caller(device, out_frame);

                if (result != CORE_OK) {
                    return result;
                }
                *out_status = INPUT_READ_STATUS_FRAME_READY;
                return CORE_OK;
            }
        }
    }
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

ErrorCode input_device_close(InputDeviceHandle *device)
{
    if (device == NULL || *device == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    InputDeviceHandle target = *device;
    ErrorCode result = CORE_OK;

    if (target->raw_device != NULL) {
        result = input_raw_device_close(&target->raw_device);
    }

    MemoryBlock block;
    block.data = (u8 *)target;
    block.size = sizeof(struct InputDeviceHandle);
    (void)memory_free(&block);

    *device = NULL;
    return result;
}
