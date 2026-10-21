/*
 * Runtime test of the libv4l2 operations against DevDoc/SystemLibs/libv4l2.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 * This library is inherently hardware-bound: the test always exercises the
 * CORE_NOT_FOUND and NULL-argument paths (no device needed), and adapts to
 * whatever /dev/video* it can find:
 *   - none present, or present but not readable/writable by this process:
 *     only the not-found/argument checks run;
 *   - present and usable (a real capture device, or a virtual one such as
 *     v4l2loopback): the full open/configure/start/dequeue/stop/close
 *     success path is checked against it.
 */
#define CHECK_TEST_NAME "libv4l2"

/* The one node tests/v4l2_kernel_fake.c pretends to have behind it. */
#define FAKE_NODE "/dev/video0"
#include "../../testing/check.h"

#include <sys/v4l2.h>

static String str(const char *text)
{
    String result;

    if (string_from_c_str(text, &result) != CORE_OK) {
    }
    return result;
}

static void destroy(String *value)
{
    (void)string_destroy(value);
}

/* Formats "/dev/video<index>" without sprintf(), which PUNos has no wrapper
 * for. buffer must be at least 32 bytes. */
static void format_video_path(char *buffer, i32 index)
{
    static const char prefix[] = "/dev/video";
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

/* Finds the first /dev/videoN this process can open for read/write.
 * buffer must be at least 32 bytes. A V4L2 capture device needs O_RDWR,
 * unlike /dev/input's O_RDONLY, so a permission-denied node cannot be told
 * apart from a missing one the way libinputdevice's test does; both are
 * equally "nothing more to check here" for this library. */
static bool find_candidate_device(char *buffer)
{
    i32 index;

    for (index = 0; index < 16; ++index) {
        i32 probe_fd;

        format_video_path(buffer, index);
        probe_fd = open(buffer, O_RDWR | O_NONBLOCK, 0);
        if (probe_fd >= 0) {
            (void)close(probe_fd);
            return true;
        }
    }
    return false;
}

static void test_missing_device_and_arguments(void)
{
    String bogus_path = str("/dev/video_does_not_exist_9999");
    V4l2DeviceConfig config;
    V4l2DeviceHandle device = NULL;
    i32 fd = -1;

    EXPECT(1, v4l2_open(NULL, &device) == CORE_INVALID_ARGUMENT);
    config.path = &bogus_path;
    EXPECT(2, v4l2_open(&config, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(3, v4l2_open(&config, &device) == CORE_NOT_FOUND);
    EXPECT(4, device == NULL);

    EXPECT(5, v4l2_get_fd(NULL, &fd) == CORE_INVALID_ARGUMENT);
    EXPECT(6, v4l2_close(NULL) == CORE_INVALID_ARGUMENT);
    {
        V4l2DeviceHandle already_null = NULL;

        EXPECT(7, v4l2_close(&already_null) == CORE_INVALID_ARGUMENT);
    }
    {
        V4l2StreamConfigList list;

        EXPECT(8, v4l2_get_capture_configurations(NULL, &list) == CORE_INVALID_ARGUMENT);
        EXPECT(9, v4l2_stream_config_list_destroy(NULL) == CORE_INVALID_ARGUMENT);
    }
    {
        V4l2StreamConfig requested;
        V4l2StreamConfig actual;

        requested.pixel_format = 0;
        requested.width = V4L2_MIN_WIDTH;
        requested.height = V4L2_MIN_HEIGHT;
        requested.frames_per_second = V4L2_MIN_FRAMES_PER_SECOND;
        requested.max_frame_bytes = 0;
        requested.frame_rate_numerator = 0;
        requested.frame_rate_denominator = 0;
        EXPECT(10, v4l2_configure_capture(NULL, &requested, &actual) == CORE_INVALID_ARGUMENT);
        /* max_frame_bytes must be 0 in a request (contract 11), device NULL
         * still short-circuits first, so this specifically needs no device. */
        requested.max_frame_bytes = 1;
        EXPECT(11, v4l2_configure_capture(NULL, &requested, &actual) == CORE_INVALID_ARGUMENT);
    }
    EXPECT(12, v4l2_start_capture(NULL) == CORE_INVALID_ARGUMENT);
    {
        V4l2DequeueStatus status;
        V4l2Buffer buffer;

        EXPECT(13, v4l2_dequeue_buffer(NULL, &status, &buffer) == CORE_INVALID_ARGUMENT);
        EXPECT(14, v4l2_queue_buffer(NULL, &buffer) == CORE_INVALID_ARGUMENT);
    }
    EXPECT(15, v4l2_stop_capture(NULL) == CORE_INVALID_ARGUMENT);

    destroy(&bogus_path);
}

static void test_real_device(void)
{
    char path_buffer[32];
    String path;
    V4l2DeviceConfig config;
    V4l2DeviceHandle device = NULL;
    i32 fd = -1;
    V4l2StreamConfigList configurations;

    if (!find_candidate_device(path_buffer)) {
        return; /* no usable /dev/video* on this machine */
    }

    path = str(path_buffer);
    config.path = &path;
    EXPECT(16, v4l2_open(&config, &device) == CORE_OK && device != NULL);

    EXPECT(17, v4l2_get_fd(device, &fd) == CORE_OK && fd >= 0);
    EXPECT(18, v4l2_get_fd(device, NULL) == CORE_INVALID_ARGUMENT);

    ErrorCode enumeration = v4l2_get_capture_configurations(device, &configurations);
    EXPECT(19, enumeration == CORE_OK || enumeration == CORE_UNSUPPORTED);
    if (enumeration == CORE_UNSUPPORTED) {
        EXPECT(122, v4l2_close(&device) == CORE_OK);
        destroy(&path);
        return;
    }

    if (configurations.count == 0) {
        EXPECT(20, v4l2_stream_config_list_destroy(&configurations) == CORE_OK);
        EXPECT(21, v4l2_close(&device) == CORE_OK);
        destroy(&path);
        return; /* nothing discrete to configure capture with */
    }

    {
        V4l2StreamConfig requested = configurations.items[0];
        V4l2StreamConfig actual;
        V4l2StreamConfig invalid_request = requested;

        EXPECT(22, requested.max_frame_bytes > 0);
        requested.max_frame_bytes = 0; /* a request must have max_frame_bytes == 0 */

        ErrorCode configured = v4l2_configure_capture(device, &requested, &actual);
        EXPECT(23, configured == CORE_OK || configured == CORE_UNSUPPORTED);
        if (configured == CORE_UNSUPPORTED) {
            EXPECT(123, v4l2_stream_config_list_destroy(&configurations) == CORE_OK);
            EXPECT(124, v4l2_close(&device) == CORE_OK);
            destroy(&path);
            return;
        }
        EXPECT(24, actual.max_frame_bytes > 0);
        EXPECT(25, v4l2_configure_capture(device, &invalid_request, &actual) ==
                       CORE_INVALID_ARGUMENT); /* invalid_request.max_frame_bytes != 0 */

        EXPECT(26, v4l2_start_capture(device) == CORE_OK);
        EXPECT(27, v4l2_start_capture(NULL) == CORE_INVALID_ARGUMENT);

        {
            V4l2DequeueStatus status;
            V4l2Buffer buffer;
            i32 attempt;
            bool got_buffer_ready = false;

            /* A non-blocking dequeue must return promptly either way; try a
             * few times in case a real driver needs a moment to produce a
             * frame (a virtual one like v4l2loopback with no writer never
             * will, and that is fine too: NoBuffer is an accepted outcome). */
            for (attempt = 0; attempt < 20; ++attempt) {
                EXPECT(28, v4l2_dequeue_buffer(device, &status, &buffer) == CORE_OK);
                if (status == V4L2_DEQUEUE_STATUS_BUFFER_READY) {
                    got_buffer_ready = true;
                    EXPECT(29, buffer.index < 64); /* sanity bound: the true buffer count is internal */
                    EXPECT(30, v4l2_queue_buffer(device, &buffer) == CORE_OK);
                    EXPECT(31, buffer.data.data == NULL && buffer.data.size == 0);
                    break;
                }
                EXPECT(32, status == V4L2_DEQUEUE_STATUS_NO_BUFFER);
                {
                    DurationMs remaining;

                    (void)time_sleep(20, &remaining);
                }
            }
            (void)got_buffer_ready; /* informational only: both outcomes are valid */

            EXPECT(33, v4l2_dequeue_buffer(device, NULL, &buffer) == CORE_INVALID_ARGUMENT);
            EXPECT(34, v4l2_dequeue_buffer(device, &status, NULL) == CORE_INVALID_ARGUMENT);
            EXPECT(35, v4l2_queue_buffer(device, NULL) == CORE_INVALID_ARGUMENT);
        }

        EXPECT(36, v4l2_stop_capture(device) == CORE_OK);
        EXPECT(37, v4l2_stop_capture(NULL) == CORE_INVALID_ARGUMENT);
    }

    EXPECT(38, v4l2_stream_config_list_destroy(&configurations) == CORE_OK);
    EXPECT(39, configurations.items == NULL && configurations.count == 0);

    EXPECT(40, v4l2_close(&device) == CORE_OK);
    EXPECT(41, device == NULL);
    destroy(&path);
}

/*
 * Driven through tests/v4l2_kernel_fake.c, so these run without a camera.
 * Control surface of the fake.
 */
void fake_v4l2_reset(void);
void fake_v4l2_set_device_gone(bool gone);
void fake_v4l2_set_not_v4l2(bool value);
void fake_v4l2_set_cannot_stream(bool value);
void fake_v4l2_arm_frame(bool ready);
void fake_v4l2_fail_queue(i32 index);
void fake_v4l2_set_fractional_rate(bool value);
u32 fake_v4l2_requested_interval_numerator(void);
u32 fake_v4l2_requested_interval_denominator(void);
u32 fake_v4l2_streamon_calls(void);

static String fake_node_path(void)
{
    return str(FAKE_NODE);
}

/* Issue 14a: camera_open() promises to refuse anything that is not a V4L2
 * video capture device. The check is VIDIOC_QUERYCAP, so it has to be asked
 * for: a file that answers ENOTTY is not a V4L2 device, and a device without
 * VIDEO_CAPTURE and STREAMING cannot do what this library does.
 */
static void test_querycap_validation(void)
{
    V4l2DeviceConfig config;
    V4l2DeviceHandle device = NULL;
    String path = fake_node_path();

    config.path = &path;

    fake_v4l2_reset();
    EXPECT(42, v4l2_open(&config, &device) == CORE_OK);
    EXPECT(43, device != NULL);
    EXPECT(44, v4l2_close(&device) == CORE_OK);

    fake_v4l2_reset();
    fake_v4l2_set_not_v4l2(true);
    device = NULL;
    EXPECT(45, v4l2_open(&config, &device) == CORE_UNSUPPORTED);
    EXPECT(46, device == NULL);

    fake_v4l2_reset();
    fake_v4l2_set_cannot_stream(true);
    device = NULL;
    EXPECT(47, v4l2_open(&config, &device) == CORE_UNSUPPORTED);
    EXPECT(48, device == NULL);

    fake_v4l2_reset();
    fake_v4l2_set_device_gone(true);
    device = NULL;
    EXPECT(49, v4l2_open(&config, &device) == CORE_NOT_FOUND);
    EXPECT(50, device == NULL);

    fake_v4l2_reset();
    destroy(&path);
}

/*
 * Issue 6: on ENODEV the driver takes the buffers it had handed out with it,
 * so they can never be queued back. libv4l2 used to keep counting them, and
 * v4l2_close() then refused with CORE_INVALID_STATE and leaked the handle -
 * which left camlib holding a pointer it had already forgotten.
 */
static void test_device_lost_with_outstanding_buffers(void)
{
    V4l2DeviceConfig config;
    V4l2DeviceHandle device = NULL;
    V4l2StreamConfig requested;
    V4l2StreamConfig actual;
    V4l2DequeueStatus status;
    V4l2Buffer buffer;
    String path = fake_node_path();

    config.path = &path;

    fake_v4l2_reset();
    EXPECT(51, v4l2_open(&config, &device) == CORE_OK);

    requested.pixel_format = 0;
    requested.width = 640;
    requested.height = 480;
    requested.frames_per_second = 30;
    requested.max_frame_bytes = 0;
    requested.frame_rate_numerator = 0;
    requested.frame_rate_denominator = 0;

    EXPECT(52, v4l2_configure_capture(device, &requested, &actual) == CORE_OK);
    EXPECT(53, v4l2_start_capture(device) == CORE_OK);

    /* Take a buffer and never give it back. */
    fake_v4l2_arm_frame(true);
    EXPECT(54, v4l2_dequeue_buffer(device, &status, &buffer) == CORE_OK);
    EXPECT(55, status == V4L2_DEQUEUE_STATUS_BUFFER_READY);

    /* While the device is healthy the outstanding buffer blocks the close,
     * which is what contract 9 asks for. */
    EXPECT(56, v4l2_close(&device) == CORE_INVALID_STATE);

    /* The device goes away. */
    fake_v4l2_set_device_gone(true);
    EXPECT(57, v4l2_dequeue_buffer(device, &status, &buffer) == CORE_OK);
    EXPECT(58, status == V4L2_DEQUEUE_STATUS_DEVICE_LOST);

    /* The buffer went with the driver, so the outstanding count has to be
     * cleared. What matters is that close() is not refused for it: before the
     * fix this returned CORE_INVALID_STATE and left *device pointing at a
     * handle nobody could reach any more. With the device still gone the
     * cleanup ioctls fail as well, so the code is an error - but never
     * CORE_INVALID_STATE, and the handle is released either way. */
    EXPECT(59, v4l2_close(&device) != CORE_INVALID_STATE);
    EXPECT(60, device == NULL);

    /* Same again, but with the device reachable again, so the whole close
     * succeeds outright. */
    fake_v4l2_reset();
    device = NULL;
    EXPECT(61, v4l2_open(&config, &device) == CORE_OK);
    EXPECT(62, v4l2_configure_capture(device, &requested, &actual) == CORE_OK);
    EXPECT(63, v4l2_start_capture(device) == CORE_OK);
    fake_v4l2_arm_frame(true);
    EXPECT(64, v4l2_dequeue_buffer(device, &status, &buffer) == CORE_OK);
    EXPECT(65, status == V4L2_DEQUEUE_STATUS_BUFFER_READY);
    fake_v4l2_set_device_gone(true);
    EXPECT(66, v4l2_dequeue_buffer(device, &status, &buffer) == CORE_OK);
    EXPECT(67, status == V4L2_DEQUEUE_STATUS_DEVICE_LOST);
    fake_v4l2_set_device_gone(false);
    EXPECT(68, v4l2_close(&device) == CORE_OK);
    EXPECT(69, device == NULL);

    fake_v4l2_reset();
    destroy(&path);
}

static void test_abort_after_queue_failure(void)
{
    String path = fake_node_path();
    V4l2DeviceConfig config = { &path };
    V4l2DeviceHandle device = NULL;
    V4l2StreamConfig requested = { 0, 640, 480, 30, 0, 0, 0 }, actual;
    V4l2DequeueStatus status;
    V4l2Buffer buffer;
    fake_v4l2_reset();
    EXPECT(110, v4l2_abort(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(111, v4l2_open(&config, &device) == CORE_OK);
    EXPECT(112, v4l2_configure_capture(device, &requested, &actual) == CORE_OK);
    EXPECT(113, v4l2_start_capture(device) == CORE_OK);
    fake_v4l2_arm_frame(true);
    EXPECT(114, v4l2_dequeue_buffer(device, &status, &buffer) == CORE_OK &&
                status == V4L2_DEQUEUE_STATUS_BUFFER_READY);
    fake_v4l2_fail_queue((i32)buffer.index);
    EXPECT(115, v4l2_queue_buffer(device, &buffer) == CORE_IO_ERROR);
    EXPECT(116, v4l2_close(&device) == CORE_INVALID_STATE && device != NULL);
    EXPECT(117, v4l2_abort(&device) == CORE_OK && device == NULL);
    fake_v4l2_reset();
    destroy(&path);
}

static void test_fractional_rate_is_not_truncated(void)
{
    String path = fake_node_path();
    V4l2DeviceConfig config = { &path };
    V4l2DeviceHandle device = NULL;
    V4l2StreamConfig requested, actual;
    V4l2StreamConfigList list;
    fake_v4l2_reset();
    fake_v4l2_set_fractional_rate(true);
    EXPECT(118, v4l2_open(&config, &device) == CORE_OK);
    EXPECT(119, v4l2_get_capture_configurations(device, &list) == CORE_OK &&
                list.count == 1);
    EXPECT(120, list.items[0].frame_rate_numerator == 30000 &&
                list.items[0].frame_rate_denominator == 1001 &&
                list.items[0].frames_per_second == 0);
    requested = list.items[0];
    requested.max_frame_bytes = 0;
    requested.frame_rate_denominator = 0;
    EXPECT(129, v4l2_configure_capture(device, &requested, &actual) == CORE_INVALID_ARGUMENT);
    requested.frame_rate_denominator = 1001;
    requested.frames_per_second = 29;
    EXPECT(130, v4l2_configure_capture(device, &requested, &actual) == CORE_INVALID_ARGUMENT);
    requested.frames_per_second = 0;
    EXPECT(121, v4l2_configure_capture(device, &requested, &actual) == CORE_OK);
    EXPECT(125, fake_v4l2_requested_interval_numerator() == 1001 &&
                fake_v4l2_requested_interval_denominator() == 30000);
    EXPECT(126, actual.frame_rate_numerator == 30000 &&
                actual.frame_rate_denominator == 1001 && actual.frames_per_second == 0);
    EXPECT(127, v4l2_stream_config_list_destroy(&list) == CORE_OK);
    EXPECT(128, v4l2_close(&device) == CORE_OK && device == NULL);
    fake_v4l2_reset();
    destroy(&path);
}

/* A start has to be refused when there are no buffers, and accepted once
 * v4l2_configure_capture() has mapped some. */
static void test_start_requires_configuration(void)
{
    V4l2DeviceConfig config;
    V4l2DeviceHandle device = NULL;
    V4l2StreamConfig requested;
    V4l2StreamConfig actual;
    String path = fake_node_path();

    config.path = &path;

    fake_v4l2_reset();
    EXPECT(70, v4l2_open(&config, &device) == CORE_OK);
    EXPECT(71, v4l2_start_capture(device) == CORE_INVALID_STATE);

    requested.pixel_format = 0;
    requested.width = 640;
    requested.height = 480;
    requested.frames_per_second = 30;
    requested.max_frame_bytes = 0;
    requested.frame_rate_numerator = 0;
    requested.frame_rate_denominator = 0;
    EXPECT(72, v4l2_configure_capture(device, &requested, &actual) == CORE_OK);
    EXPECT(73, actual.max_frame_bytes > 0);
    EXPECT(74, v4l2_start_capture(device) == CORE_OK);
    EXPECT(75, fake_v4l2_streamon_calls() == 1);

    EXPECT(76, v4l2_stop_capture(device) == CORE_OK);
    /* stop gives the buffers back, so streaming cannot start again without a
     * fresh configure: this is what camlib's state table has to mirror. */
    EXPECT(77, v4l2_start_capture(device) == CORE_INVALID_STATE);
    EXPECT(78, fake_v4l2_streamon_calls() == 1);

    EXPECT(79, v4l2_configure_capture(device, &requested, &actual) == CORE_OK);
    EXPECT(80, v4l2_start_capture(device) == CORE_OK);
    EXPECT(81, fake_v4l2_streamon_calls() == 2);

    EXPECT(82, v4l2_stop_capture(device) == CORE_OK);
    EXPECT(83, v4l2_close(&device) == CORE_OK);

    fake_v4l2_reset();
    destroy(&path);
}

void fake_v4l2_buffer(u32 index, u32 size);
void fake_v4l2_set_close_fails(bool value);

static void test_driver_buffer_bounds_and_saved_error(void)
{
    String path = fake_node_path();
    V4l2DeviceConfig config = { &path };
    V4l2DeviceHandle device = NULL;
    V4l2StreamConfig requested = { 0, 640, 480, 30, 0, 0, 0 }, actual;
    V4l2DequeueStatus status;
    V4l2Buffer buffer;
    fake_v4l2_reset();
    EXPECT(84, v4l2_open(&config, &device) == CORE_OK);
    EXPECT(85, v4l2_configure_capture(device, &requested, &actual) == CORE_OK);
    EXPECT(86, v4l2_start_capture(device) == CORE_OK);
    fake_v4l2_buffer(V4L2_BUFFER_COUNT, 1);
    EXPECT(87, v4l2_dequeue_buffer(device, &status, &buffer) == CORE_IO_ERROR);
    fake_v4l2_buffer(0, actual.max_frame_bytes + 1);
    EXPECT(88, v4l2_dequeue_buffer(device, &status, &buffer) == CORE_IO_ERROR);
    EXPECT(89, v4l2_stop_capture(device) == CORE_OK);
    EXPECT(90, v4l2_close(&device) == CORE_OK);
    fake_v4l2_set_device_gone(true);
    fake_v4l2_set_close_fails(true);
    EXPECT(91, v4l2_open(&config, &device) == CORE_NOT_FOUND);
    fake_v4l2_reset();
    destroy(&path);
}

void fake_v4l2_fail_streamoff(bool value);
void fake_v4l2_buffer_count(u32 count);

static void test_capture_failure_lifecycle(void)
{
    String path = fake_node_path();
    V4l2DeviceConfig config = { &path };
    V4l2DeviceHandle device = NULL;
    V4l2StreamConfig requested = { 0, 640, 480, 30, 0, 0, 0 }, actual;
    V4l2DequeueStatus status;
    V4l2Buffer buffer, stale;
    fake_v4l2_reset();
    EXPECT(92, v4l2_open(&config, &device) == CORE_OK);
    EXPECT(93, v4l2_dequeue_buffer(device, &status, &buffer) == CORE_INVALID_STATE);
    fake_v4l2_buffer_count(6); /* Driver may allocate more than requested. */
    EXPECT(94, v4l2_configure_capture(device, &requested, &actual) == CORE_OK);
    fake_v4l2_fail_queue(2);
    EXPECT(95, v4l2_start_capture(device) == CORE_IO_ERROR);
    EXPECT(96, v4l2_start_capture(device) == CORE_OK);
    EXPECT(97, v4l2_start_capture(device) == CORE_INVALID_STATE);
    EXPECT(98, v4l2_configure_capture(device, &requested, &actual) == CORE_INVALID_STATE);
    fake_v4l2_arm_frame(true);
    EXPECT(99, v4l2_dequeue_buffer(device, &status, &buffer) == CORE_OK &&
               status == V4L2_DEQUEUE_STATUS_BUFFER_READY);
    stale = buffer;
    EXPECT(100, v4l2_queue_buffer(device, &buffer) == CORE_OK);
    EXPECT(101, v4l2_queue_buffer(device, &stale) == CORE_INVALID_ARGUMENT);
    fake_v4l2_arm_frame(true);
    EXPECT(102, v4l2_dequeue_buffer(device, &status, &buffer) == CORE_OK);
    EXPECT(103, v4l2_queue_buffer(device, &stale) == CORE_INVALID_ARGUMENT);
    EXPECT(104, v4l2_stop_capture(device) == CORE_INVALID_STATE);
    EXPECT(105, v4l2_queue_buffer(device, &buffer) == CORE_OK);
    fake_v4l2_fail_streamoff(true);
    EXPECT(106, v4l2_stop_capture(device) == CORE_IO_ERROR);
    fake_v4l2_arm_frame(true);
    EXPECT(107, v4l2_dequeue_buffer(device, &status, &buffer) == CORE_OK &&
                status == V4L2_DEQUEUE_STATUS_BUFFER_READY && buffer.data.data != NULL);
    EXPECT(108, v4l2_queue_buffer(device, &buffer) == CORE_OK);
    /* close must stop DMA even if STREAMOFF continues to fail. */
    EXPECT(109, v4l2_close(&device) == CORE_OK && device == NULL);
    fake_v4l2_reset();
    destroy(&path);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    test_missing_device_and_arguments();
    test_real_device();
    test_querycap_validation();
    test_device_lost_with_outstanding_buffers();
    test_abort_after_queue_failure();
    test_fractional_rate_is_not_truncated();
    test_start_requires_configuration();
    test_driver_buffer_bounds_and_saved_error();
    test_capture_failure_lifecycle();

    check_passed();
    return 0;
}
