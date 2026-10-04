#ifndef PUNOS_SYS_INPUT_DEVICE_H
#define PUNOS_SYS_INPUT_DEVICE_H

/*
 * libinputdevice (sys/input-device) - raw access to one Linux input
 * subsystem device through /dev/input/event*.
 *
 * Specification: DevDoc/SystemLibs/libinputdevice.md
 *
 * Encapsulates the Linux evdev ABI (struct input_event, EVIOCGID,
 * EVIOCGBIT) by hand: PUNos is freestanding and cannot include the host's
 * <linux/input.h>. The only syscall wrappers used are open(), close(),
 * read() and ioctl(). All operations return ErrorCode; see <sys/core.h>
 * for the conventions.
 */

#include <sys/core.h>
#include <sys/string.h>
#include <sys/time.h>

/* Opaque handle of one open input device. Closed exactly once with
 * input_raw_device_close(). */
typedef struct InputRawDeviceHandle *InputRawDeviceHandle;

typedef struct InputRawDeviceConfig {
    const String *path; /* non-empty /dev/input/event* path, no NUL */
} InputRawDeviceConfig;

/* Bit mask of INPUT_RAW_CAP_* flags. */
typedef u32 InputRawCapabilityMask;

#define INPUT_RAW_CAP_KEY ((InputRawCapabilityMask)1)
#define INPUT_RAW_CAP_RELATIVE ((InputRawCapabilityMask)2)
#define INPUT_RAW_CAP_ABSOLUTE ((InputRawCapabilityMask)4)
/* The device supports at least one key code below BTN_MISC (0x100), so it
 * reports keyboard keys and not only pointer buttons. Without this a mouse,
 * which also reports EV_KEY, would look like a keyboard. */
#define INPUT_RAW_CAP_KEYBOARD_KEYS ((InputRawCapabilityMask)8)
/* The device supports at least one button code, BTN_MISC (0x100) and up. */
#define INPUT_RAW_CAP_POINTER_BUTTONS ((InputRawCapabilityMask)16)

typedef struct InputRawDeviceInfo {
    u16 bus;
    u16 vendor;
    u16 product;
    u16 version;
    InputRawCapabilityMask capabilities;
} InputRawDeviceInfo;

/* Linux evdev event type/code constants the specification pins. */
#define INPUT_RAW_EVENT_SYN ((u16)0)
#define INPUT_RAW_EVENT_KEY ((u16)1)
#define INPUT_RAW_EVENT_REL ((u16)2)
#define INPUT_RAW_EVENT_ABS ((u16)3)
#define INPUT_RAW_SYN_REPORT ((u16)0)
#define INPUT_RAW_SYN_DROPPED ((u16)3)

typedef struct InputRawEvent {
    u16 type;
    u16 code;
    i32 value;
    MonotonicTime timestamp;
} InputRawEvent;

/*
 * The specification lists these values by name only, without pinning
 * numbers. This library numbers them in the order the specification's
 * table lists them, starting at 0.
 */
typedef enum InputRawReadStatus {
    INPUT_RAW_READ_STATUS_EVENTS_READY = 0,
    INPUT_RAW_READ_STATUS_NO_EVENTS = 1,
    INPUT_RAW_READ_STATUS_DEVICE_LOST = 2
} InputRawReadStatus;

_Static_assert(sizeof(InputRawDeviceInfo) == 12, "InputRawDeviceInfo ABI mismatch");
_Static_assert(sizeof(InputRawEvent) == 16, "InputRawEvent ABI mismatch");
_Static_assert(sizeof(InputRawReadStatus) == 4, "InputRawReadStatus ABI mismatch");

/*
 * Opens the Linux input device at config->path in non-blocking mode and
 * reads its bus/vendor/product/version and raw capabilities once, through
 * EVIOCGID and EVIOCGBIT.
 *
 * Errors: CORE_INVALID_ARGUMENT if config, config->path or out_device is
 * invalid; codes from core_error_from_errno() if open() or ioctl() fails;
 * errors of libmem.
 */
ErrorCode input_raw_device_open(const InputRawDeviceConfig *config,
                                InputRawDeviceHandle *out_device);

/*
 * Stores device's non-owning file descriptor in *out_fd. The caller never
 * closes it (specification contract 6).
 *
 * Errors: CORE_INVALID_ARGUMENT if device or out_fd is NULL.
 */
ErrorCode input_raw_device_get_fd(InputRawDeviceHandle device, i32 *out_fd);

/*
 * Stores the identifiers and capabilities captured at
 * input_raw_device_open() in *out_info.
 *
 * Errors: CORE_INVALID_ARGUMENT if device or out_info is NULL.
 */
ErrorCode input_raw_device_get_info(InputRawDeviceHandle device, InputRawDeviceInfo *out_info);

/*
 * Reads up to events_capacity raw events into out_events without waiting
 * (specification contract 7): if none are queued, stores NoEvents in
 * *out_status. Stores DeviceLost if the device was disconnected. Events are
 * copied verbatim and in order (contract 8): unknown type/code/value are
 * not filtered, and events are not grouped by SYN_REPORT. out_events and
 * out_count are only written when *out_status is EventsReady.
 *
 * Errors: CORE_INVALID_ARGUMENT if device or out_status is NULL, or
 * events_capacity != 0 with out_events or out_count NULL; codes from
 * core_error_from_errno() for an unexpected read() failure.
 */
ErrorCode input_raw_device_read(InputRawDeviceHandle device, InputRawEvent *out_events,
                                usize events_capacity, InputRawReadStatus *out_status,
                                usize *out_count);

/*
 * Closes device, releases its internal state and sets *device to NULL.
 *
 * Errors: CORE_INVALID_ARGUMENT if device or *device is NULL; codes from
 * core_error_from_errno() if close() fails (the handle is still released).
 */
ErrorCode input_raw_device_close(InputRawDeviceHandle *device);

#endif
