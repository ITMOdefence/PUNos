#define CHECK_TEST_NAME "IOserv protocol"
#include <check.h>
#include <ioserv/protocol.h>

static bool bytes_equal(const u8 *a, const u8 *b, usize count)
{
    for (usize i = 0; i < count; ++i) if (a[i] != b[i]) return false;
    return true;
}

int main(int argc, char **argv, char **envp)
{
    (void)argc; (void)argv; (void)envp;
    _Static_assert(IO_DEVICE_EVENT_SIZE == 20 && IO_FRAME_HEADER_SIZE == 18 &&
                   IO_EVENT_SIZE == 7, "input wire sizes");
    u8 subscribe[] = { 1,0,5,0,0,0 };
    IpcMessage request = { IO_SUBSCRIBE, { subscribe, sizeof(subscribe) } };
    u32 type; InputDeviceClass mask = 0;
    EXPECT(1, io_request_error(&request, &type, &mask) == CORE_OK &&
              type == IO_SUBSCRIBE_RESULT && mask == 5);
    subscribe[0] = 2;
    EXPECT(2, io_request_error(&request, &type, &mask) == CORE_UNSUPPORTED);
    subscribe[0] = 1; subscribe[2] = 0;
    EXPECT(3, io_request_error(&request, &type, &mask) == CORE_INVALID_ARGUMENT);
    subscribe[2] = 8;
    EXPECT(4, io_request_error(&request, &type, &mask) == CORE_UNSUPPORTED);
    request.payload.size = 5;
    EXPECT(5, io_request_error(&request, &type, &mask) == CORE_INVALID_ARGUMENT);
    request.type = 99;
    EXPECT(6, io_request_error(&request, &type, &mask) == CORE_INVALID_ARGUMENT && type == IO_ERROR);
    InputDeviceInfo info = { 0x1234, 0x5678, 0x9abc, 0xdef0, 5 };
    u8 bytes[IO_FRAME_HEADER_SIZE + IO_EVENT_SIZE * INPUT_FRAME_MAX_EVENTS];
    const u8 device_expected[] = {
        8,7,6,5,4,3,2,1, 0x34,0x12, 0x78,0x56, 0xbc,0x9a,
        0xf0,0xde, 5,0,0,0
    };
    io_encode_device(bytes, 0x0102030405060708ULL, &info);
    EXPECT(7, bytes_equal(bytes, device_expected, sizeof(device_expected)));
    InputEvent events[] = { { INPUT_EVENT_KIND_KEY, 0x1234, -3 },
                            { INPUT_EVENT_KIND_SYNC_REPORT, 0, 0 } };
    InputFrame frame = { events, 2 };
    usize size;
    const u8 frame_expected[] = {
        8,7,6,5,4,3,2,1, 3,0,0,0,0,0,0,0, 2,0,
        1,0x34,0x12,0xfd,0xff,0xff,0xff,
        4,0,0,0,0,0,0
    };
    EXPECT(8, io_encode_frame(bytes, sizeof(bytes), 0x0102030405060708ULL,
                              3, &frame, &size) == CORE_OK &&
              size == sizeof(frame_expected) && bytes_equal(bytes, frame_expected, size));
    EXPECT(9, io_encode_frame(bytes, 30, 1, 1, &frame, &size) == CORE_INVALID_ARGUMENT);
    InputEvent kinds[] = {
        { INPUT_EVENT_KIND_KEY, 0x0101, -1 },
        { INPUT_EVENT_KIND_RELATIVE_AXIS, 0x0202, -2 },
        { INPUT_EVENT_KIND_ABSOLUTE_AXIS, 0x0303, -3 },
        { INPUT_EVENT_KIND_SYNC_REPORT, 0x0404, -4 },
        { INPUT_EVENT_KIND_SYNC_LOST, 0x0505, -5 },
        { INPUT_EVENT_KIND_RAW, 0x0606, -6 }
    };
    const u8 kinds_expected[] = {
        6,0,
        1,1,1,0xff,0xff,0xff,0xff,
        2,2,2,0xfe,0xff,0xff,0xff,
        3,3,3,0xfd,0xff,0xff,0xff,
        4,4,4,0xfc,0xff,0xff,0xff,
        5,5,5,0xfb,0xff,0xff,0xff,
        6,6,6,0xfa,0xff,0xff,0xff
    };
    frame.events = kinds;
    frame.count = 6;
    EXPECT(10, io_encode_frame(bytes, sizeof(bytes), 1, 1, &frame, &size) == CORE_OK &&
               size == IO_FRAME_HEADER_SIZE + 6 * IO_EVENT_SIZE &&
               bytes_equal(bytes + 16, kinds_expected, sizeof(kinds_expected)));
    kinds[5].kind = (InputEventKind)6;
    EXPECT(11, io_encode_frame(bytes, sizeof(bytes), 1, 1, &frame, &size) ==
               CORE_INVALID_ARGUMENT);
    frame.count = 257;
    EXPECT(12, io_encode_frame(bytes, sizeof(bytes), 1, 1, &frame, &size) == CORE_INVALID_ARGUMENT);
    check_passed();
    return 0;
}
