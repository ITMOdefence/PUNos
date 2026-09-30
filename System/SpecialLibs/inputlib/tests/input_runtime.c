/*
 * Runtime test of the inputlib operations against
 * DevDoc/SpecialLibs/inputlib.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 *
 * The test needs a real evdev device, which neither the host build nor the
 * PUNos initramfs is guaranteed to have. /dev/input/event0 is used when it is
 * present and openable; otherwise the checks that need a device are skipped
 * and only the argument validation is verified. That keeps the test honest on
 * a machine without input devices instead of failing for the wrong reason.
 *
 * Check number groups:
 *   1-5    input_device_open / input_device_close with invalid arguments
 *   10     a device path that does not exist
 *   20-32  get_fd, get_info, read_frame, input_frame_destroy on a real device
 *   40-43  read_frame and input_frame_destroy with invalid arguments
 */

#define CHECK_TEST_NAME "inputlib"
#include <check.h>

#include <punos/syscall/syscall.h>
#include <punos/syscall/constants.h>
#include <special/input.h>
#include <sys/memory.h>
#include <sys/string.h>

#define TEST_DEVICE_PATH "/dev/input/event0"

static String make_str(const char *text)
{
    String s;
    if (string_from_c_str(text, &s) != CORE_OK) {
        check_fail(250);
    }
    return s;
}

static void destroy_str(String *s)
{
    (void)string_destroy(s);
}

static void test_open_close_errors(void)
{
    String path = make_str(TEST_DEVICE_PATH);
    InputDeviceHandle *device = NULL;
    InputDeviceConfig config;

    config.path = &path;

    EXPECT(1, input_device_open(NULL, &device) == CORE_INVALID_ARGUMENT);
    EXPECT(2, input_device_open(&config, NULL) == CORE_INVALID_ARGUMENT);

    InputDeviceConfig bad_config;
    bad_config.path = NULL;
    EXPECT(3, input_device_open(&bad_config, &device) == CORE_INVALID_ARGUMENT);

    EXPECT(4, input_device_close(NULL) == CORE_INVALID_ARGUMENT);
    {
        InputDeviceHandle *null_device = NULL;
        EXPECT(5, input_device_close(&null_device) == CORE_INVALID_ARGUMENT);
    }

    destroy_str(&path);
}

static void test_open_nonexistent(void)
{
    String path = make_str("/dev/input/event999");
    InputDeviceHandle *device = NULL;
    InputDeviceConfig config;

    config.path = &path;

    EXPECT(10, input_device_open(&config, &device) == CORE_NOT_FOUND);

    destroy_str(&path);
}

static void test_get_fd_and_info(void)
{
    String path = make_str(TEST_DEVICE_PATH);
    InputDeviceHandle *device = NULL;
    InputDeviceConfig config;

    config.path = &path;

    ErrorCode result = input_device_open(&config, &device);
    if (result == CORE_OK) {
        i32 fd = -1;
        EXPECT(20, input_device_get_fd(device, &fd) == CORE_OK && fd >= 0);
        EXPECT(21, input_device_get_fd(NULL, &fd) == CORE_INVALID_ARGUMENT);
        EXPECT(22, input_device_get_fd(device, NULL) == CORE_INVALID_ARGUMENT);

        InputDeviceInfo info;
        EXPECT(23, input_device_get_info(device, &info) == CORE_OK);
        EXPECT(24, input_device_get_info(NULL, &info) == CORE_INVALID_ARGUMENT);
        EXPECT(25, input_device_get_info(device, NULL) == CORE_INVALID_ARGUMENT);

        InputReadStatus status;
        InputFrame frame;
        EXPECT(26, input_device_read_frame(device, &status, &frame) == CORE_OK);
        EXPECT(27, status == INPUT_READ_STATUS_NO_FRAME || status == INPUT_READ_STATUS_FRAME_READY);

        if (status == INPUT_READ_STATUS_FRAME_READY) {
            EXPECT(28, frame.count > 0);
            EXPECT(29, input_frame_destroy(&frame) == CORE_OK);
            EXPECT(30, frame.events == NULL && frame.count == 0);
        }

        EXPECT(31, input_device_close(&device) == CORE_OK);
        EXPECT(32, device == NULL);
    } else {
        EXPECT(20, result == CORE_NOT_FOUND || result == CORE_PERMISSION_DENIED);
    }

    destroy_str(&path);
}

static void test_read_frame_errors(void)
{
    String path = make_str(TEST_DEVICE_PATH);
    InputDeviceHandle *device = NULL;
    InputDeviceConfig config;

    config.path = &path;

    ErrorCode result = input_device_open(&config, &device);
    if (result == CORE_OK) {
        InputReadStatus status;
        InputFrame frame;

        EXPECT(40, input_device_read_frame(NULL, &status, &frame) == CORE_INVALID_ARGUMENT);
        EXPECT(41, input_device_read_frame(device, NULL, &frame) == CORE_INVALID_ARGUMENT);
        EXPECT(42, input_frame_destroy(NULL) == CORE_INVALID_ARGUMENT);

        EXPECT(43, input_device_close(&device) == CORE_OK);
    }

    destroy_str(&path);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    test_open_close_errors();
    test_open_nonexistent();
    test_get_fd_and_info();
    test_read_frame_errors();

    check_passed();
    return 0;
}
