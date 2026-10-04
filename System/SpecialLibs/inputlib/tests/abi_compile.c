#include <special/input.h>
#define OFFSET(type, field) __builtin_offsetof(type, field)
_Static_assert(sizeof(InputDeviceHandle) == 8, "handle ABI");
_Static_assert(sizeof(InputDeviceConfig) == 8, "config ABI");
_Static_assert(sizeof(InputDeviceClass) == 4 && sizeof(InputDeviceInfo) == 12, "info ABI");
_Static_assert(OFFSET(InputDeviceInfo, classes) == 8, "classes offset");
_Static_assert(sizeof(InputEvent) == 12 && OFFSET(InputEvent, code) == 4 && OFFSET(InputEvent, value) == 8, "event ABI");
_Static_assert(sizeof(InputFrame) == 16 && OFFSET(InputFrame, count) == 8, "frame ABI");
_Static_assert(sizeof(InputEventKind) == 4 && sizeof(InputReadStatus) == 4, "enum ABI");
_Static_assert(INPUT_EVENT_KIND_KEY == 0 && INPUT_EVENT_KIND_RELATIVE_AXIS == 1 && INPUT_EVENT_KIND_ABSOLUTE_AXIS == 2 && INPUT_EVENT_KIND_SYNC_REPORT == 3 && INPUT_EVENT_KIND_SYNC_LOST == 4 && INPUT_EVENT_KIND_RAW == 5, "event values");
_Static_assert(INPUT_READ_STATUS_FRAME_READY == 0 && INPUT_READ_STATUS_NO_FRAME == 1 && INPUT_READ_STATUS_DEVICE_LOST == 2, "status values");
_Static_assert(INPUT_DEVICE_KEYBOARD == 1 && INPUT_DEVICE_POINTER == 2 && INPUT_DEVICE_TOUCH == 4 && INPUT_DEVICE_UNKNOWN == 0 && INPUT_FRAME_MAX_EVENTS == 256, "constants");
