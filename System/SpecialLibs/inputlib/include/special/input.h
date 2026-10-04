#ifndef PUNOS_SPECIAL_INPUT_H
#define PUNOS_SPECIAL_INPUT_H

/*
 * inputlib (special/input) - frames of physical events from one Linux input
 * subsystem device.
 *
 * Specification: DevDoc/SpecialLibs/inputlib.md
 *
 * The library is a technical adapter over the Linux input subsystem, used by
 * IOserv. It reads raw evdev events through sys/input-device, normalises each
 * one into an InputEvent and groups them into frames terminated by
 * SYN_REPORT. It routes nothing, interprets no gesture, and offers no API to
 * applications: which device to open, and what to do with the events, belongs
 * to IOserv.
 *
 * The Linux evdev ABI, reading and ioctl() are reached only through
 * sys/input-device, so the evdev structures are never spelled out here.
 *
 * Events keep the values the kernel reported. A physical key code is not
 * turned into a character, coordinates are not scaled and no gesture is
 * recognised:
 *
 *   Linux event                    InputEventKind    code        value
 *   EV_KEY                         Key               KEY_*       0 up, 1 down, 2 repeat
 *   EV_REL                         RelativeAxis      REL_*       relative delta
 *   EV_ABS                         AbsoluteAxis      ABS_*       absolute position
 *   EV_SYN / SYN_REPORT            SyncReport        0           0
 *   EV_SYN / SYN_DROPPED           SyncLost          3           0
 *   any other EV_*                 Raw               as reported as reported
 *
 * Conventions for every operation that can fail:
 *   - it returns ErrorCode, and CORE_OK is the only success value;
 *   - results are written through out_* pointers;
 *   - out_* is defined only on CORE_OK, except out_status in
 *     input_device_read_frame(), which is defined on CORE_OK, and out_frame,
 *     which is defined only when out_status is
 *     INPUT_READ_STATUS_FRAME_READY;
 *   - a closed handle makes every operation on it return CORE_INVALID_STATE.
 *
 * Threading: one handle belongs to the thread that opened it. Concurrent
 * operations on one handle are not allowed; IOserv serialises them with
 * libsync.
 *
 * Readiness: the device is opened O_NONBLOCK | O_CLOEXEC, so
 * input_device_read_frame() never waits for the kernel. It reports
 * INPUT_READ_STATUS_NO_FRAME instead when no complete frame is available.
 * IOserv registers the descriptor from input_device_get_fd() in
 * sys/eventloop; EVENT_READABLE means a read may be attempted, while
 * EVENT_HANGUP or EVENT_ERROR mean the handle must be read once more to
 * obtain INPUT_READ_STATUS_DEVICE_LOST and then closed.
 */

#include <sys/core.h>
#include <sys/input-device.h>
#include <sys/memory.h>
#include <sys/string.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                */
/* ------------------------------------------------------------------------ */

/* Bit mask of INPUT_DEVICE_* flags, describing the kinds of event a device
 * can report, as read once when the device was opened. */
typedef u32 InputDeviceClass;

/* The device reports key events with KEY_* codes. */
#define INPUT_DEVICE_KEYBOARD ((InputDeviceClass)1)

/* The device reports relative axes, or pointer buttons. */
#define INPUT_DEVICE_POINTER ((InputDeviceClass)2)

/* The device reports absolute axes, for touch or pen. */
#define INPUT_DEVICE_TOUCH ((InputDeviceClass)4)

/* The device matches none of the supported classes. */
#define INPUT_DEVICE_UNKNOWN ((InputDeviceClass)0)

/*
 * Most events one frame may hold before it counts as unterminated. A frame
 * that reaches this without a SYN_REPORT is reported as a single SyncLost
 * event, because the library can no longer tell where the real boundaries
 * were.
 */
#define INPUT_FRAME_MAX_EVENTS ((usize)256)

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/*
 * Opaque context for one open /dev/input/event* device. Created by
 * input_device_open() and released exactly once by input_device_close().
 * Never build one by hand.
 */
typedef struct InputDeviceHandle *InputDeviceHandle;

/* What to open. path is not owned: it only has to stay valid for the
 * input_device_open() call. */
typedef struct InputDeviceConfig {
    const String *path;
} InputDeviceConfig;

/*
 * Identity and capabilities of a device, captured once when it was opened so
 * that reading frames needs no further ioctl(). The four identifiers are the
 * Linux bus/vendor/product/version triple and quadruple; classes says which
 * of the INPUT_DEVICE_* kinds of event the device reports.
 */
typedef struct InputDeviceInfo {
    u16 bus;
    u16 vendor;
    u16 product;
    u16 version;
    InputDeviceClass classes;
} InputDeviceInfo;

/*
 * What one event means. The specification lists these values by name only,
 * without pinning numbers. This library numbers them in the order the table
 * in the file header lists them, starting at 0; the numbers are then part of
 * the ABI.
 */
typedef enum InputEventKind {
    INPUT_EVENT_KIND_KEY = 0,
    INPUT_EVENT_KIND_RELATIVE_AXIS = 1,
    INPUT_EVENT_KIND_ABSOLUTE_AXIS = 2,
    INPUT_EVENT_KIND_SYNC_REPORT = 3,
    INPUT_EVENT_KIND_SYNC_LOST = 4,
    INPUT_EVENT_KIND_RAW = 5
} InputEventKind;

/*
 * One normalised Linux input event. code and value are exactly what the
 * kernel reported, so a consumer can compare them against the Linux constants
 * it already knows. INPUT_EVENT_KIND_RAW carries an event type the library
 * does not classify, left for IOserv to deal with explicitly.
 */
typedef struct InputEvent {
    InputEventKind kind;
    u16 code;
    i32 value;
} InputEvent;

/* ------------------------------------------------------------------------ */
/* Types - frames                                                            */
/* ------------------------------------------------------------------------ */

/*
 * An atomic sequence of events as the kernel grouped them, ending in a
 * SyncReport event.
 *
 * The frame owns its array: release it exactly once with
 * input_frame_destroy(). A frame handed to the caller always has
 * events != NULL and count >= 1; the pair { NULL, 0 } is only valid after
 * destroy().
 *
 * A frame whose only event is SyncLost means the kernel dropped events or the
 * previous frame ran past INPUT_FRAME_MAX_EVENTS. IOserv must then discard
 * whatever it believed about pressed keys, buttons and touches for this
 * device before handling the rest.
 */
typedef struct InputFrame {
    InputEvent *events;
    usize count;
} InputFrame;

/* Outcome of one input_device_read_frame() attempt. */
typedef enum InputReadStatus {
    INPUT_READ_STATUS_FRAME_READY = 0,
    INPUT_READ_STATUS_NO_FRAME = 1,
    INPUT_READ_STATUS_DEVICE_LOST = 2
} InputReadStatus;

_Static_assert(sizeof(InputDeviceClass) == 4, "InputDeviceClass ABI mismatch");
_Static_assert(sizeof(InputDeviceInfo) == 12, "InputDeviceInfo ABI mismatch");
_Static_assert(sizeof(InputEventKind) == 4, "InputEventKind ABI mismatch");
_Static_assert(sizeof(InputEvent) == 12, "InputEvent ABI mismatch");
_Static_assert(sizeof(InputFrame) == 16, "InputFrame ABI mismatch");
_Static_assert(sizeof(InputReadStatus) == 4, "InputReadStatus ABI mismatch");

/* ------------------------------------------------------------------------ */
/* Operations                                                               */
/* ------------------------------------------------------------------------ */

/*
 * Opens the device at config->path without blocking and reads its identity
 * and capabilities once, storing the handle in *out_device.
 *
 * Errors: CORE_INVALID_ARGUMENT if config, config->path, config->path->data
 * or out_device is invalid; codes from core_error_from_errno() if the device
 * cannot be opened (CORE_NOT_FOUND if the path does not exist,
 * CORE_PERMISSION_DENIED if the process may not open it, CORE_UNSUPPORTED if
 * it does not speak the required evdev ABI); errors of libmem.
 */
ErrorCode input_device_open(const InputDeviceConfig *config, InputDeviceHandle *out_device);

/*
 * Stores the device's file descriptor in *out_fd. The descriptor belongs to
 * the library, not to the caller: register it in sys/eventloop, but never
 * close it.
 *
 * Errors: CORE_INVALID_ARGUMENT if device or out_fd is NULL; CORE_INVALID_
 * STATE if the device is closed.
 */
ErrorCode input_device_get_fd(InputDeviceHandle device, i32 *out_fd);

/*
 * Stores the identity and classes captured at input_device_open() in
 * *out_info. Reading them needs no syscall.
 *
 * Errors: CORE_INVALID_ARGUMENT if device or out_info is NULL; CORE_INVALID_
 * STATE if the device is closed.
 */
ErrorCode input_device_get_info(InputDeviceHandle device, InputDeviceInfo *out_info);

/*
 * Reads without waiting and assembles the next complete frame, normalising
 * each event on the way. Frames are returned in the order the kernel
 * produced them.
 *
 * On INPUT_READ_STATUS_FRAME_READY, out_frame owns its array and must be
 * released with input_frame_destroy(). On INPUT_READ_STATUS_NO_FRAME nothing
 * complete was available and out_frame is not written. On
 * INPUT_READ_STATUS_DEVICE_LOST the device is gone: the handle may only be
 * closed afterwards.
 *
 * Errors: CORE_INVALID_ARGUMENT if device, out_status or out_frame is NULL;
 * CORE_INVALID_STATE if the device is closed; codes from
 * core_error_from_errno() for an unexpected read failure; errors of libmem.
 */
ErrorCode input_device_read_frame(InputDeviceHandle device, InputReadStatus *out_status,
                                  InputFrame *out_frame);

/*
 * Releases the array a frame owns and leaves the structure { NULL, 0 },
 * which every operation accepts.
 *
 * Errors: CORE_INVALID_ARGUMENT if frame is NULL; errors of libmem.
 */
ErrorCode input_frame_destroy(InputFrame *frame);

/*
 * Closes the device's file descriptor, releases the handle and writes NULL
 * through *device. Frames the caller still owns are unaffected.
 *
 * Errors: CORE_INVALID_ARGUMENT if device or *device is NULL; codes from
 * core_error_from_errno() if close() fails (the handle is still released).
 */
ErrorCode input_device_close(InputDeviceHandle *device);

#endif
