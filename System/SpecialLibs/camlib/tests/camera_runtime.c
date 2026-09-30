/*
 * Runtime test of the camlib operations against
 * DevDoc/SpecialLibs/camlib.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 *
 * The test needs a real V4L2 capture device, which neither the host build nor
 * the PUNos initramfs is guaranteed to have. /dev/video0 is used when it is
 * present and openable; otherwise the checks that need a device are skipped
 * and only argument and state validation is verified. The state checks run
 * without a device too, by driving a handle that was never configured.
 *
 * Check number groups:
 *   1-5    camera_open / camera_close with invalid arguments
 *   10     a device path that does not exist
 *   20-24  get_fd on a real device
 *   30-39  get_configurations and configuration_list_destroy
 *   40-47  camera_configure rejecting out-of-range requests
 *   50-59  operations refused outside the Streaming state
 */

#define CHECK_TEST_NAME "camlib"
#include <check.h>

#include <punos/syscall/syscall.h>
#include <punos/syscall/constants.h>
#include <special/camera.h>
#include <sys/memory.h>
#include <sys/string.h>

#define TEST_CAMERA_PATH "/dev/video0"

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
    String path = make_str(TEST_CAMERA_PATH);
    CameraHandle *camera = NULL;
    CameraConfig config;

    config.path = &path;

    EXPECT(1, camera_open(NULL, &camera) == CORE_INVALID_ARGUMENT);
    EXPECT(2, camera_open(&config, NULL) == CORE_INVALID_ARGUMENT);

    CameraConfig bad_config;
    bad_config.path = NULL;
    EXPECT(3, camera_open(&bad_config, &camera) == CORE_INVALID_ARGUMENT);

    EXPECT(4, camera_close(NULL) == CORE_INVALID_ARGUMENT);
    {
        CameraHandle *null_camera = NULL;
        EXPECT(5, camera_close(&null_camera) == CORE_INVALID_ARGUMENT);
    }

    destroy_str(&path);
}

static void test_open_nonexistent(void)
{
    String path = make_str("/dev/video999");
    CameraHandle *camera = NULL;
    CameraConfig config;

    config.path = &path;

    EXPECT(10, camera_open(&config, &camera) == CORE_NOT_FOUND);

    destroy_str(&path);
}

static void test_get_fd(void)
{
    String path = make_str(TEST_CAMERA_PATH);
    CameraHandle *camera = NULL;
    CameraConfig config;

    config.path = &path;

    ErrorCode result = camera_open(&config, &camera);
    if (result == CORE_OK) {
        i32 fd = -1;
        EXPECT(20, camera_get_fd(camera, &fd) == CORE_OK && fd >= 0);
        EXPECT(21, camera_get_fd(NULL, &fd) == CORE_INVALID_ARGUMENT);
        EXPECT(22, camera_get_fd(camera, NULL) == CORE_INVALID_ARGUMENT);

        EXPECT(23, camera_close(&camera) == CORE_OK);
        EXPECT(24, camera == NULL);
    } else {
        EXPECT(20, result == CORE_NOT_FOUND || result == CORE_PERMISSION_DENIED);
    }

    destroy_str(&path);
}

static void test_get_configurations(void)
{
    String path = make_str(TEST_CAMERA_PATH);
    CameraHandle *camera = NULL;
    CameraConfig config;

    config.path = &path;

    ErrorCode result = camera_open(&config, &camera);
    if (result == CORE_OK) {
        CameraConfigurationList configs;
        configs.items = NULL;
        configs.count = 0;

        result = camera_get_configurations(camera, &configs);
        if (result == CORE_OK) {
            EXPECT(30, configs.count > 0);
            for (usize i = 0; i < configs.count; i++) {
                EXPECT(31, configs.items[i].width > 0);
                EXPECT(32, configs.items[i].height > 0);
                EXPECT(33, configs.items[i].frames_per_second > 0);
                EXPECT(34, configs.items[i].max_frame_bytes > 0);
            }
            EXPECT(35, camera_configuration_list_destroy(&configs) == CORE_OK);
        } else {
            EXPECT(30, result == CORE_UNSUPPORTED);
        }

        EXPECT(36, camera_get_configurations(NULL, &configs) == CORE_INVALID_ARGUMENT);
        EXPECT(37, camera_get_configurations(camera, NULL) == CORE_INVALID_ARGUMENT);
        EXPECT(38, camera_configuration_list_destroy(NULL) == CORE_INVALID_ARGUMENT);

        EXPECT(39, camera_close(&camera) == CORE_OK);
    }

    destroy_str(&path);
}

static void test_configure_errors(void)
{
    String path = make_str(TEST_CAMERA_PATH);
    CameraHandle *camera = NULL;
    CameraConfig config;

    config.path = &path;

    ErrorCode result = camera_open(&config, &camera);
    if (result == CORE_OK) {
        CameraStreamConfig requested;
        CameraStreamConfig actual;

        requested.format = CAMERA_PIXEL_FORMAT_YUYV;
        requested.width = 640;
        requested.height = 480;
        requested.frames_per_second = 30;
        requested.max_frame_bytes = 0;

        EXPECT(40, camera_configure(NULL, &requested, &actual) == CORE_INVALID_ARGUMENT);
        EXPECT(41, camera_configure(camera, NULL, &actual) == CORE_INVALID_ARGUMENT);
        EXPECT(42, camera_configure(camera, &requested, NULL) == CORE_INVALID_ARGUMENT);

        CameraStreamConfig bad_config = requested;
        bad_config.width = 0;
        EXPECT(43, camera_configure(camera, &bad_config, &actual) == CORE_INVALID_ARGUMENT);

        bad_config = requested;
        bad_config.height = 0;
        EXPECT(44, camera_configure(camera, &bad_config, &actual) == CORE_INVALID_ARGUMENT);

        bad_config = requested;
        bad_config.frames_per_second = 0;
        EXPECT(45, camera_configure(camera, &bad_config, &actual) == CORE_INVALID_ARGUMENT);

        bad_config = requested;
        bad_config.max_frame_bytes = 1;
        EXPECT(46, camera_configure(camera, &bad_config, &actual) == CORE_INVALID_ARGUMENT);

        EXPECT(47, camera_close(&camera) == CORE_OK);
    }

    destroy_str(&path);
}

static void test_state_errors(void)
{
    String path = make_str(TEST_CAMERA_PATH);
    CameraHandle *camera = NULL;
    CameraConfig config;

    config.path = &path;

    ErrorCode result = camera_open(&config, &camera);
    if (result == CORE_OK) {
        CameraReadStatus status;
        CameraFrame frame;

        EXPECT(50, camera_start(camera) == CORE_INVALID_STATE);
        EXPECT(51, camera_dequeue_frame(camera, &status, &frame) == CORE_INVALID_STATE);
        EXPECT(52, camera_stop(camera) == CORE_INVALID_STATE);

        EXPECT(53, camera_dequeue_frame(NULL, &status, &frame) == CORE_INVALID_ARGUMENT);
        EXPECT(54, camera_dequeue_frame(camera, NULL, &frame) == CORE_INVALID_ARGUMENT);
        EXPECT(55, camera_dequeue_frame(camera, &status, NULL) == CORE_INVALID_ARGUMENT);
        EXPECT(56, camera_frame_release(NULL, &frame) == CORE_INVALID_ARGUMENT);
        EXPECT(57, camera_frame_release(camera, NULL) == CORE_INVALID_ARGUMENT);
        EXPECT(58, camera_stop(NULL) == CORE_INVALID_ARGUMENT);

        EXPECT(59, camera_close(&camera) == CORE_OK);
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
    test_get_fd();
    test_get_configurations();
    test_configure_errors();
    test_state_errors();

    check_passed();
    return 0;
}
