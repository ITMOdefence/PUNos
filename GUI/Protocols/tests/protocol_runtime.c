#define CHECK_TEST_NAME "gui-protocol"
#include "../../../System/SystemLibs/testing/check.h"

#include <gui/protocol.h>

static void test_buffer_import(void)
{
    u8 encoded[34];
    usize size = 0;
    GuiBufferImportRequest source = {
        { GUI_PROTOCOL_VERSION, 0x0102030405060708ULL },
        320, 640, 1280, GUI_PIXEL_XRGB8888, 819200
    };
    GuiBufferImportRequest decoded;
    IpcMessage message;

    EXPECT(1, gui_encode_buffer_import(&source,
        (Bytes){ encoded, sizeof(encoded) }, &size) == CORE_OK);
    EXPECT(2, size == sizeof(encoded));
    EXPECT(3, encoded[0] == 1 && encoded[1] == 0 && encoded[2] == 8 &&
              encoded[9] == 1);
    message.type = GUI_SURFACE_BUFFER_IMPORT;
    message.payload = (Bytes){ encoded, sizeof(encoded) };
    message.descriptor = 5;
    EXPECT(4, gui_decode_buffer_import(&message, &decoded) == CORE_OK);
    EXPECT(5, decoded.prefix.request_id == source.prefix.request_id &&
              decoded.width == 320 && decoded.height == 640 &&
              decoded.stride == 1280 && decoded.byte_size == 819200);
    message.descriptor = IPC_NO_DESCRIPTOR;
    EXPECT(6, gui_decode_buffer_import(&message, &decoded) ==
              CORE_INVALID_ARGUMENT);
}

static void test_damage_and_present(void)
{
    u8 damage_bytes[40];
    u8 present_bytes[72];
    usize size;
    GuiSurfaceDamageRequest damage = { 0 };
    GuiSurfaceDamageRequest decoded_damage;
    GuiDisplayPresentRequest present = { 0 };
    GuiDisplayPresentRequest decoded_present;
    IpcMessage message;

    damage.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    damage.prefix.request_id = 11;
    damage.surface_id = 9;
    damage.rect_count = 1;
    damage.rects[0] = (GuiRect){ -2, 3, 10, 20 };
    EXPECT(10, gui_encode_surface_damage(&damage,
        (Bytes){ damage_bytes, sizeof(damage_bytes) }, &size) == CORE_OK);
    EXPECT(11, size == sizeof(damage_bytes));
    message.type = GUI_SURFACE_DAMAGE;
    message.payload = (Bytes){ damage_bytes, sizeof(damage_bytes) };
    message.descriptor = IPC_NO_DESCRIPTOR;
    EXPECT(12, gui_decode_surface_damage(&message, &decoded_damage) == CORE_OK);
    EXPECT(13, decoded_damage.rect_count == 1 &&
               decoded_damage.rects[0].x == -2 &&
               decoded_damage.rects[0].height == 20);

    present.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    present.prefix.request_id = 12;
    present.display_id = 1;
    present.generation = 2;
    present.width = 320;
    present.height = 640;
    present.stride = 1280;
    present.pixel_format = GUI_PIXEL_ARGB8888;
    present.byte_size = 819200;
    present.damage_count = 1;
    present.damage[0] = (GuiRect){ 0, 0, 320, 640 };
    EXPECT(14, gui_encode_display_present(&present,
        (Bytes){ present_bytes, sizeof(present_bytes) }, &size) == CORE_OK);
    EXPECT(15, size == sizeof(present_bytes));
    message.type = GUI_DISPLAY_PRESENT;
    message.payload = (Bytes){ present_bytes, sizeof(present_bytes) };
    message.descriptor = 7;
    EXPECT(16, gui_decode_display_present(&message, &decoded_present) ==
               CORE_OK);
    EXPECT(17, decoded_present.display_id == 1 &&
               decoded_present.generation == 2 &&
               decoded_present.damage_count == 1 &&
               decoded_present.damage[0].width == 320);
    present.flags = GUI_FULL_DAMAGE;
    EXPECT(18, gui_encode_display_present(&present,
        (Bytes){ present_bytes, sizeof(present_bytes) }, &size) ==
        CORE_INVALID_ARGUMENT);
}

static void test_display_control(void)
{
    u8 bytes[42];
    usize size;
    usize index;
    IpcMessage message;
    GuiPresenterBindRequest bind = { 0 };
    GuiPresenterBindRequest decoded_bind;
    GuiDisplayConfigureRequest configure = { 0 };
    GuiDisplayConfigureRequest decoded_configure;
    GuiBasicResult result = { CORE_WOULD_BLOCK, 33 };
    GuiBasicResult decoded_result;
    GuiDisplayPresentedEvent presented = { 33, 2, 4, 1000 };
    GuiDisplayPresentedEvent decoded_presented;
    GuiDisplayPresentFailedEvent failed = { 34, 2, 4, CORE_IO_ERROR };
    GuiDisplayPresentFailedEvent decoded_failed;

    bind.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    bind.prefix.request_id = 30;
    for (index = 0; index < GUI_PRESENTER_TOKEN_SIZE; ++index) {
        bind.presenter_token[index] = (u8)(index + 1);
    }
    EXPECT(30, gui_encode_presenter_bind(&bind, (Bytes){ bytes, sizeof(bytes) },
                                          &size) == CORE_OK && size == 42);
    message.type = GUI_DISPLAY_BIND_PRESENTER;
    message.payload = (Bytes){ bytes, size };
    message.descriptor = IPC_NO_DESCRIPTOR;
    EXPECT(31, gui_decode_presenter_bind(&message, &decoded_bind) == CORE_OK &&
               decoded_bind.presenter_token[31] == 32);

    configure.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    configure.prefix.request_id = 31;
    configure.display_id = 2;
    configure.generation = 4;
    configure.enabled = true;
    configure.orientation = GUI_ORIENTATION_RIGHT;
    configure.mode = (GuiDisplayMode){ 1080, 1920, 60000 };
    EXPECT(32, gui_encode_display_configure(&configure,
        (Bytes){ bytes, sizeof(bytes) }, &size) == CORE_OK && size == 40);
    message.type = GUI_DISPLAY_CONFIGURE;
    message.payload = (Bytes){ bytes, size };
    EXPECT(33, gui_decode_display_configure(&message, &decoded_configure) ==
               CORE_OK && decoded_configure.enabled &&
               decoded_configure.mode.height_px == 1920);

    EXPECT(34, gui_encode_basic_result(&result,
        (Bytes){ bytes, sizeof(bytes) }, &size) == CORE_OK && size == 12);
    message.type = GUI_DISPLAY_PRESENT_RESULT;
    message.payload = (Bytes){ bytes, size };
    EXPECT(35, gui_decode_basic_result(&message, GUI_DISPLAY_PRESENT_RESULT,
               &decoded_result) == CORE_OK &&
               decoded_result.error == CORE_WOULD_BLOCK);

    EXPECT(36, gui_encode_display_presented(&presented,
        (Bytes){ bytes, sizeof(bytes) }, &size) == CORE_OK && size == 32);
    message.type = GUI_DISPLAY_PRESENTED;
    message.payload = (Bytes){ bytes, size };
    EXPECT(37, gui_decode_display_presented(&message, &decoded_presented) ==
               CORE_OK && decoded_presented.presented_at_ms == 1000);

    EXPECT(38, gui_encode_display_present_failed(&failed,
        (Bytes){ bytes, sizeof(bytes) }, &size) == CORE_OK && size == 28);
    message.type = GUI_DISPLAY_PRESENT_FAILED;
    message.payload = (Bytes){ bytes, size };
    EXPECT(39, gui_decode_display_present_failed(&message, &decoded_failed) ==
               CORE_OK && decoded_failed.error == CORE_IO_ERROR);
    failed.error = CORE_OK;
    EXPECT(40, gui_encode_display_present_failed(&failed,
        (Bytes){ bytes, sizeof(bytes) }, &size) == CORE_INVALID_ARGUMENT);
}

static void test_display_information(void)
{
    u8 info_bytes[73];
    u8 list_bytes[95] = { 0 };
    usize size;
    usize consumed;
    usize first_display;
    usize index;
    GuiDisplayInfo info = { 0 };
    GuiDisplayInfo decoded;
    GuiDisplayListHeader header;
    GuiBasicResult basic = { CORE_OK, 50 };
    IpcMessage message;

    info.display_id = 3;
    info.generation = 4;
    info.flags = GUI_DISPLAY_CONNECTED | GUI_DISPLAY_ENABLED |
                 GUI_DISPLAY_PRIMARY;
    info.physical_width_mm = 68;
    info.physical_height_mm = 121;
    info.current_mode = (GuiDisplayMode){ 1080, 1920, 60000 };
    info.name_length = 5;
    info.mode_count = 2;
    info.name[0] = 'p';
    info.name[1] = 'h';
    info.name[2] = 'o';
    info.name[3] = 'n';
    info.name[4] = 'e';
    info.modes[0] = info.current_mode;
    info.modes[1] = (GuiDisplayMode){ 720, 1280, 60000 };

    EXPECT(50, gui_encode_display_info(&info,
        (Bytes){ info_bytes, sizeof(info_bytes) }, &size) == CORE_OK &&
        size == sizeof(info_bytes));
    EXPECT(51, gui_decode_display_info(
        (Bytes){ info_bytes, sizeof(info_bytes) }, &decoded, &consumed) ==
        CORE_OK && consumed == sizeof(info_bytes));
    EXPECT(52, decoded.display_id == 3 && decoded.mode_count == 2 &&
               decoded.name[4] == 'e' && decoded.modes[1].width_px == 720);

    EXPECT(53, gui_encode_basic_result(&basic,
        (Bytes){ list_bytes, sizeof(list_bytes) }, &size) == CORE_OK);
    list_bytes[12] = 9;
    list_bytes[20] = 1;
    for (index = 0; index < sizeof(info_bytes); ++index) {
        list_bytes[22 + index] = info_bytes[index];
    }
    message.type = GUI_DISPLAY_LIST_RESULT;
    message.payload = (Bytes){ list_bytes, sizeof(list_bytes) };
    message.descriptor = IPC_NO_DESCRIPTOR;
    EXPECT(54, gui_decode_display_list_header(&message, &header,
                                               &first_display) == CORE_OK);
    EXPECT(55, header.result.request_id == 50 && header.snapshot_serial == 9 &&
               header.display_count == 1 && first_display == 22);

    info.modes[1] = info.current_mode;
    EXPECT(56, gui_encode_display_info(&info,
        (Bytes){ info_bytes, sizeof(info_bytes) }, &size) ==
        CORE_INVALID_ARGUMENT);
}

int main(void)
{
    test_buffer_import();
    test_damage_and_present();
    test_display_control();
    test_display_information();
    check_passed();
    return 0;
}
