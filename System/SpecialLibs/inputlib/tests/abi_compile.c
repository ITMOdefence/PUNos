/*
 * Compile-time checks of the inputlib ABI against
 * DevDoc/SpecialLibs/inputlib.md.
 * This file only has to compile; it produces no runnable program.
 */
#include <special/input.h>

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)

_Static_assert(SAME_TYPE((InputDeviceHandle)0, struct InputDeviceHandle *), "InputDeviceHandle pointer typedef");
_Static_assert(SAME_TYPE(&input_device_open, ErrorCode (*)(const InputDeviceConfig *, InputDeviceHandle *)), "input_device_open signature");
_Static_assert(SAME_TYPE(&input_device_close, ErrorCode (*)(InputDeviceHandle *)), "input_device_close signature");

_Static_assert(SAME_TYPE((InputDeviceClass)0, u32), "InputDeviceClass size");
_Static_assert(SAME_TYPE((InputEventKind)0, u32), "InputEventKind size");
_Static_assert(SAME_TYPE((InputReadStatus)0, u32), "InputReadStatus size");
_Static_assert(sizeof(InputDeviceInfo) == 12, "InputDeviceInfo ABI");
_Static_assert(sizeof(InputEvent) == 12, "InputEvent ABI");
_Static_assert(sizeof(InputFrame) == 16, "InputFrame ABI");

_Static_assert(INPUT_DEVICE_KEYBOARD == 1, "INPUT_DEVICE_KEYBOARD");
_Static_assert(INPUT_DEVICE_POINTER == 2, "INPUT_DEVICE_POINTER");
_Static_assert(INPUT_DEVICE_TOUCH == 4, "INPUT_DEVICE_TOUCH");
_Static_assert(INPUT_DEVICE_UNKNOWN == 0, "INPUT_DEVICE_UNKNOWN");
_Static_assert(INPUT_FRAME_MAX_EVENTS == 256, "INPUT_FRAME_MAX_EVENTS");

_Static_assert(INPUT_EVENT_KIND_KEY == 0, "INPUT_EVENT_KIND_KEY");
_Static_assert(INPUT_EVENT_KIND_RELATIVE_AXIS == 1, "INPUT_EVENT_KIND_RELATIVE_AXIS");
_Static_assert(INPUT_EVENT_KIND_ABSOLUTE_AXIS == 2, "INPUT_EVENT_KIND_ABSOLUTE_AXIS");
_Static_assert(INPUT_EVENT_KIND_SYNC_REPORT == 3, "INPUT_EVENT_KIND_SYNC_REPORT");
_Static_assert(INPUT_EVENT_KIND_SYNC_LOST == 4, "INPUT_EVENT_KIND_SYNC_LOST");
_Static_assert(INPUT_EVENT_KIND_RAW == 5, "INPUT_EVENT_KIND_RAW");

_Static_assert(INPUT_READ_STATUS_FRAME_READY == 0, "INPUT_READ_STATUS_FRAME_READY");
_Static_assert(INPUT_READ_STATUS_NO_FRAME == 1, "INPUT_READ_STATUS_NO_FRAME");
_Static_assert(INPUT_READ_STATUS_DEVICE_LOST == 2, "INPUT_READ_STATUS_DEVICE_LOST");

int main(void)
{
    return 0;
}
