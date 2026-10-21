/*
 * Runtime test of the camlib operations against
 * DevDoc/SpecialLibs/camlib.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 *
 * Linked with v4l2_fake.c, so capture and failure paths run without hardware
 * on the host and in the PUNos initramfs. libv4l2 has its own driver ABI suite.
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
    CameraHandle camera = NULL;
    CameraConfig config;

    config.path = &path;

    EXPECT(1, camera_open(NULL, &camera) == CORE_INVALID_ARGUMENT);
    EXPECT(2, camera_open(&config, NULL) == CORE_INVALID_ARGUMENT);

    CameraConfig bad_config;
    bad_config.path = NULL;
    EXPECT(3, camera_open(&bad_config, &camera) == CORE_INVALID_ARGUMENT);

    EXPECT(4, camera_close(NULL) == CORE_INVALID_ARGUMENT);
    {
        CameraHandle null_camera = NULL;
        EXPECT(5, camera_close(&null_camera) == CORE_INVALID_ARGUMENT);
    }

    destroy_str(&path);
}

static void test_open_nonexistent(void)
{
    String path = make_str("/dev/video999");
    CameraHandle camera = NULL;
    CameraConfig config;

    config.path = &path;

    EXPECT(6, camera_open(&config, &camera) == CORE_NOT_FOUND);

    destroy_str(&path);
}

static void test_get_fd(void)
{
    String path = make_str(TEST_CAMERA_PATH);
    CameraHandle camera = NULL;
    CameraConfig config;

    config.path = &path;

    ErrorCode result = camera_open(&config, &camera);
    if (result == CORE_OK) {
        i32 fd = -1;
        EXPECT(7, camera_get_fd(camera, &fd) == CORE_OK && fd >= 0);
        EXPECT(8, camera_get_fd(NULL, &fd) == CORE_INVALID_ARGUMENT);
        EXPECT(9, camera_get_fd(camera, NULL) == CORE_INVALID_ARGUMENT);

        EXPECT(10, camera_close(&camera) == CORE_OK);
        EXPECT(11, camera == NULL);
    } else {
        EXPECT(12, result == CORE_NOT_FOUND || result == CORE_PERMISSION_DENIED);
    }

    destroy_str(&path);
}

static void test_get_configurations(void)
{
    String path = make_str(TEST_CAMERA_PATH);
    CameraHandle camera = NULL;
    CameraConfig config;

    config.path = &path;

    ErrorCode result = camera_open(&config, &camera);
    if (result == CORE_OK) {
        CameraConfigurationList configs;
        configs.items = NULL;
        configs.count = 0;

        result = camera_get_configurations(camera, &configs);
        if (result == CORE_OK) {
            EXPECT(13, configs.count > 0);
            for (usize i = 0; i < configs.count; i++) {
                EXPECT(14, configs.items[i].width > 0);
                EXPECT(15, configs.items[i].height > 0);
                EXPECT(16, configs.items[i].frames_per_second > 0);
                EXPECT(17, configs.items[i].max_frame_bytes > 0);
            }
            EXPECT(18, camera_configuration_list_destroy(&configs) == CORE_OK);
        } else {
            EXPECT(19, result == CORE_UNSUPPORTED);
        }

        EXPECT(20, camera_get_configurations(NULL, &configs) == CORE_INVALID_ARGUMENT);
        EXPECT(21, camera_get_configurations(camera, NULL) == CORE_INVALID_ARGUMENT);
        EXPECT(22, camera_configuration_list_destroy(NULL) == CORE_INVALID_ARGUMENT);

        EXPECT(23, camera_close(&camera) == CORE_OK);
    }

    destroy_str(&path);
}

static void test_configure_errors(void)
{
    String path = make_str(TEST_CAMERA_PATH);
    CameraHandle camera = NULL;
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

        EXPECT(24, camera_configure(NULL, &requested, &actual) == CORE_INVALID_ARGUMENT);
        EXPECT(25, camera_configure(camera, NULL, &actual) == CORE_INVALID_ARGUMENT);
        EXPECT(26, camera_configure(camera, &requested, NULL) == CORE_INVALID_ARGUMENT);

        CameraStreamConfig bad_config = requested;
        bad_config.width = 0;
        EXPECT(27, camera_configure(camera, &bad_config, &actual) == CORE_INVALID_ARGUMENT);

        bad_config = requested;
        bad_config.height = 0;
        EXPECT(28, camera_configure(camera, &bad_config, &actual) == CORE_INVALID_ARGUMENT);

        bad_config = requested;
        bad_config.frames_per_second = 0;
        EXPECT(29, camera_configure(camera, &bad_config, &actual) == CORE_INVALID_ARGUMENT);

        bad_config = requested;
        bad_config.max_frame_bytes = 1;
        EXPECT(30, camera_configure(camera, &bad_config, &actual) == CORE_INVALID_ARGUMENT);

        EXPECT(31, camera_close(&camera) == CORE_OK);
    }

    destroy_str(&path);
}

static void test_state_errors(void)
{
    String path = make_str(TEST_CAMERA_PATH);
    CameraHandle camera = NULL;
    CameraConfig config;

    config.path = &path;

    ErrorCode result = camera_open(&config, &camera);
    if (result == CORE_OK) {
        CameraReadStatus status;
        CameraFrame frame;

        EXPECT(32, camera_start(camera) == CORE_INVALID_STATE);
        EXPECT(33, camera_dequeue_frame(camera, &status, &frame) == CORE_INVALID_STATE);
        EXPECT(34, camera_stop(camera) == CORE_INVALID_STATE);

        EXPECT(35, camera_dequeue_frame(NULL, &status, &frame) == CORE_INVALID_ARGUMENT);
        EXPECT(36, camera_dequeue_frame(camera, NULL, &frame) == CORE_INVALID_ARGUMENT);
        EXPECT(37, camera_dequeue_frame(camera, &status, NULL) == CORE_INVALID_ARGUMENT);
        EXPECT(38, camera_frame_release(NULL, &frame) == CORE_INVALID_ARGUMENT);
        EXPECT(39, camera_frame_release(camera, NULL) == CORE_INVALID_ARGUMENT);
        EXPECT(40, camera_stop(NULL) == CORE_INVALID_ARGUMENT);

        EXPECT(41, camera_close(&camera) == CORE_OK);
    }

    destroy_str(&path);
}

/* Issue 13: empty and wrongly shaped paths must be rejected before they reach
 * the lower layer, which would answer with a code outside the contract. */
static void test_path_validation(void)
{
    CameraHandle camera = NULL;
    CameraConfig config;

    String empty = make_str("");
    config.path = &empty;
    EXPECT(42, camera_open(&config, &camera) == CORE_INVALID_ARGUMENT);

    String wrong_dir = make_str("/dev/input/event0");
    config.path = &wrong_dir;
    EXPECT(43, camera_open(&config, &camera) == CORE_INVALID_PATH);

    String no_digits = make_str("/dev/videoX");
    config.path = &no_digits;
    EXPECT(44, camera_open(&config, &camera) == CORE_INVALID_PATH);

    String prefix_only = make_str("/dev/video");
    config.path = &prefix_only;
    EXPECT(45, camera_open(&config, &camera) == CORE_INVALID_PATH);

    CameraConfig null_path;
    null_path.path = NULL;
    EXPECT(46, camera_open(&null_path, &camera) == CORE_INVALID_ARGUMENT);

    destroy_str(&empty);
    destroy_str(&wrong_dir);
    destroy_str(&no_digits);
    destroy_str(&prefix_only);
}

/* --- driven through tests/v4l2_fake.c, so no camera hardware is needed --- */

/* Control surface of the fake. */
void fake_v4l2_forget(void);
void fake_v4l2_lose_device(void);
void fake_v4l2_arm_frame(bool ready, usize index);
bool fake_v4l2_is_configured(void);
usize fake_v4l2_outstanding(void);
usize fake_v4l2_start_refusals(void);
void fake_v4l2_configurations(usize count, bool unsupported);
void fake_v4l2_reject_close(void);
usize fake_memory_mismatches(void);
usize fake_memory_live(void);

static void fill_request(CameraStreamConfig *out)
{
    out->format = CAMERA_PIXEL_FORMAT_YUYV;
    out->width = 640;
    out->height = 480;
    out->frames_per_second = 30;
    out->max_frame_bytes = 0;
}

static CameraHandle open_fake_camera(void)
{
    String path = make_str(TEST_CAMERA_PATH);
    CameraHandle camera = NULL;
    CameraConfig config;

    config.path = &path;
    (void)camera_open(&config, &camera);
    destroy_str(&path);
    return camera;
}

/*
 * Issue 5: camera_stop() left camlib in Configured while the lower layer had
 * already unmapped its buffers, so the next camera_start() failed with
 * CORE_INVALID_STATE. stop() now returns the camera to Open, and a
 * configure/start/stop cycle has to be repeatable.
 */
static void test_restart_after_stop(void)
{
    CameraHandle camera = open_fake_camera();
    CameraStreamConfig requested;
    CameraStreamConfig actual;

    fill_request(&requested);

    EXPECT(47, camera != NULL);

    EXPECT(48, camera_configure(camera, &requested, &actual) == CORE_OK);
    EXPECT(49, fake_v4l2_is_configured());
    EXPECT(50, camera_start(camera) == CORE_OK);

    EXPECT(51, camera_stop(camera) == CORE_OK);
    /* The lower layer gave its buffers back, so it is no longer configured. */
    EXPECT(52, !fake_v4l2_is_configured());

    /* camserv reads the state table and may call start() straight after a
     * stop(). camlib has to refuse that itself, because the lower layer has no
     * buffers left. The old code claimed Configured, forwarded the call, and
     * the refusal came from one layer down as CORE_INVALID_STATE with the
     * buffers already released - which is the bug. start_refusals stays 0
     * when camlib stops the call itself. */
    EXPECT(53, camera_start(camera) == CORE_INVALID_STATE);
    EXPECT(54, fake_v4l2_start_refusals() == 0);

    /* And the documented way round still works. */
    EXPECT(55, camera_configure(camera, &requested, &actual) == CORE_OK);
    EXPECT(56, camera_start(camera) == CORE_OK);
    EXPECT(57, camera_stop(camera) == CORE_OK);
    EXPECT(58, fake_v4l2_start_refusals() == 0);

    /* A third cycle, to be sure nothing accumulates. */
    EXPECT(59, camera_configure(camera, &requested, &actual) == CORE_OK);
    EXPECT(60, camera_start(camera) == CORE_OK);
    EXPECT(61, camera_stop(camera) == CORE_OK);

    EXPECT(62, camera_close(&camera) == CORE_OK);
    EXPECT(63, camera == NULL);
}

/*
 * Issue 4: the state table allows configure() only in Open and Configured, and
 * close() not in Streaming. Both used to be accepted, which let the MMAP
 * buffers be released while the driver was still filling them.
 */
static void test_configure_and_close_refused_while_streaming(void)
{
    CameraHandle camera = open_fake_camera();
    CameraStreamConfig requested;
    CameraStreamConfig actual;

    fill_request(&requested);

    EXPECT(64, camera_configure(camera, &requested, &actual) == CORE_OK);
    EXPECT(65, camera_start(camera) == CORE_OK);

    EXPECT(66, camera_configure(camera, &requested, &actual) == CORE_INVALID_STATE);
    EXPECT(67, camera_close(&camera) == CORE_INVALID_STATE);
    /* The refused close must not have released anything. */
    EXPECT(68, camera != NULL);

    /* Streaming allows get_fd, dequeue, release and stop, nothing else. */
    EXPECT(69, camera_stop(camera) == CORE_OK);
    EXPECT(70, camera_configure(camera, &requested, &actual) == CORE_OK);
    EXPECT(71, camera_start(camera) == CORE_OK);

    /* In Configured, close is allowed again. */
    EXPECT(72, camera_stop(camera) == CORE_OK);
    EXPECT(73, camera_close(&camera) == CORE_OK);
    EXPECT(74, camera == NULL);
}

/*
 * Issue 10: only a counter and a range check guarded camera_frame_release(), so
 * a frame this camera never handed out was accepted while another one was still
 * out. The set of issued buffers has to be tracked per buffer.
 */
static void test_frame_identity(void)
{
    CameraHandle camera = open_fake_camera();
    CameraStreamConfig requested;
    CameraStreamConfig actual;
    CameraReadStatus status;
    CameraFrame frame;

    fill_request(&requested);
    EXPECT(75, camera_configure(camera, &requested, &actual) == CORE_OK);
    EXPECT(76, camera_start(camera) == CORE_OK);

    /* Nothing dequeued yet: any release is invalid. */
    frame.buffer_id = 0;
    frame.data.data = NULL;
    frame.data.size = 0;
    frame.timestamp = 0;
    frame.sequence = 0;
    EXPECT(77, camera_frame_release(camera, &frame) == CORE_INVALID_STATE);

    /* Take buffer 0 out for real. */
    fake_v4l2_arm_frame(true, 0);
    EXPECT(78, camera_dequeue_frame(camera, &status, &frame) == CORE_OK);
    EXPECT(79, status == CAMERA_READ_STATUS_FRAME_READY);
    EXPECT(80, frame.buffer_id == 0);
    EXPECT(81, frame.data.data != NULL && frame.data.size > 0);
    EXPECT(82, fake_v4l2_outstanding() == 1);

    /* A different frame naming buffer 1, which was never issued: this is what
     * the counter-only check used to let through. */
    {
        CameraFrame forged = frame;

        forged.buffer_id = 1;
        EXPECT(83, camera_frame_release(camera, &forged) == CORE_INVALID_ARGUMENT);
        /* Buffer 0 is still out, so the counter did not move. */
        EXPECT(84, fake_v4l2_outstanding() == 1);
    }

    /* The real frame is accepted and empties the queue. */
    EXPECT(85, camera_frame_release(camera, &frame) == CORE_OK);
    EXPECT(86, fake_v4l2_outstanding() == 0);
    EXPECT(87, frame.data.data == NULL && frame.data.size == 0);

    /* Releasing it a second time is refused: nothing is outstanding. */
    EXPECT(88, camera_frame_release(camera, &frame) == CORE_INVALID_STATE);

    EXPECT(89, camera_stop(camera) == CORE_OK);
    EXPECT(90, camera_close(&camera) == CORE_OK);
}

/*
 * Issue 6 and contract 11: on DeviceLost every frame the caller still holds
 * becomes meaningless and close() is allowed without returning them. camlib
 * used to zero only its own counter, so the lower layer refused the close and
 * its handle leaked.
 */
static void test_device_lost_with_outstanding_frame(void)
{
    CameraHandle camera = open_fake_camera();
    CameraStreamConfig requested;
    CameraStreamConfig actual;
    CameraReadStatus status;
    CameraFrame frame;

    fill_request(&requested);
    EXPECT(91, camera_configure(camera, &requested, &actual) == CORE_OK);
    EXPECT(92, camera_start(camera) == CORE_OK);

    fake_v4l2_arm_frame(true, 0);
    EXPECT(93, camera_dequeue_frame(camera, &status, &frame) == CORE_OK);
    EXPECT(94, status == CAMERA_READ_STATUS_FRAME_READY);
    EXPECT(95, fake_v4l2_outstanding() == 1);

    /* The device goes away while that frame is still out. */
    fake_v4l2_lose_device();
    EXPECT(96, camera_dequeue_frame(camera, &status, &frame) == CORE_OK);
    EXPECT(97, status == CAMERA_READ_STATUS_DEVICE_LOST);

    /* DeviceLost allows close only. */
    EXPECT(98, camera_configure(camera, &requested, &actual) == CORE_INVALID_STATE);
    EXPECT(99, camera_stop(camera) == CORE_INVALID_STATE);

    /* Closing here is what used to fail and leak the lower layer's handle. */
    EXPECT(100, camera_close(&camera) == CORE_OK);
    EXPECT(101, camera == NULL);
    fake_v4l2_forget();
}

/* A frame that is not ready has to be reported as NoFrame, not invented. */
static void test_no_frame(void)
{
    CameraHandle camera = open_fake_camera();
    CameraStreamConfig requested;
    CameraStreamConfig actual;
    CameraReadStatus status;
    CameraFrame frame;

    fill_request(&requested);
    EXPECT(102, camera_configure(camera, &requested, &actual) == CORE_OK);
    EXPECT(103, camera_start(camera) == CORE_OK);

    fake_v4l2_arm_frame(false, 0);
    EXPECT(104, camera_dequeue_frame(camera, &status, &frame) == CORE_OK);
    EXPECT(105, status == CAMERA_READ_STATUS_NO_FRAME);

    EXPECT(106, camera_stop(camera) == CORE_OK);
    EXPECT(107, camera_close(&camera) == CORE_OK);
}

/*
 * Issue 11: allocate exactly the number of supported configurations. The
 * filtered multi-page case and allocation accounting are tested below too.
 */
static void test_configuration_list_ownership(void)
{
    CameraHandle camera = open_fake_camera();
    CameraConfigurationList configs;

    configs.items = NULL;
    configs.count = 0;

    EXPECT(108, camera_get_configurations(camera, &configs) == CORE_OK);
    EXPECT(109, configs.count == 1);
    EXPECT(110, configs.items != NULL);
    EXPECT(111, configs.items[0].max_frame_bytes > 0);

    EXPECT(112, camera_configuration_list_destroy(&configs) == CORE_OK);
    EXPECT(113, configs.items == NULL && configs.count == 0);
    EXPECT(114, camera_configuration_list_destroy(&configs) == CORE_OK);

    EXPECT(115, camera_close(&camera) == CORE_OK);
}

static void test_cross_camera_and_stale_frames(void)
{
    CameraStreamConfig requested, actual;
    CameraReadStatus status;
    CameraFrame first, second, stale, forged;
    CameraHandle a = open_fake_camera();
    fill_request(&requested);
    EXPECT(116, camera_configure(a, &requested, &actual) == CORE_OK);
    EXPECT(117, camera_start(a) == CORE_OK);
    fake_v4l2_arm_frame(true, 0);
    EXPECT(118, camera_dequeue_frame(a, &status, &first) == CORE_OK &&
                status == CAMERA_READ_STATUS_FRAME_READY);
    CameraHandle b = open_fake_camera();
    EXPECT(119, camera_configure(b, &requested, &actual) == CORE_OK);
    EXPECT(120, camera_start(b) == CORE_OK);
    fake_v4l2_arm_frame(true, 0);
    EXPECT(121, camera_dequeue_frame(b, &status, &second) == CORE_OK &&
                status == CAMERA_READ_STATUS_FRAME_READY);
    EXPECT(122, camera_frame_release(a, &second) == CORE_INVALID_ARGUMENT);
    EXPECT(123, camera_frame_release(b, &first) == CORE_INVALID_ARGUMENT);
    forged = second;
    forged.data.size -= 1;
    EXPECT(124, camera_frame_release(b, &forged) == CORE_INVALID_ARGUMENT);
    forged = second;
    forged.timestamp += 1;
    EXPECT(125, camera_frame_release(b, &forged) == CORE_INVALID_ARGUMENT);
    stale = second;
    EXPECT(126, camera_frame_release(b, &second) == CORE_OK);
    fake_v4l2_arm_frame(true, 0);
    EXPECT(127, camera_dequeue_frame(b, &status, &second) == CORE_OK &&
                status == CAMERA_READ_STATUS_FRAME_READY);
    /* Isolate the sequence check: even with the current timestamp, the old
     * issuance of this same buffer must not be returned as the new frame. */
    stale.timestamp = second.timestamp;
    EXPECT(128, camera_frame_release(b, &stale) == CORE_INVALID_ARGUMENT);
    EXPECT(129, camera_stop(b) == CORE_INVALID_STATE);
    EXPECT(130, camera_frame_release(b, &second) == CORE_OK);
    EXPECT(131, camera_frame_release(a, &first) == CORE_OK);
    EXPECT(132, camera_stop(a) == CORE_OK);
    EXPECT(133, camera_stop(b) == CORE_OK);
    EXPECT(134, camera_close(&a) == CORE_OK);
    EXPECT(135, camera_close(&b) == CORE_OK);
}

static void test_state_table_and_close_retry(void)
{
    CameraHandle camera = open_fake_camera();
    CameraStreamConfig requested, actual;
    CameraConfigurationList configs;
    CameraFrame frame;
    CameraReadStatus status;
    i32 fd;
    fill_request(&requested);
    EXPECT(136, camera_configure(camera, &requested, &actual) == CORE_OK);
    EXPECT(137, camera_configure(camera, &requested, &actual) == CORE_OK);
    EXPECT(138, camera_get_configurations(camera, &configs) == CORE_OK);
    EXPECT(139, camera_configuration_list_destroy(&configs) == CORE_OK);
    EXPECT(140, camera_start(camera) == CORE_OK);
    EXPECT(141, camera_start(camera) == CORE_INVALID_STATE);
    EXPECT(142, camera_get_configurations(camera, &configs) == CORE_INVALID_STATE);
    EXPECT(143, camera_get_fd(camera, &fd) == CORE_OK);
    fake_v4l2_lose_device();
    EXPECT(144, camera_dequeue_frame(camera, &status, &frame) == CORE_OK &&
                status == CAMERA_READ_STATUS_DEVICE_LOST);
    EXPECT(145, camera_get_fd(camera, &fd) == CORE_INVALID_STATE);
    EXPECT(146, camera_get_configurations(camera, &configs) == CORE_INVALID_STATE);
    EXPECT(147, camera_start(camera) == CORE_INVALID_STATE);
    EXPECT(148, camera_dequeue_frame(camera, &status, &frame) == CORE_INVALID_STATE);
    EXPECT(149, camera_frame_release(camera, &frame) == CORE_INVALID_STATE);
    fake_v4l2_reject_close();
    EXPECT(150, camera_close(&camera) == CORE_INVALID_STATE);
    EXPECT(151, camera != NULL);
    EXPECT(152, camera_close(&camera) == CORE_OK && camera == NULL);
}

static void test_filtered_configuration_allocation(void)
{
    usize before = fake_memory_live();
    CameraHandle camera = open_fake_camera();
    CameraConfigurationList configs;
    fake_v4l2_configurations(300, false);
    EXPECT(153, camera_get_configurations(camera, &configs) == CORE_OK);
    EXPECT(154, configs.count == 1 && configs.items[0].format == CAMERA_PIXEL_FORMAT_YUYV);
    EXPECT(155, camera_configuration_list_destroy(&configs) == CORE_OK);
    EXPECT(156, fake_memory_mismatches() == 0);
    fake_v4l2_configurations(300, true);
    EXPECT(157, camera_get_configurations(camera, &configs) == CORE_UNSUPPORTED);
    fake_v4l2_configurations(0, false);
    EXPECT(158, camera_get_configurations(camera, &configs) == CORE_OK);
    EXPECT(159, configs.count == 0 && configs.items == NULL);
    EXPECT(160, camera_configuration_list_destroy(&configs) == CORE_OK);
    EXPECT(161, camera_close(&camera) == CORE_OK);
    EXPECT(162, fake_memory_live() == before && fake_memory_mismatches() == 0);
    fake_v4l2_configurations(1, false);
}

void fake_v4l2_fail_stop(void);
void fake_v4l2_fail_queue(void);
void fake_v4l2_fractional_rate(bool value);
void fake_v4l2_mixed_rates(bool value);

static void test_stop_failure_invalidates_capture(void)
{
    CameraHandle camera = open_fake_camera();
    CameraStreamConfig requested, actual;
    CameraFrame frame;
    CameraReadStatus status;
    fill_request(&requested);
    EXPECT(163, camera_configure(camera, &requested, &actual) == CORE_OK);
    EXPECT(164, camera_start(camera) == CORE_OK);
    fake_v4l2_fail_stop();
    EXPECT(165, camera_stop(camera) == CORE_IO_ERROR);
    EXPECT(166, camera_dequeue_frame(camera, &status, &frame) == CORE_INVALID_STATE);
    EXPECT(167, camera_start(camera) == CORE_INVALID_STATE);
    EXPECT(168, camera_configure(camera, &requested, &actual) == CORE_INVALID_STATE);
    EXPECT(169, camera_close(&camera) == CORE_OK && camera == NULL);
}

static void test_release_failure_emergency_cleanup(void)
{
    usize before = fake_memory_live();
    CameraHandle camera = open_fake_camera();
    CameraStreamConfig requested, actual;
    CameraFrame frame;
    CameraReadStatus status;
    fill_request(&requested);
    EXPECT(170, camera_configure(camera, &requested, &actual) == CORE_OK);
    EXPECT(171, camera_start(camera) == CORE_OK);
    fake_v4l2_arm_frame(true, 0);
    EXPECT(172, camera_dequeue_frame(camera, &status, &frame) == CORE_OK &&
                status == CAMERA_READ_STATUS_FRAME_READY);
    fake_v4l2_fail_queue();
    EXPECT(173, camera_frame_release(camera, &frame) == CORE_IO_ERROR);
    EXPECT(174, camera_stop(camera) == CORE_INVALID_STATE);
    EXPECT(175, camera_close(&camera) == CORE_INVALID_STATE && camera != NULL);
    EXPECT(176, camera_abort(&camera) == CORE_OK && camera == NULL);
    EXPECT(177, fake_memory_live() == before && fake_memory_mismatches() == 0);
}

static void test_fractional_rate_is_explicitly_unsupported(void)
{
    usize before = fake_memory_live();
    CameraHandle camera = open_fake_camera();
    CameraConfigurationList list;
    CameraStreamConfig requested, actual;
    fake_v4l2_fractional_rate(true);
    EXPECT(178, camera_get_configurations(camera, &list) == CORE_UNSUPPORTED);
    fill_request(&requested);
    EXPECT(179, camera_configure(camera, &requested, &actual) == CORE_UNSUPPORTED);
    EXPECT(180, camera_close(&camera) == CORE_OK && camera == NULL);
    fake_v4l2_fractional_rate(false);
    EXPECT(181, fake_memory_live() == before && fake_memory_mismatches() == 0);
}

static void test_mixed_rates_keep_integer_configuration(void)
{
    usize before = fake_memory_live();
    CameraHandle camera = open_fake_camera();
    CameraConfigurationList list;
    fake_v4l2_configurations(2, false);
    fake_v4l2_mixed_rates(true);
    EXPECT(182, camera_get_configurations(camera, &list) == CORE_OK);
    EXPECT(183, list.count == 1 && list.items != NULL);
    EXPECT(184, list.items[0].format == CAMERA_PIXEL_FORMAT_YUYV &&
                list.items[0].frames_per_second == 30 &&
                list.items[0].width == 640 && list.items[0].height == 480);
    EXPECT(185, camera_configuration_list_destroy(&list) == CORE_OK);
    EXPECT(186, camera_close(&camera) == CORE_OK && camera == NULL);
    fake_v4l2_mixed_rates(false);
    fake_v4l2_configurations(1, false);
    EXPECT(187, fake_memory_live() == before && fake_memory_mismatches() == 0);
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
    test_path_validation();

    /* Driven through the fake, so these run without a camera. */
    test_restart_after_stop();
    test_configure_and_close_refused_while_streaming();
    test_frame_identity();
    test_device_lost_with_outstanding_frame();
    test_no_frame();
    test_configuration_list_ownership();
    test_cross_camera_and_stale_frames();
    test_state_table_and_close_retry();
    test_filtered_configuration_allocation();
    test_stop_failure_invalidates_capture();
    test_release_failure_emergency_cleanup();
    test_fractional_rate_is_explicitly_unsupported();
    test_mixed_rates_keep_integer_configuration();

    check_passed();
    return 0;
}
