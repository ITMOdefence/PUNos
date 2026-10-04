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
    InputDeviceHandle device = NULL;
    InputDeviceConfig config;

    config.path = &path;

    EXPECT(1, input_device_open(NULL, &device) == CORE_INVALID_ARGUMENT);
    EXPECT(2, input_device_open(&config, NULL) == CORE_INVALID_ARGUMENT);

    InputDeviceConfig bad_config;
    bad_config.path = NULL;
    EXPECT(3, input_device_open(&bad_config, &device) == CORE_INVALID_ARGUMENT);

    EXPECT(4, input_device_close(NULL) == CORE_INVALID_ARGUMENT);
    {
        InputDeviceHandle null_device = NULL;
        EXPECT(5, input_device_close(&null_device) == CORE_INVALID_ARGUMENT);
    }

    destroy_str(&path);
}

static void test_open_nonexistent(void)
{
    String path = make_str("/dev/input/event999");
    InputDeviceHandle device = NULL;
    InputDeviceConfig config;

    config.path = &path;

    EXPECT(10, input_device_open(&config, &device) == CORE_NOT_FOUND);

    destroy_str(&path);
}

static void test_get_fd_and_info(void)
{
    String path = make_str(TEST_DEVICE_PATH);
    InputDeviceHandle device = NULL;
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
    InputDeviceHandle device = NULL;
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

/* Issue 13: only /dev/input/event* may be opened, and an empty path is not a
 * path at all. Both must be refused here rather than by the lower layer. */
static void test_path_validation(void)
{
    InputDeviceHandle device = NULL;
    InputDeviceConfig config;

    String empty = make_str("");
    config.path = &empty;
    EXPECT(60, input_device_open(&config, &device) == CORE_INVALID_ARGUMENT);

    String wrong_dir = make_str("/dev/video0");
    config.path = &wrong_dir;
    EXPECT(61, input_device_open(&config, &device) == CORE_INVALID_PATH);

    String no_digits = make_str("/dev/input/eventX");
    config.path = &no_digits;
    EXPECT(62, input_device_open(&config, &device) == CORE_INVALID_PATH);

    String prefix_only = make_str("/dev/input/event");
    config.path = &prefix_only;
    EXPECT(63, input_device_open(&config, &device) == CORE_INVALID_PATH);

    InputDeviceConfig null_path;
    null_path.path = NULL;
    EXPECT(64, input_device_open(&null_path, &device) == CORE_INVALID_ARGUMENT);

    destroy_str(&empty);
    destroy_str(&wrong_dir);
    destroy_str(&no_digits);
    destroy_str(&prefix_only);
}

/* Issues 2 and 3: a frame without its SYN_REPORT is not handed over, and the
 * events read past a returned frame are not thrown away. Both need a real
 * device, so they only run when one can be opened. */
/* --- driven through tests/input_device_fake.c, so no device is needed --- */

/* Control surface of the fake. */
void fake_input_forget(void);
void fake_input_set_capabilities(InputRawCapabilityMask capabilities);
void fake_input_queue(const InputRawEvent *events, usize count);
void fake_input_lose_device(void);
usize fake_input_pending(void);

#define FAKE_KEY_A ((u16)30)
#define FAKE_KEY_B ((u16)48)

static InputRawEvent raw_key(u16 code, i32 value)
{
    InputRawEvent event;

    event.type = INPUT_RAW_EVENT_KEY;
    event.code = code;
    event.value = value;
    event.timestamp = 0;
    return event;
}

static InputRawEvent raw_syn(u16 code)
{
    InputRawEvent event;

    event.type = INPUT_RAW_EVENT_SYN;
    event.code = code;
    event.value = 0;
    event.timestamp = 0;
    return event;
}

static InputDeviceHandle open_fake_device(void)
{
    String path = make_str(TEST_DEVICE_PATH);
    InputDeviceHandle device = NULL;
    InputDeviceConfig config;

    config.path = &path;
    (void)input_device_open(&config, &device);
    destroy_str(&path);
    return device;
}

/*
 * Issue 2: events read without their SYN_REPORT are not a frame. The old code
 * reset its state at the start of every call and handed the accumulated events
 * over as soon as a read came back empty, so a caller received a frame that
 * was never terminated. The partial frame has to survive until the kernel
 * finishes it.
 */
static void test_partial_frame_is_not_handed_over(void)
{
    InputDeviceHandle device = open_fake_device();
    InputReadStatus status;
    InputFrame frame;
    InputRawEvent events[1];

    EXPECT(100, device != NULL);

    /* KEY_A with no SYN_REPORT yet. */
    events[0] = raw_key(FAKE_KEY_A, 1);
    fake_input_queue(events, 1);

    EXPECT(101, input_device_read_frame(device, &status, &frame) == CORE_OK);
    EXPECT(102, status == INPUT_READ_STATUS_NO_FRAME);

    /* Still nothing new: the same partial frame must not be flushed either. */
    EXPECT(103, input_device_read_frame(device, &status, &frame) == CORE_OK);
    EXPECT(104, status == INPUT_READ_STATUS_NO_FRAME);

    /* Now the kernel terminates it, and the earlier KEY_A comes with it. */
    events[0] = raw_syn(INPUT_RAW_SYN_REPORT);
    fake_input_queue(events, 1);

    EXPECT(105, input_device_read_frame(device, &status, &frame) == CORE_OK);
    EXPECT(106, status == INPUT_READ_STATUS_FRAME_READY);
    EXPECT(107, frame.count == 2);
    EXPECT(108, frame.events[0].kind == INPUT_EVENT_KIND_KEY);
    EXPECT(109, frame.events[0].code == FAKE_KEY_A);
    EXPECT(110, frame.events[1].kind == INPUT_EVENT_KIND_SYNC_REPORT);
    EXPECT(111, input_frame_destroy(&frame) == CORE_OK);

    EXPECT(112, input_device_close(&device) == CORE_OK);
}

/*
 * Issue 3: one read can carry several frames. The old code returned as soon as
 * it saw the first SYN_REPORT and dropped the rest of the batch, so the second
 * frame never reached the caller. KEY_A/SYN_REPORT/KEY_B/SYN_REPORT must come
 * back as two frames, in that order, from two calls.
 */
static void test_two_frames_in_one_batch(void)
{
    InputDeviceHandle device = open_fake_device();
    InputReadStatus status;
    InputFrame frame;
    InputRawEvent events[4];

    events[0] = raw_key(FAKE_KEY_A, 1);
    events[1] = raw_syn(INPUT_RAW_SYN_REPORT);
    events[2] = raw_key(FAKE_KEY_B, 1);
    events[3] = raw_syn(INPUT_RAW_SYN_REPORT);
    fake_input_queue(events, 4);

    /* First call: the frame ending at the first SYN_REPORT. */
    EXPECT(120, input_device_read_frame(device, &status, &frame) == CORE_OK);
    EXPECT(121, status == INPUT_READ_STATUS_FRAME_READY);
    EXPECT(122, frame.count == 2);
    EXPECT(123, frame.events[0].code == FAKE_KEY_A);
    EXPECT(124, input_frame_destroy(&frame) == CORE_OK);

    /* Second call: the frame that was sitting behind it in the same batch.
     * With the old code this came back as NoFrame and KEY_B was lost. */
    EXPECT(125, input_device_read_frame(device, &status, &frame) == CORE_OK);
    EXPECT(126, status == INPUT_READ_STATUS_FRAME_READY);
    EXPECT(127, frame.count == 2);
    EXPECT(128, frame.events[0].code == FAKE_KEY_B);
    EXPECT(129, input_frame_destroy(&frame) == CORE_OK);

    /* Nothing is left over. */
    EXPECT(130, fake_input_pending() == 0);
    EXPECT(131, input_device_read_frame(device, &status, &frame) == CORE_OK);
    EXPECT(132, status == INPUT_READ_STATUS_NO_FRAME);

    EXPECT(133, input_device_close(&device) == CORE_OK);
}

/*
 * Contract 12: after SYN_DROPPED the frame is a single SyncLost event, and the
 * events the kernel also drops - everything up to the next SYN_REPORT - must
 * not leak into the next frame.
 */
static void test_syn_dropped_discards_until_resync(void)
{
    InputDeviceHandle device = open_fake_device();
    InputReadStatus status;
    InputFrame frame;
    InputRawEvent events[4];

    events[0] = raw_key(FAKE_KEY_A, 1);
    events[1] = raw_syn(INPUT_RAW_SYN_DROPPED);
    events[2] = raw_key(FAKE_KEY_B, 1);
    events[3] = raw_syn(INPUT_RAW_SYN_REPORT);
    fake_input_queue(events, 4);

    /* The unterminated KEY_A is dropped and reported as SyncLost. */
    EXPECT(140, input_device_read_frame(device, &status, &frame) == CORE_OK);
    EXPECT(141, status == INPUT_READ_STATUS_FRAME_READY);
    EXPECT(142, frame.count == 1);
    EXPECT(143, frame.events[0].kind == INPUT_EVENT_KIND_SYNC_LOST);
    EXPECT(144, frame.events[0].code == INPUT_RAW_SYN_DROPPED);
    EXPECT(145, input_frame_destroy(&frame) == CORE_OK);

    /* KEY_B was after the drop and before the resync, so it belongs to no
     * frame: this call finds nothing. */
    EXPECT(146, input_device_read_frame(device, &status, &frame) == CORE_OK);
    EXPECT(147, status == INPUT_READ_STATUS_NO_FRAME);

    /* Past the resync the device works again. */
    events[0] = raw_key(FAKE_KEY_A, 1);
    events[1] = raw_syn(INPUT_RAW_SYN_REPORT);
    fake_input_queue(events, 2);

    EXPECT(148, input_device_read_frame(device, &status, &frame) == CORE_OK);
    EXPECT(149, status == INPUT_READ_STATUS_FRAME_READY);
    EXPECT(150, frame.count == 2);
    EXPECT(151, frame.events[0].code == FAKE_KEY_A);
    EXPECT(152, input_frame_destroy(&frame) == CORE_OK);

    EXPECT(153, input_device_close(&device) == CORE_OK);
}

/* Contract 8: a lost device is reported once, then only close is allowed. */
static void test_device_lost(void)
{
    InputDeviceHandle device = open_fake_device();
    InputReadStatus status;
    InputFrame frame;

    EXPECT(160, device != NULL);

    fake_input_lose_device();
    EXPECT(161, input_device_read_frame(device, &status, &frame) == CORE_OK);
    EXPECT(162, status == INPUT_READ_STATUS_DEVICE_LOST);

    EXPECT(163, input_device_close(&device) == CORE_OK);
    EXPECT(164, device == NULL);
}

/*
 * Issue 14b: a device whose only key codes are buttons must not be reported as
 * a keyboard, which is what the single INPUT_RAW_CAP_KEY flag used to cause.
 */
static void test_device_classes(void)
{
    String path = make_str(TEST_DEVICE_PATH);
    InputDeviceHandle device = NULL;
    InputDeviceConfig config;
    InputDeviceInfo info;

    config.path = &path;

    /* Buttons and relative axes: a pointer, not a keyboard. */
    fake_input_set_capabilities(INPUT_RAW_CAP_KEY | INPUT_RAW_CAP_POINTER_BUTTONS |
                                INPUT_RAW_CAP_RELATIVE);
    if (input_device_open(&config, &device) == CORE_OK) {
        EXPECT(170, input_device_get_info(device, &info) == CORE_OK);
        EXPECT(171, (info.classes & INPUT_DEVICE_POINTER) != 0);
        EXPECT(172, (info.classes & INPUT_DEVICE_KEYBOARD) == 0);
        EXPECT(173, input_device_close(&device) == CORE_OK);
    }
    fake_input_forget();

    /* Key codes below BTN_MISC: a keyboard. */
    fake_input_set_capabilities(INPUT_RAW_CAP_KEY | INPUT_RAW_CAP_KEYBOARD_KEYS);
    if (input_device_open(&config, &device) == CORE_OK) {
        EXPECT(174, input_device_get_info(device, &info) == CORE_OK);
        EXPECT(175, (info.classes & INPUT_DEVICE_KEYBOARD) != 0);
        EXPECT(176, (info.classes & INPUT_DEVICE_POINTER) == 0);
        EXPECT(177, input_device_close(&device) == CORE_OK);
    }
    fake_input_forget();

    /* Absolute axes: touch. */
    fake_input_set_capabilities(INPUT_RAW_CAP_ABSOLUTE);
    if (input_device_open(&config, &device) == CORE_OK) {
        EXPECT(178, input_device_get_info(device, &info) == CORE_OK);
        EXPECT(179, (info.classes & INPUT_DEVICE_TOUCH) != 0);
        EXPECT(180, (info.classes & INPUT_DEVICE_KEYBOARD) == 0);
        EXPECT(181, input_device_close(&device) == CORE_OK);
    }
    fake_input_forget();

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
    test_path_validation();

    test_partial_frame_is_not_handed_over();
    test_two_frames_in_one_batch();
    test_syn_dropped_discards_until_resync();
    test_device_lost();
    test_device_classes();

    check_passed();
    return 0;
}
