#define CHECK_TEST_NAME "displayserv"
#include "../../../SystemLibs/testing/check.h"

#include <displayserv.h>
#include <gui/protocol.h>
#include <special/ipc.h>
#include <sys/memory.h>

static void test_list_request(void)
{
    String endpoint;
    DisplayServiceHandle service;
    IpcConnectionHandle client;
    IpcConnectionState state = IPC_CONNECTION_STATE_CONNECTING;
    u8 token[32] = { 1 };
    u8 payload[56];
    usize payload_size;
    IpcMessage outgoing;
    IpcMessage incoming;
    IpcReceiveStatus receive_status = IPC_RECEIVE_STATUS_NO_MESSAGE;
    GuiDisplayListHeader header;
    usize first_display;
    bool pending = true;
    usize attempt;
    GuiPresenterBindRequest bind = { 0 };
    GuiBasicResult basic;
    SharedMemory shared;
    GuiDisplayPresentRequest present = { 0 };
    GuiDisplayPresentedEvent presented;
    GuiDisplayConfigureRequest configure = { 0 };
    GuiDisplayInfo configured_info;
    u64 changed_serial;
    i32 present_fd;

    EXPECT(1, string_from_c_str("/tmp/punos-displayserv-test", &endpoint) ==
              CORE_OK);
    EXPECT(2, display_service_create(&endpoint, token, &service) == CORE_OK);
    EXPECT(3, ipc_connection_begin_connect(&endpoint, &client) == CORE_OK);
    for (attempt = 0; attempt < 1000 && state == IPC_CONNECTION_STATE_CONNECTING;
         ++attempt) {
        EXPECT(4, display_service_poll(service, 0) == CORE_OK);
        EXPECT(5, ipc_connection_finish_connect(client, &state) == CORE_OK);
    }
    EXPECT(6, state == IPC_CONNECTION_STATE_OPEN);
    EXPECT(7, gui_encode_versioned_request(
        (GuiRequestPrefix){ GUI_PROTOCOL_VERSION, 10 },
        (Bytes){ payload, sizeof(payload) }, &payload_size) == CORE_OK);
    outgoing = (IpcMessage){ GUI_DISPLAY_LIST, { payload, payload_size } };
    EXPECT(8, ipc_connection_send(client, &outgoing) == CORE_OK);
    for (attempt = 0; attempt < 1000 && pending; ++attempt) {
        EXPECT(9, ipc_connection_flush(client, &pending) == CORE_OK);
    }
    for (attempt = 0;
         attempt < 1000 && receive_status == IPC_RECEIVE_STATUS_NO_MESSAGE;
         ++attempt) {
        EXPECT(10, display_service_poll(service, 0) == CORE_OK);
        EXPECT(11, ipc_connection_receive(client, &receive_status, &incoming) ==
                   CORE_OK);
    }
    EXPECT(12, receive_status == IPC_RECEIVE_STATUS_MESSAGE &&
               incoming.type == GUI_DISPLAY_LIST_RESULT);
    EXPECT(13, gui_decode_display_list_header(&incoming, &header,
                                               &first_display) == CORE_OK);
    EXPECT(14, header.result.request_id == 10 &&
               header.result.error == CORE_OK && header.display_count == 1 &&
               first_display == 22);
    EXPECT(15, ipc_message_destroy(&incoming) == CORE_OK);

    bind.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION, 11 };
    bind.presenter_token[0] = 1;
    EXPECT(16, gui_encode_presenter_bind(&bind,
        (Bytes){ payload, sizeof(payload) }, &payload_size) == CORE_OK);
    outgoing = (IpcMessage){ GUI_DISPLAY_BIND_PRESENTER, { payload, payload_size } };
    EXPECT(17, ipc_connection_send(client, &outgoing) == CORE_OK);
    pending = true;
    for (attempt = 0; attempt < 1000 && pending; ++attempt) {
        EXPECT(18, ipc_connection_flush(client, &pending) == CORE_OK);
    }
    receive_status = IPC_RECEIVE_STATUS_NO_MESSAGE;
    for (attempt = 0; attempt < 1000 && receive_status == IPC_RECEIVE_STATUS_NO_MESSAGE;
         ++attempt) {
        EXPECT(19, display_service_poll(service, 0) == CORE_OK);
        EXPECT(20, ipc_connection_receive(client, &receive_status, &incoming) ==
                   CORE_OK);
    }
    EXPECT(21, receive_status == IPC_RECEIVE_STATUS_MESSAGE &&
               gui_decode_basic_result(&incoming,
                   GUI_DISPLAY_BIND_PRESENTER_RESULT, &basic) == CORE_OK &&
               basic.error == CORE_OK);
    EXPECT(22, ipc_message_destroy(&incoming) == CORE_OK);

    EXPECT(200, gui_encode_versioned_request(
        (GuiRequestPrefix){ GUI_PROTOCOL_VERSION, 12 },
        (Bytes){ payload, sizeof(payload) }, &payload_size) == CORE_OK);
    outgoing = (IpcMessage){ GUI_DISPLAY_SUBSCRIBE, { payload, payload_size } };
    EXPECT(201, ipc_connection_send(client, &outgoing) == CORE_OK);
    pending = true;
    while (pending) EXPECT(202, ipc_connection_flush(client, &pending) == CORE_OK);
    receive_status = IPC_RECEIVE_STATUS_NO_MESSAGE;
    while (receive_status == IPC_RECEIVE_STATUS_NO_MESSAGE) {
        EXPECT(203, display_service_poll(service, 0) == CORE_OK);
        EXPECT(204, ipc_connection_receive(client, &receive_status, &incoming) ==
                    CORE_OK);
    }
    EXPECT(205, gui_decode_basic_result(&incoming,
        GUI_DISPLAY_SUBSCRIBE_RESULT, &basic) == CORE_OK &&
        basic.error == CORE_OK);
    EXPECT(206, ipc_message_destroy(&incoming) == CORE_OK);

    configure.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION, 13 };
    configure.display_id = 1;
    configure.generation = 1;
    configure.enabled = true;
    configure.orientation = GUI_ORIENTATION_NORMAL;
    configure.mode = (GuiDisplayMode){ 1080, 1920, 60000 };
    EXPECT(207, gui_encode_display_configure(&configure,
        (Bytes){ payload, sizeof(payload) }, &payload_size) == CORE_OK);
    outgoing = (IpcMessage){ GUI_DISPLAY_CONFIGURE, { payload, payload_size } };
    EXPECT(208, ipc_connection_send(client, &outgoing) == CORE_OK);
    pending = true;
    while (pending) EXPECT(209, ipc_connection_flush(client, &pending) == CORE_OK);
    receive_status = IPC_RECEIVE_STATUS_NO_MESSAGE;
    while (receive_status == IPC_RECEIVE_STATUS_NO_MESSAGE) {
        EXPECT(210, display_service_poll(service, 0) == CORE_OK);
        EXPECT(211, ipc_connection_receive(client, &receive_status, &incoming) ==
                    CORE_OK);
    }
    EXPECT(212, gui_decode_display_info_result(&incoming,
        GUI_DISPLAY_CONFIGURE_RESULT, &basic, &configured_info) == CORE_OK &&
        basic.error == CORE_OK && configured_info.generation == 2);
    EXPECT(213, ipc_message_destroy(&incoming) == CORE_OK);
    receive_status = IPC_RECEIVE_STATUS_NO_MESSAGE;
    while (receive_status == IPC_RECEIVE_STATUS_NO_MESSAGE) {
        EXPECT(214, display_service_poll(service, 0) == CORE_OK);
        EXPECT(215, ipc_connection_receive(client, &receive_status, &incoming) ==
                    CORE_OK);
    }
    EXPECT(216, gui_decode_display_info_event(&incoming, GUI_DISPLAY_CHANGED,
        &changed_serial, &configured_info) == CORE_OK && changed_serial == 2 &&
        configured_info.generation == 2);
    EXPECT(217, ipc_message_destroy(&incoming) == CORE_OK);

    EXPECT(23, memory_shared_create(8294400, &shared) == CORE_OK);
    present.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION, 14 };
    present.display_id = 1;
    present.generation = 2;
    present.width = 1080;
    present.height = 1920;
    present.stride = 4320;
    present.pixel_format = GUI_PIXEL_XRGB8888;
    present.byte_size = 8294400;
    present.flags = GUI_FULL_DAMAGE;
    EXPECT(24, gui_encode_display_present(&present,
        (Bytes){ payload, sizeof(payload) }, &payload_size) == CORE_OK);
    present_fd = fcntl(shared.fd, F_DUPFD_CLOEXEC, 0);
    EXPECT(25, present_fd >= 0);
    outgoing = (IpcMessage){ GUI_DISPLAY_PRESENT,
                             { payload, payload_size } };
    EXPECT(26, ipc_connection_send_with_descriptor(client, &outgoing,
                                                     present_fd) == CORE_OK);
    EXPECT(27, close(present_fd) == 0);
    pending = true;
    for (attempt = 0; attempt < 1000 && pending; ++attempt) {
        EXPECT(28, ipc_connection_flush(client, &pending) == CORE_OK);
    }
    receive_status = IPC_RECEIVE_STATUS_NO_MESSAGE;
    for (attempt = 0; attempt < 1000 && receive_status == IPC_RECEIVE_STATUS_NO_MESSAGE;
         ++attempt) {
        EXPECT(29, display_service_poll(service, 0) == CORE_OK);
        EXPECT(30, ipc_connection_receive(client, &receive_status, &incoming) ==
                   CORE_OK);
    }
    EXPECT(31, receive_status == IPC_RECEIVE_STATUS_MESSAGE &&
               gui_decode_basic_result(&incoming, GUI_DISPLAY_PRESENT_RESULT,
                                        &basic) == CORE_OK &&
               basic.error == CORE_OK);
    EXPECT(32, ipc_message_destroy(&incoming) == CORE_OK);
    receive_status = IPC_RECEIVE_STATUS_NO_MESSAGE;
    for (attempt = 0; attempt < 1000 && receive_status == IPC_RECEIVE_STATUS_NO_MESSAGE;
         ++attempt) {
        EXPECT(33, display_service_poll(service, 0) == CORE_OK);
        EXPECT(34, ipc_connection_receive(client, &receive_status, &incoming) ==
                   CORE_OK);
    }
    EXPECT(35, receive_status == IPC_RECEIVE_STATUS_MESSAGE &&
               gui_decode_display_presented(&incoming, &presented) == CORE_OK &&
               presented.request_id == 14 && presented.generation == 2);
    EXPECT(36, ipc_message_destroy(&incoming) == CORE_OK);
    EXPECT(37, memory_shared_close(&shared) == CORE_OK);
    EXPECT(38, ipc_connection_close(&client) == CORE_OK);
    EXPECT(39, display_service_poll(service, 0) == CORE_OK);
    EXPECT(40, display_service_close(&service) == CORE_OK);
    EXPECT(41, string_destroy(&endpoint) == CORE_OK);
}

static void test_invalid_presenter_token_limit(void)
{
    String endpoint;
    DisplayServiceHandle service;
    IpcConnectionHandle client;
    IpcConnectionState state = IPC_CONNECTION_STATE_CONNECTING;
    u8 expected_token[32] = { 7 };
    u8 payload[48];
    usize payload_size;
    usize attempt;
    usize bind_attempt;
    bool pending;
    IpcReceiveStatus receive_status;
    IpcMessage incoming;

    EXPECT(100, string_from_c_str("/tmp/punos-displayserv-token-test",
                                  &endpoint) == CORE_OK);
    EXPECT(101, display_service_create(&endpoint, expected_token, &service) ==
                CORE_OK);
    EXPECT(102, ipc_connection_begin_connect(&endpoint, &client) == CORE_OK);
    for (attempt = 0; attempt < 1000 && state == IPC_CONNECTION_STATE_CONNECTING;
         ++attempt) {
        EXPECT(103, display_service_poll(service, 0) == CORE_OK);
        EXPECT(104, ipc_connection_finish_connect(client, &state) == CORE_OK);
    }
    EXPECT(105, state == IPC_CONNECTION_STATE_OPEN);

    for (bind_attempt = 0; bind_attempt < 3; ++bind_attempt) {
        GuiPresenterBindRequest bind = { 0 };
        IpcMessage outgoing;
        bind.prefix.protocol_version = GUI_PROTOCOL_VERSION;
        bind.prefix.request_id = bind_attempt + 1;
        EXPECT(106, gui_encode_presenter_bind(&bind,
            (Bytes){ payload, sizeof(payload) }, &payload_size) == CORE_OK);
        outgoing = (IpcMessage){ GUI_DISPLAY_BIND_PRESENTER, { payload, payload_size } };
        EXPECT(107, ipc_connection_send(client, &outgoing) == CORE_OK);
        pending = true;
        for (attempt = 0; attempt < 1000 && pending; ++attempt) {
            EXPECT(108, ipc_connection_flush(client, &pending) == CORE_OK);
        }
        EXPECT(109, display_service_poll(service, 0) == CORE_OK);
        receive_status = IPC_RECEIVE_STATUS_NO_MESSAGE;
        for (attempt = 0;
             attempt < 1000 && receive_status == IPC_RECEIVE_STATUS_NO_MESSAGE;
             ++attempt) {
            EXPECT(110, ipc_connection_receive(client, &receive_status,
                                                &incoming) == CORE_OK);
        }
        if (bind_attempt < 2) {
            EXPECT(111, receive_status == IPC_RECEIVE_STATUS_MESSAGE);
            EXPECT(116, ipc_message_destroy(&incoming) == CORE_OK);
        } else {
            EXPECT(112, receive_status == IPC_RECEIVE_STATUS_PEER_CLOSED);
        }
    }
    EXPECT(113, ipc_connection_close(&client) == CORE_OK);
    EXPECT(114, display_service_close(&service) == CORE_OK);
    EXPECT(115, string_destroy(&endpoint) == CORE_OK);
}

int main(void)
{
    test_list_request();
    test_invalid_presenter_token_limit();
    check_passed();
    return 0;
}
