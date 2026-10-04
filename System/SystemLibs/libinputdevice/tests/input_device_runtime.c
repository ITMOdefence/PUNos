/*
 * Runtime test of the libinputdevice operations against
 * DevDoc/SystemLibs/libinputdevice.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 * This library is inherently hardware-bound: the test always exercises the
 * CORE_NOT_FOUND path (a guaranteed-missing path needs no device at all),
 * and adapts to whatever /dev/input/event* it can find:
 *   - none present: only the not-found/argument checks run;
 *   - present but not readable by this process (the common case for an
 *     unprivileged `make check`): the CORE_PERMISSION_DENIED path is
 *     checked and nothing further;
 *   - present and readable (running as root, e.g. inside the PUNos
 *     initramfs, or through `sudo`): the full open/get_fd/get_info/read/
 *     close success path is checked against the real device.
 */
#define CHECK_TEST_NAME "libinputdevice"
#include "../../testing/check.h"

#include <sys/input-device.h>

static String str(const char *text)
{
    String result;

    if (string_from_c_str(text, &result) != CORE_OK) {
        check_fail(250);
    }
    return result;
}

static void destroy(String *value)
{
    (void)string_destroy(value);
}

/* Formats "/dev/input/event<index>" without sprintf(), which PUNos has no
 * wrapper for. buffer must be at least 32 bytes. */
static void format_event_path(char *buffer, i32 index)
{
    static const char prefix[] = "/dev/input/event";
    usize prefix_length = sizeof(prefix) - 1;
    char digits[4];
    usize digit_count = 0;
    usize position = prefix_length;
    usize i;

    for (i = 0; i < prefix_length; ++i) {
        buffer[i] = prefix[i];
    }
    if (index == 0) {
        digits[digit_count++] = '0';
    } else {
        i32 value = index;

        while (value > 0) {
            digits[digit_count++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }
    while (digit_count > 0) {
        --digit_count;
        buffer[position++] = digits[digit_count];
    }
    buffer[position] = '\0';
}

/* Finds the first /dev/input/eventN that exists, whether or not this
 * process can actually read it. buffer must be at least 32 bytes. */
static bool find_candidate_device(char *buffer)
{
    i32 index;

    for (index = 0; index < 32; ++index) {
        i32 probe_fd;

        format_event_path(buffer, index);
        probe_fd = open(buffer, O_RDONLY | O_NONBLOCK, 0);
        if (probe_fd >= 0) {
            (void)close(probe_fd);
            return true;
        }
        if (errno != ENOENT) {
            return true; /* exists, just not accessible right now */
        }
    }
    return false;
}

static void test_missing_device(void)
{
    String bogus_path = str("/dev/input/event_does_not_exist_9999");
    InputRawDeviceConfig config;
    InputRawDeviceHandle device = NULL;
    i32 fd = -1;

    EXPECT(1, input_raw_device_open(NULL, &device) == CORE_INVALID_ARGUMENT);
    config.path = &bogus_path;
    EXPECT(2, input_raw_device_open(&config, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(3, input_raw_device_open(&config, &device) == CORE_NOT_FOUND);
    EXPECT(4, device == NULL);

    EXPECT(5, input_raw_device_get_fd(NULL, &fd) == CORE_INVALID_ARGUMENT);
    EXPECT(6, input_raw_device_get_info(NULL, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(7, input_raw_device_close(NULL) == CORE_INVALID_ARGUMENT);
    {
        InputRawDeviceHandle already_null = NULL;

        EXPECT(8, input_raw_device_close(&already_null) == CORE_INVALID_ARGUMENT);
    }
    {
        InputRawReadStatus status;

        EXPECT(9, input_raw_device_read(NULL, NULL, 0, &status, NULL) ==
                      CORE_INVALID_ARGUMENT);
    }

    destroy(&bogus_path);
}

static void test_real_device(void)
{
    char path_buffer[32];
    String path;
    InputRawDeviceConfig config;
    InputRawDeviceHandle device = NULL;
    ErrorCode open_result;

    if (!find_candidate_device(path_buffer)) {
        return; /* no /dev/input/event* at all on this machine */
    }

    path = str(path_buffer);
    config.path = &path;
    open_result = input_raw_device_open(&config, &device);

    if (open_result == CORE_PERMISSION_DENIED) {
        /* Unprivileged run (the common case for `make check`): the device
         * exists, so this is a real, hardware-backed exercise of the
         * permission error path, just not of the success path below. Run
         * as root (inside PUNos, or through sudo on the host) for that. */
        EXPECT(20, device == NULL);
        destroy(&path);
        return;
    }
    EXPECT(21, open_result == CORE_OK && device != NULL);

    {
        i32 fd = -1;
        InputRawDeviceInfo info;

        EXPECT(22, input_raw_device_get_fd(device, &fd) == CORE_OK && fd >= 0);
        EXPECT(23, input_raw_device_get_fd(device, NULL) == CORE_INVALID_ARGUMENT);

        EXPECT(24, input_raw_device_get_info(device, &info) == CORE_OK);
        EXPECT(25, input_raw_device_get_info(device, NULL) == CORE_INVALID_ARGUMENT);
        /* Only the three known bits may ever be set. */
        EXPECT(26, (info.capabilities &
                    ~(InputRawCapabilityMask)(INPUT_RAW_CAP_KEY | INPUT_RAW_CAP_RELATIVE |
                                               INPUT_RAW_CAP_ABSOLUTE)) == 0);
    }

    {
        InputRawEvent events[8];
        InputRawReadStatus status;
        usize count = 0;

        /* A non-blocking read must return promptly either way, never hang;
         * EventsReady is accepted too since real hardware may genuinely
         * have a pending event (e.g. touchpad jitter) during the test. */
        EXPECT(27, input_raw_device_read(device, events, 8, &status, &count) == CORE_OK);
        EXPECT(28, status == INPUT_RAW_READ_STATUS_EVENTS_READY ||
                       status == INPUT_RAW_READ_STATUS_NO_EVENTS);
        EXPECT(29, input_raw_device_read(NULL, events, 8, &status, &count) ==
                       CORE_INVALID_ARGUMENT);
        EXPECT(30, input_raw_device_read(device, NULL, 8, &status, &count) ==
                       CORE_INVALID_ARGUMENT);
        EXPECT(31, input_raw_device_read(device, events, 8, NULL, &count) ==
                       CORE_INVALID_ARGUMENT);
        EXPECT(32, input_raw_device_read(device, events, 8, &status, NULL) ==
                       CORE_INVALID_ARGUMENT);
        EXPECT(33, input_raw_device_read(device, events, 0, &status, NULL) == CORE_OK &&
                       status == INPUT_RAW_READ_STATUS_NO_EVENTS);
    }

    {
        u8 codes[INPUT_RAW_KEY_BITMAP_SIZE];
        EXPECT(37, input_raw_device_get_key_codes(device, (Bytes){codes, sizeof(codes)}) == CORE_OK);
        EXPECT(38, input_raw_device_get_key_codes(device, (Bytes){codes, sizeof(codes) - 1}) == CORE_INVALID_ARGUMENT);
        EXPECT(39, input_raw_device_get_key_codes(device, (Bytes){NULL, sizeof(codes)}) == CORE_INVALID_ARGUMENT);
    }
    EXPECT(34, input_raw_device_close(&device) == CORE_OK);
    EXPECT(35, device == NULL);
    destroy(&path);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    test_missing_device();
    EXPECT(36, input_raw_device_get_key_codes(NULL, (Bytes){NULL, 0}) == CORE_INVALID_ARGUMENT);
    test_real_device();

    check_passed();
    return 0;
}
