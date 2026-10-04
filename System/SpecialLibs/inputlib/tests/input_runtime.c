/* Checks 1-9: validation/metadata; 10-29: frames, boundaries and loss;
 * 30-35: disconnection/lifetime; 36-42: paths/errors/mouse-only metadata.
 * A scripted sys/input-device adapter makes
 * frame tests deterministic without evdev hardware, including in QEMU. */
#define CHECK_TEST_NAME "inputlib"
#include "../../../SystemLibs/testing/check.h"
#include <special/input.h>
#include <sys/input-device.h>

static InputRawEvent script[600];
static usize script_count, position;
static bool disconnected;
static ErrorCode open_error;
static bool mouse_only;
static u8 raw_token;
ErrorCode input_raw_device_get_key_codes(InputRawDeviceHandle device, Bytes out)
{
    (void)device;
    for (usize i = 0; i < out.size; ++i) out.data[i] = 0;
    if (!mouse_only) out.data[30 / 8] = (u8)(1u << (30 % 8));
    out.data[0x110 / 8] = 1;
    return CORE_OK;
}
ErrorCode input_raw_device_open(const InputRawDeviceConfig *config, InputRawDeviceHandle *out)
{
    if (config->path->length == 0) return CORE_INVALID_ARGUMENT;
    if (open_error != CORE_OK) return open_error;
    *out = (InputRawDeviceHandle)&raw_token;
    return CORE_OK;
}
ErrorCode input_raw_device_get_info(InputRawDeviceHandle device, InputRawDeviceInfo *out)
{
    (void)device;
    *out = (InputRawDeviceInfo){1, 2, 3, 4, mouse_only ? INPUT_RAW_CAP_KEY : INPUT_RAW_CAP_KEY | INPUT_RAW_CAP_RELATIVE | INPUT_RAW_CAP_ABSOLUTE};
    return CORE_OK;
}
ErrorCode input_raw_device_get_fd(InputRawDeviceHandle device, i32 *out)
{
    (void)device; *out = 42; return CORE_OK;
}
ErrorCode input_raw_device_read(InputRawDeviceHandle device, InputRawEvent *out, usize capacity,
                               InputRawReadStatus *status, usize *count)
{
    (void)device; (void)capacity;
    if (position == script_count) {
        *status = disconnected ? INPUT_RAW_READ_STATUS_DEVICE_LOST : INPUT_RAW_READ_STATUS_NO_EVENTS;
    } else {
        *status = INPUT_RAW_READ_STATUS_EVENTS_READY;
        *out = script[position++]; *count = 1;
    }
    return CORE_OK;
}
ErrorCode input_raw_device_close(InputRawDeviceHandle *device)
{
    *device = NULL; return CORE_OK;
}
static void event(u16 type, u16 code, i32 value)
{
    script[script_count++] = (InputRawEvent){type, code, value, 0};
}
int main(void)
{
    String path;
    InputDeviceHandle device = NULL;
    InputDeviceInfo info;
    InputFrame frame = {NULL, 0};
    InputReadStatus status;
    i32 fd;
    EXPECT(1, string_from_c_str("/dev/input/event0", &path) == CORE_OK);
    EXPECT(2, input_device_open(NULL, &device) == CORE_INVALID_ARGUMENT);
    EXPECT(3, input_device_open(&(InputDeviceConfig){&path}, &device) == CORE_OK);
    EXPECT(4, input_device_get_info(device, &info) == CORE_OK && info.classes == 7 && info.vendor == 2);
    EXPECT(5, input_device_get_fd(device, &fd) == CORE_OK && fd == 42);
    EXPECT(6, input_device_get_fd(NULL, &fd) == CORE_INVALID_STATE);
    EXPECT(7, input_device_read_frame(device, NULL, &frame) == CORE_INVALID_ARGUMENT);
    EXPECT(8, input_frame_destroy(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(9, input_device_read_frame(device, &status, &frame) == CORE_OK && status == INPUT_READ_STATUS_NO_FRAME);
    event(1, 30, 2); event(2, 0, -7); event(3, 1, 900); event(9, 12, -10);
    EXPECT(10, input_device_read_frame(device, &status, &frame) == CORE_OK && status == INPUT_READ_STATUS_NO_FRAME);
    event(0, 0, 0); event(1, 31, 0); event(0, 0, 0);
    EXPECT(11, input_device_read_frame(device, &status, &frame) == CORE_OK && status == INPUT_READ_STATUS_FRAME_READY && frame.count == 5);
    EXPECT(12, frame.events[0].kind == INPUT_EVENT_KIND_KEY && frame.events[0].value == 2);
    EXPECT(13, frame.events[1].kind == INPUT_EVENT_KIND_RELATIVE_AXIS && frame.events[1].value == -7);
    EXPECT(14, frame.events[2].kind == INPUT_EVENT_KIND_ABSOLUTE_AXIS && frame.events[2].value == 900);
    EXPECT(15, frame.events[3].kind == INPUT_EVENT_KIND_RAW && frame.events[3].code == 12 && frame.events[3].value == -10);
    EXPECT(16, frame.events[4].kind == INPUT_EVENT_KIND_SYNC_REPORT);
    EXPECT(17, input_frame_destroy(&frame) == CORE_OK && frame.events == NULL && frame.count == 0);
    EXPECT(18, input_device_read_frame(device, &status, &frame) == CORE_OK && frame.count == 2 && frame.events[0].code == 31);
    EXPECT(19, input_frame_destroy(&frame) == CORE_OK);
    event(1, 30, 1); event(0, 3, 0); event(1, 32, 1); event(0, 0, 0); event(0, 0, 0);
    EXPECT(20, input_device_read_frame(device, &status, &frame) == CORE_OK && frame.count == 1 && frame.events[0].kind == INPUT_EVENT_KIND_SYNC_LOST);
    EXPECT(21, input_frame_destroy(&frame) == CORE_OK);
    EXPECT(22, input_device_read_frame(device, &status, &frame) == CORE_OK && frame.count == 1 && frame.events[0].kind == INPUT_EVENT_KIND_SYNC_REPORT);
    EXPECT(23, input_frame_destroy(&frame) == CORE_OK);
    script_count = position = 0;
    for (usize i = 0; i < 255; ++i) event(1, 30, 1);
    event(0, 0, 0);
    EXPECT(24, input_device_read_frame(device, &status, &frame) == CORE_OK && frame.count == 256 && frame.events[255].kind == INPUT_EVENT_KIND_SYNC_REPORT);
    EXPECT(25, input_frame_destroy(&frame) == CORE_OK);
    for (usize i = 0; i < 257; ++i) event(1, 30, 1);
    event(0, 0, 0); event(0, 0, 0);
    EXPECT(26, input_device_read_frame(device, &status, &frame) == CORE_OK && frame.count == 1 && frame.events[0].kind == INPUT_EVENT_KIND_SYNC_LOST);
    EXPECT(27, input_frame_destroy(&frame) == CORE_OK);
    EXPECT(28, input_device_read_frame(device, &status, &frame) == CORE_OK && frame.count == 1 && frame.events[0].kind == INPUT_EVENT_KIND_SYNC_REPORT);
    EXPECT(29, input_frame_destroy(&frame) == CORE_OK);
    event(1, 44, 1); disconnected = true;
    EXPECT(30, input_device_read_frame(device, &status, &frame) == CORE_OK && status == INPUT_READ_STATUS_DEVICE_LOST && frame.events == NULL);
    EXPECT(31, input_device_read_frame(device, &status, &frame) == CORE_OK && status == INPUT_READ_STATUS_DEVICE_LOST);
    EXPECT(32, input_device_close(&device) == CORE_OK && device == NULL);
    EXPECT(33, input_device_close(&device) == CORE_INVALID_STATE);
    EXPECT(34, input_device_read_frame(device, &status, &frame) == CORE_INVALID_STATE);
    EXPECT(35, string_destroy(&path) == CORE_OK);
    EXPECT(36, string_from_c_str("/dev/input/event0/../file", &path) == CORE_OK);
    EXPECT(37, input_device_open(&(InputDeviceConfig){&path}, &device) == CORE_INVALID_PATH);
    (void)string_destroy(&path);
    EXPECT(38, string_from_c_str("/dev/input/event0", &path) == CORE_OK);
    ErrorCode errors[] = {CORE_NOT_FOUND, CORE_PERMISSION_DENIED, CORE_OUT_OF_MEMORY, CORE_UNSUPPORTED, CORE_IO_ERROR};
    for (usize i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        open_error = errors[i];
        EXPECT(39, input_device_open(&(InputDeviceConfig){&path}, &device) == errors[i] && device == NULL);
    }
    open_error = CORE_OK; mouse_only = true;
    EXPECT(40, input_device_open(&(InputDeviceConfig){&path}, &device) == CORE_OK);
    EXPECT(41, input_device_get_info(device, &info) == CORE_OK && info.classes == INPUT_DEVICE_POINTER);
    EXPECT(42, input_device_close(&device) == CORE_OK && string_destroy(&path) == CORE_OK);
    check_passed(); return 0;
}
