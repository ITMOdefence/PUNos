/*
 * Compile-time checks of the libinputdevice ABI against
 * DevDoc/SystemLibs/libinputdevice.md. This file only has to compile; it
 * produces no runnable program.
 */
#include <sys/input-device.h>

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)

_Static_assert(SAME_TYPE((InputRawCapabilityMask)0, u32), "InputRawCapabilityMask must be u32");

/* All these values are pinned by the specification's constants table. */
_Static_assert(INPUT_RAW_EVENT_SYN == 0, "INPUT_RAW_EVENT_SYN");
_Static_assert(INPUT_RAW_EVENT_KEY == 1, "INPUT_RAW_EVENT_KEY");
_Static_assert(INPUT_RAW_EVENT_REL == 2, "INPUT_RAW_EVENT_REL");
_Static_assert(INPUT_RAW_EVENT_ABS == 3, "INPUT_RAW_EVENT_ABS");
_Static_assert(INPUT_RAW_SYN_REPORT == 0, "INPUT_RAW_SYN_REPORT");
_Static_assert(INPUT_RAW_SYN_DROPPED == 3, "INPUT_RAW_SYN_DROPPED");
_Static_assert(INPUT_RAW_CAP_KEY == 1, "INPUT_RAW_CAP_KEY");
_Static_assert(INPUT_RAW_CAP_RELATIVE == 2, "INPUT_RAW_CAP_RELATIVE");
_Static_assert(INPUT_RAW_CAP_ABSOLUTE == 4, "INPUT_RAW_CAP_ABSOLUTE");

/*
 * InputRawReadStatus values are not pinned by the specification; this
 * library numbers them in the order the specification's table lists them.
 */
_Static_assert(INPUT_RAW_READ_STATUS_EVENTS_READY == 0, "INPUT_RAW_READ_STATUS_EVENTS_READY");
_Static_assert(INPUT_RAW_READ_STATUS_NO_EVENTS == 1, "INPUT_RAW_READ_STATUS_NO_EVENTS");
_Static_assert(INPUT_RAW_READ_STATUS_DEVICE_LOST == 2, "INPUT_RAW_READ_STATUS_DEVICE_LOST");

/* InputRawDeviceInfo layout: bus, vendor, product, version, capabilities. */
_Static_assert(__builtin_offsetof(InputRawDeviceInfo, bus) == 0, "InputRawDeviceInfo.bus offset");
_Static_assert(__builtin_offsetof(InputRawDeviceInfo, vendor) == 2,
               "InputRawDeviceInfo.vendor offset");
_Static_assert(__builtin_offsetof(InputRawDeviceInfo, product) == 4,
               "InputRawDeviceInfo.product offset");
_Static_assert(__builtin_offsetof(InputRawDeviceInfo, version) == 6,
               "InputRawDeviceInfo.version offset");
_Static_assert(__builtin_offsetof(InputRawDeviceInfo, capabilities) == 8,
               "InputRawDeviceInfo.capabilities offset");

/* InputRawEvent layout: type, code, value, timestamp. */
_Static_assert(__builtin_offsetof(InputRawEvent, type) == 0, "InputRawEvent.type offset");
_Static_assert(__builtin_offsetof(InputRawEvent, code) == 2, "InputRawEvent.code offset");
_Static_assert(__builtin_offsetof(InputRawEvent, value) == 4, "InputRawEvent.value offset");
_Static_assert(__builtin_offsetof(InputRawEvent, timestamp) == 8, "InputRawEvent.timestamp offset");
_Static_assert(SAME_TYPE(((InputRawEvent *)0)->timestamp, MonotonicTime),
               "InputRawEvent.timestamp type");

_Static_assert(sizeof(InputRawDeviceHandle) == sizeof(void *),
               "InputRawDeviceHandle must be a pointer");
