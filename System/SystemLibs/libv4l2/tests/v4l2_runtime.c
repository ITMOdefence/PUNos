/*
 * Runtime test of the libv4l2 operations against DevDoc/SystemLibs/libv4l2.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 * This library is inherently hardware-bound: the test always exercises the
 * CORE_NOT_FOUND and NULL-argument paths, then requires a readable/writable
 * /dev/video* device (a real capture device, or a virtual one such as
 * v4l2loopback) and checks the full open/configure/start/dequeue/stop/close
 * success path against it. Check 19 fails when no usable device is present,
 * so a passing QEMU self-test proves that the hardware-backed path ran.
 */
#define CHECK_TEST_NAME "libv4l2"
#include "../../testing/check.h"

#include <sys/v4l2.h>

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

    EXPECT(19, find_candidate_device(path_buffer));

    path = str(path_buffer);
    config.path = &path;
    EXPECT(20, v4l2_open(&config, &device) == CORE_OK && device != NULL);

    EXPECT(21, v4l2_get_fd(device, &fd) == CORE_OK && fd >= 0);
    EXPECT(22, v4l2_get_fd(device, NULL) == CORE_INVALID_ARGUMENT);

    EXPECT(23, v4l2_get_capture_configurations(device, &configurations) == CORE_OK);

    if (configurations.count == 0) {
        EXPECT(24, v4l2_stream_config_list_destroy(&configurations) == CORE_OK);
        EXPECT(25, v4l2_close(&device) == CORE_OK);
        destroy(&path);
        return; /* nothing discrete to configure capture with */
    }

    {
        V4l2StreamConfig requested = configurations.items[0];
        V4l2StreamConfig actual;
        V4l2StreamConfig invalid_request = requested;

        EXPECT(26, requested.max_frame_bytes > 0);
        requested.max_frame_bytes = 0; /* a request must have max_frame_bytes == 0 */

        EXPECT(27, v4l2_configure_capture(device, &requested, &actual) == CORE_OK);
        EXPECT(28, actual.max_frame_bytes > 0);
        EXPECT(29, v4l2_configure_capture(device, &invalid_request, &actual) ==
                       CORE_INVALID_ARGUMENT); /* invalid_request.max_frame_bytes != 0 */

        EXPECT(30, v4l2_start_capture(device) == CORE_OK);
        EXPECT(31, v4l2_start_capture(NULL) == CORE_INVALID_ARGUMENT);

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
                EXPECT(32, v4l2_dequeue_buffer(device, &status, &buffer) == CORE_OK);
                if (status == V4L2_DEQUEUE_STATUS_BUFFER_READY) {
                    got_buffer_ready = true;
                    EXPECT(33, buffer.index < 64); /* sanity bound: the true buffer count is internal */
                    EXPECT(34, v4l2_queue_buffer(device, &buffer) == CORE_OK);
                    EXPECT(35, buffer.data.data == NULL && buffer.data.size == 0);
                    break;
                }
                EXPECT(36, status == V4L2_DEQUEUE_STATUS_NO_BUFFER);
                {
                    DurationMs remaining;

                    (void)time_sleep(20, &remaining);
                }
            }
            (void)got_buffer_ready; /* informational only: both outcomes are valid */

            EXPECT(37, v4l2_dequeue_buffer(device, NULL, &buffer) == CORE_INVALID_ARGUMENT);
            EXPECT(38, v4l2_dequeue_buffer(device, &status, NULL) == CORE_INVALID_ARGUMENT);
            EXPECT(39, v4l2_queue_buffer(device, NULL) == CORE_INVALID_ARGUMENT);
        }

        EXPECT(40, v4l2_stop_capture(device) == CORE_OK);
        EXPECT(41, v4l2_stop_capture(NULL) == CORE_INVALID_ARGUMENT);
    }

    EXPECT(42, v4l2_stream_config_list_destroy(&configurations) == CORE_OK);
    EXPECT(43, configurations.items == NULL && configurations.count == 0);

    EXPECT(44, v4l2_close(&device) == CORE_OK);
    EXPECT(45, device == NULL);
    destroy(&path);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    test_missing_device_and_arguments();
    test_real_device();

    check_passed();
    return 0;
}
