#define CHECK_TEST_NAME "camserv protocol"
#include <check.h>
#include <camserv/protocol.h>

static bool bytes_equal(const u8 *a, const u8 *b, usize count)
{
    for (usize i = 0; i < count; ++i) if (a[i] != b[i]) return false;
    return true;
}

int main(int argc, char **argv, char **envp)
{
    (void)argc; (void)argv; (void)envp;
    _Static_assert(CAM_CONFIG_SIZE == 20 && CAM_INFO_SIZE == 25 &&
                   CAM_FRAME_HEADER_SIZE == 20, "camera wire sizes");
    CameraStreamConfig config = { CAMERA_PIXEL_FORMAT_MJPEG, 0x1234, 0x5678, 30, 0x100000 };
    u8 bytes[64];
    const u8 config_expected[20] = {
        2,0,0,0, 0x34,0x12,0,0, 0x78,0x56,0,0,
        30,0,0,0, 0,0,0x10,0
    };
    cam_encode_config(bytes, &config);
    EXPECT(1, bytes_equal(bytes, config_expected, 20));
    cam_encode_info(bytes, CORE_OK, &config);
    EXPECT(2, bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 &&
              bytes[3] == 0 && bytes[4] == 1 &&
              bytes_equal(bytes + 5, config_expected, 20));
    cam_encode_info(bytes, CORE_INVALID_ARGUMENT, NULL);
    EXPECT(3, bytes[0] == 1 && bytes[4] == 0);
    for (usize i = 5; i < 25; ++i) EXPECT(4, bytes[i] == 0);
    u8 data[] = { 0xaa, 0x00, 0xff };
    CameraFrame frame = { .data = { data, 3 }, .timestamp = 0x1122334455667788ULL,
                          .sequence = 0x0102030405060708ULL };
    usize size;
    const u8 frame_expected[] = {
        8,7,6,5,4,3,2,1, 0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,
        3,0,0,0, 0xaa,0x00,0xff
    };
    EXPECT(5, cam_encode_frame(bytes, sizeof(bytes), &frame, &size) == CORE_OK &&
              size == sizeof(frame_expected) && bytes_equal(bytes, frame_expected, size));
    EXPECT(6, cam_encode_frame(bytes, 22, &frame, &size) == CORE_INVALID_ARGUMENT);
    frame.data.size = IPC_MESSAGE_MAX_SIZE - CAM_FRAME_HEADER_SIZE;
    EXPECT(7, cam_encode_frame(bytes, sizeof(bytes), &frame, &size) == CORE_INVALID_ARGUMENT);
    frame.data.size++;
    EXPECT(8, cam_encode_frame(bytes, sizeof(bytes), &frame, &size) == CORE_INVALID_ARGUMENT);
    u8 version[] = { 1, 0 };
    IpcMessage request = { CAM_SUBSCRIBE, { version, 2 } };
    u32 type;
    EXPECT(9, cam_request_error(&request, &type) == CORE_OK && type == CAM_SUBSCRIBE_RESULT);
    version[0] = 2;
    EXPECT(10, cam_request_error(&request, &type) == CORE_UNSUPPORTED);
    request.payload.size = 1;
    EXPECT(11, cam_request_error(&request, &type) == CORE_INVALID_ARGUMENT);
    request.type = 99;
    EXPECT(12, cam_request_error(&request, &type) == CORE_INVALID_ARGUMENT && type == CAM_ERROR);
    check_passed();
    return 0;
}
