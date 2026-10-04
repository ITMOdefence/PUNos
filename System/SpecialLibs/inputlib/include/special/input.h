#ifndef PUNOS_SPECIAL_INPUT_H
#define PUNOS_SPECIAL_INPUT_H

#include <sys/core.h>
#include <sys/string.h>

typedef struct InputDeviceHandle *InputDeviceHandle;
typedef struct InputDeviceConfig { const String *path; } InputDeviceConfig;
typedef u32 InputDeviceClass;
#define INPUT_DEVICE_UNKNOWN ((InputDeviceClass)0)
#define INPUT_DEVICE_KEYBOARD ((InputDeviceClass)1)
#define INPUT_DEVICE_POINTER ((InputDeviceClass)2)
#define INPUT_DEVICE_TOUCH ((InputDeviceClass)4)
#define INPUT_FRAME_MAX_EVENTS ((usize)256)
typedef struct InputDeviceInfo {
    u16 bus, vendor, product, version;
    InputDeviceClass classes;
} InputDeviceInfo;
typedef enum InputEventKind {
    INPUT_EVENT_KIND_KEY = 0,
    INPUT_EVENT_KIND_RELATIVE_AXIS = 1,
    INPUT_EVENT_KIND_ABSOLUTE_AXIS = 2,
    INPUT_EVENT_KIND_SYNC_REPORT = 3,
    INPUT_EVENT_KIND_SYNC_LOST = 4,
    INPUT_EVENT_KIND_RAW = 5
} InputEventKind;
typedef struct InputEvent { InputEventKind kind; u16 code; i32 value; } InputEvent;
typedef struct InputFrame { InputEvent *events; usize count; } InputFrame;
typedef enum InputReadStatus {
    INPUT_READ_STATUS_FRAME_READY = 0,
    INPUT_READ_STATUS_NO_FRAME = 1,
    INPUT_READ_STATUS_DEVICE_LOST = 2
} InputReadStatus;

/* Thread-confined handles. The fd is borrowed; FrameReady transfers ownership
 * of events to the caller. Other statuses leave out_frame untouched. */
ErrorCode input_device_open(const InputDeviceConfig *config, InputDeviceHandle *out_device);
ErrorCode input_device_get_fd(InputDeviceHandle device, i32 *out_fd);
ErrorCode input_device_get_info(InputDeviceHandle device, InputDeviceInfo *out_info);
ErrorCode input_device_read_frame(InputDeviceHandle device, InputReadStatus *out_status,
                                 InputFrame *out_frame);
ErrorCode input_frame_destroy(InputFrame *frame);
ErrorCode input_device_close(InputDeviceHandle *device);

#endif
