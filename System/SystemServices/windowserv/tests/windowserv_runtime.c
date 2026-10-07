#define CHECK_TEST_NAME "windowserv"
#include "../../../SystemLibs/testing/check.h"

#include <windowserv.h>
#include <special/ipc.h>

#define TEST_LINUX_ABS_X ((u16)0)
#define TEST_LINUX_ABS_Y ((u16)1)
#define TEST_LINUX_BTN_TOUCH ((u16)330)

static ErrorCode connect_client(const String *endpoint,
                                WindowServiceHandle service,
                                IpcConnectionHandle *out_client)
{
    IpcConnectionState state = IPC_CONNECTION_CONNECTING;
    usize attempt;
    ErrorCode result = ipc_connection_begin_connect(endpoint, out_client);

    if (result != CORE_OK) return result;
    for (attempt = 0; attempt < 1000 && state == IPC_CONNECTION_CONNECTING;
         ++attempt) {
        result = window_service_poll(service, 0);
        if (result != CORE_OK) return result;
        result = ipc_connection_finish_connect(*out_client, &state);
        if (result != CORE_OK) return result;
    }
    return state == IPC_CONNECTION_OPEN ? CORE_OK : CORE_WOULD_BLOCK;
}

static ErrorCode send_message(IpcConnectionHandle client, IpcMessageType type,
                              u8 *payload, usize size)
{
    IpcMessage message = { type, 0, { payload, size }, IPC_NO_DESCRIPTOR, 0 };
    bool pending;
    ErrorCode result = ipc_connection_send(client, &message);

    if (result != CORE_OK) return result;
    return ipc_connection_flush(client, &pending);
}

static ErrorCode receive_message_type(WindowServiceHandle service,
                                      IpcConnectionHandle client,
                                      IpcMessageType type,
                                      IpcMessage *out_message)
{
    usize attempt;

    for (attempt = 0; attempt < 2000; ++attempt) {
        IpcReceiveStatus status;
        IpcMessage message;
        ErrorCode result = window_service_poll(service, 0);
        if (result != CORE_OK) return result;
        result = ipc_connection_receive(client, &status, &message);
        if (result != CORE_OK) return result;
        if (status == IPC_RECEIVE_PEER_CLOSED) return CORE_IO_ERROR;
        if (status == IPC_RECEIVE_NO_MESSAGE) continue;
        if (message.type == type) {
            *out_message = message;
            return CORE_OK;
        }
        (void)ipc_message_destroy(&message);
    }
    return CORE_WOULD_BLOCK;
}

static ErrorCode accept_peer(IpcServerHandle server,
                             WindowServiceHandle service,
                             IpcConnectionHandle *out_peer)
{
    usize attempt;

    for (attempt = 0; attempt < 2000; ++attempt) {
        ErrorCode result = window_service_poll(service, 0);
        if (result != CORE_OK) return result;
        result = ipc_server_accept(server, out_peer);
        if (result == CORE_OK) return CORE_OK;
        if (result != CORE_WOULD_BLOCK) return result;
    }
    return CORE_WOULD_BLOCK;
}

static void fill_token(u8 *token, usize size, u8 start)
{
    usize index;

    for (index = 0; index < size; ++index) {
        token[index] = (u8)(start + index);
    }
}

static void test_window_lifecycle_input_and_cleanup(void)
{
    String endpoint;
    WindowServiceHandle service;
    IpcConnectionHandle app;
    IpcConnectionHandle app2;
    IpcConnectionHandle shell;
    u8 shell_token[WINDOW_SERVICE_SHELL_TOKEN_SIZE];
    u8 payload[320];
    usize size;
    IpcMessage message;
    GuiShellBindRequest bind = { 0 };
    GuiBasicResult basic;
    GuiWindowCreateRequest create = { 0 };
    GuiWindowCreateResult created;
    GuiWindowIdRequest id_request;
    GuiShellActivateRequest activate;
    GuiWindowLifecycleEvent lifecycle;
    GuiWindowConfigureEvent configure;
    GuiWindowTouchEvent touch;
    GuiWindowTouchEvent decoded_touch;
    u64 window_id;

    fill_token(shell_token, WINDOW_SERVICE_SHELL_TOKEN_SIZE, 1);
    EXPECT(1, string_from_c_str("/tmp/punos-windowserv-runtime",
                                &endpoint) == CORE_OK);
    EXPECT(2, window_service_create(&endpoint, shell_token, &service) ==
              CORE_OK);
    EXPECT(3, connect_client(&endpoint, service, &app) == CORE_OK);
    EXPECT(4, connect_client(&endpoint, service, &shell) == CORE_OK);

    bind.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    bind.prefix.request_id = 1;
    fill_token(bind.shell_token, GUI_SHELL_TOKEN_SIZE, 1);
    EXPECT(5, gui_encode_shell_bind(&bind, (Bytes){ payload, sizeof(payload) },
                                    &size) == CORE_OK);
    EXPECT(6, send_message(shell, GUI_WINDOW_BIND_SHELL, payload, size) ==
              CORE_OK);
    EXPECT(7, receive_message_type(service, shell,
                                   GUI_WINDOW_BIND_SHELL_RESULT,
                                   &message) == CORE_OK);
    EXPECT(8, gui_decode_basic_result(&message, GUI_WINDOW_BIND_SHELL_RESULT,
                                      &basic) == CORE_OK &&
              basic.error == CORE_OK);
    EXPECT(9, ipc_message_destroy(&message) == CORE_OK);

    create.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    create.prefix.request_id = 2;
    create.surface_id = 100;
    fill_token(create.surface_bind_token, GUI_SURFACE_BIND_TOKEN_SIZE, 30);
    create.role = GUI_WINDOW_ROLE_APP;
    create.flags = GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD |
                   GUI_WINDOW_FLAG_HANDLES_BACK;
    create.title_length = 4;
    create.title[0] = 'm';
    create.title[1] = 'a';
    create.title[2] = 'i';
    create.title[3] = 'n';
    EXPECT(10, gui_encode_window_create(&create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(11, send_message(app, GUI_WINDOW_CREATE, payload, size) == CORE_OK);
    EXPECT(12, receive_message_type(service, app, GUI_WINDOW_CREATE_RESULT,
                                    &message) == CORE_OK);
    EXPECT(13, gui_decode_window_create_result(&message, &created) == CORE_OK &&
               created.result.error == CORE_OK && created.window_id != 0);
    window_id = created.window_id;
    EXPECT(14, ipc_message_destroy(&message) == CORE_OK);

    id_request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    id_request.prefix.request_id = 3;
    id_request.window_id = window_id;
    EXPECT(15, gui_encode_window_id_request(&id_request,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(16, send_message(app, GUI_WINDOW_SHOW, payload, size) == CORE_OK);
    EXPECT(17, receive_message_type(service, app,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(18, gui_decode_basic_result(&message, GUI_WINDOW_OPERATION_RESULT,
                                       &basic) == CORE_OK &&
               basic.error == CORE_OK);
    EXPECT(19, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(20, receive_message_type(service, app, GUI_WINDOW_CONFIGURE,
                                    &message) == CORE_OK);
    EXPECT(21, gui_decode_window_configure(&message, &configure) == CORE_OK &&
               configure.window_id == window_id &&
               configure.orientation == GUI_ORIENTATION_NORMAL);
    EXPECT(22, ipc_message_destroy(&message) == CORE_OK);

    activate.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    activate.prefix.request_id = 4;
    activate.window_id = window_id;
    EXPECT(23, gui_encode_shell_activate(&activate,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(24, send_message(shell, GUI_SHELL_ACTIVATE, payload, size) ==
               CORE_OK);
    EXPECT(25, receive_message_type(service, shell,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(26, gui_decode_basic_result(&message, GUI_WINDOW_OPERATION_RESULT,
                                       &basic) == CORE_OK &&
               basic.error == CORE_OK);
    EXPECT(27, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(28, receive_message_type(service, app,
                                    GUI_WINDOW_LIFECYCLE_CHANGED,
                                    &message) == CORE_OK);
    EXPECT(29, gui_decode_window_lifecycle(&message, &lifecycle) == CORE_OK &&
               lifecycle.lifecycle == GUI_WINDOW_LIFECYCLE_FOREGROUND);
    EXPECT(30, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(31, window_service_set_display_metrics(service, 1920, 1080,
        GUI_SCALE_1X, GUI_ORIENTATION_LEFT,
        (GuiInsets){ 10, 20, 30, 40 }) == CORE_OK);
    EXPECT(32, receive_message_type(service, app, GUI_WINDOW_CONFIGURE,
                                    &message) == CORE_OK);
    EXPECT(33, gui_decode_window_configure(&message, &configure) == CORE_OK &&
               configure.orientation == GUI_ORIENTATION_LEFT &&
               configure.safe_insets.left == 40);
    EXPECT(34, ipc_message_destroy(&message) == CORE_OK);

    touch.window_id = 0;
    touch.input_serial = 50;
    touch.timestamp_ms = 1000;
    touch.touch_id = 1;
    touch.phase = GUI_TOUCH_DOWN;
    touch.x = 9 << 8;
    touch.y = 8 << 8;
    EXPECT(35, window_service_route_touch(service, &touch) == CORE_OK);
    EXPECT(36, receive_message_type(service, app, GUI_WINDOW_TOUCH,
                                    &message) == CORE_OK);
    EXPECT(37, gui_decode_window_touch(&message, &decoded_touch) == CORE_OK &&
               decoded_touch.window_id == window_id &&
               decoded_touch.x == (9 << 8));
    EXPECT(38, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(39, connect_client(&endpoint, service, &app2) == CORE_OK);
    id_request.prefix.request_id = 5;
    id_request.window_id = window_id;
    EXPECT(40, gui_encode_window_id_request(&id_request,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(41, send_message(app2, GUI_WINDOW_DESTROY, payload, size) ==
               CORE_OK);
    EXPECT(42, receive_message_type(service, app2, GUI_WINDOW_DESTROY_RESULT,
                                    &message) == CORE_OK);
    EXPECT(43, gui_decode_basic_result(&message, GUI_WINDOW_DESTROY_RESULT,
                                       &basic) == CORE_OK &&
               basic.error == CORE_PERMISSION_DENIED);
    EXPECT(44, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(45, ipc_connection_close(&app) == CORE_OK);
    EXPECT(46, window_service_poll(service, 0) == CORE_OK);
    activate.prefix.request_id = 6;
    EXPECT(47, gui_encode_shell_activate(&activate,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(48, send_message(shell, GUI_SHELL_ACTIVATE, payload, size) ==
               CORE_OK);
    EXPECT(49, receive_message_type(service, shell,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(50, gui_decode_basic_result(&message, GUI_WINDOW_OPERATION_RESULT,
                                       &basic) == CORE_OK &&
               basic.error == CORE_NOT_FOUND);
    EXPECT(51, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(52, ipc_connection_close(&app2) == CORE_OK);
    EXPECT(53, ipc_connection_close(&shell) == CORE_OK);
    EXPECT(54, window_service_close(&service) == CORE_OK);
    EXPECT(55, string_destroy(&endpoint) == CORE_OK);
}

static void test_io_input_frame_routing(void)
{
    String endpoint;
    String io_endpoint;
    IpcServerHandle io_server;
    IpcConnectionHandle io_peer;
    WindowServiceHandle service;
    WindowServiceConfig config;
    IpcConnectionHandle app;
    IpcConnectionHandle shell;
    u8 shell_token[WINDOW_SERVICE_SHELL_TOKEN_SIZE];
    u8 payload[320];
    usize size;
    IpcMessage message;
    GuiIoSubscribeRequest subscribe;
    GuiIoResult io_result = { CORE_OK };
    GuiShellBindRequest bind = { 0 };
    GuiWindowCreateRequest create = { 0 };
    GuiWindowCreateResult created;
    GuiWindowIdRequest show;
    GuiShellActivateRequest activate;
    GuiBasicResult basic;
    GuiIoInputFrameEvent frame = { 0 };
    GuiWindowTouchEvent touch;

    fill_token(shell_token, WINDOW_SERVICE_SHELL_TOKEN_SIZE, 70);
    EXPECT(100, string_from_c_str("/tmp/punos-windowserv-io-window",
                                  &endpoint) == CORE_OK);
    EXPECT(101, string_from_c_str("/tmp/punos-windowserv-io-fake",
                                  &io_endpoint) == CORE_OK);
    EXPECT(102, ipc_server_create(&io_endpoint, &io_server) == CORE_OK);

    config.endpoint = &endpoint;
    config.compositor_endpoint = NULL;
    config.io_endpoint = &io_endpoint;
    config.controller_token = NULL;
    config.shell_token = shell_token;
    config.viewport_width = 100;
    config.viewport_height = 200;
    config.scale = GUI_SCALE_1X;
    config.orientation = GUI_ORIENTATION_NORMAL;
    config.safe_insets = (GuiInsets){ 0, 0, 0, 0 };
    EXPECT(103, window_service_create_config(&config, &service) == CORE_OK);
    EXPECT(104, accept_peer(io_server, service, &io_peer) == CORE_OK);
    EXPECT(105, receive_message_type(service, io_peer, GUI_IO_SUBSCRIBE,
                                     &message) == CORE_OK);
    EXPECT(106, gui_decode_io_subscribe(&message, &subscribe) == CORE_OK &&
                (subscribe.device_class_mask & GUI_INPUT_DEVICE_TOUCH) != 0);
    EXPECT(107, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(108, gui_encode_io_result(&io_result,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(109, send_message(io_peer, GUI_IO_SUBSCRIBE_RESULT, payload, size) ==
                CORE_OK);
    EXPECT(110, window_service_poll(service, 0) == CORE_OK);

    EXPECT(111, connect_client(&endpoint, service, &app) == CORE_OK);
    EXPECT(112, connect_client(&endpoint, service, &shell) == CORE_OK);
    bind.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    bind.prefix.request_id = 1;
    fill_token(bind.shell_token, GUI_SHELL_TOKEN_SIZE, 70);
    EXPECT(113, gui_encode_shell_bind(&bind,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(114, send_message(shell, GUI_WINDOW_BIND_SHELL, payload, size) ==
                CORE_OK);
    EXPECT(115, receive_message_type(service, shell,
                                     GUI_WINDOW_BIND_SHELL_RESULT,
                                     &message) == CORE_OK);
    EXPECT(116, ipc_message_destroy(&message) == CORE_OK);

    create.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    create.prefix.request_id = 2;
    create.surface_id = 200;
    fill_token(create.surface_bind_token, GUI_SURFACE_BIND_TOKEN_SIZE, 80);
    create.role = GUI_WINDOW_ROLE_APP;
    create.flags = GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD;
    EXPECT(117, gui_encode_window_create(&create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(118, send_message(app, GUI_WINDOW_CREATE, payload, size) == CORE_OK);
    EXPECT(119, receive_message_type(service, app, GUI_WINDOW_CREATE_RESULT,
                                     &message) == CORE_OK);
    EXPECT(120, gui_decode_window_create_result(&message, &created) ==
                CORE_OK && created.result.error == CORE_OK);
    EXPECT(121, ipc_message_destroy(&message) == CORE_OK);

    show.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    show.prefix.request_id = 3;
    show.window_id = created.window_id;
    EXPECT(122, gui_encode_window_id_request(&show,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(123, send_message(app, GUI_WINDOW_SHOW, payload, size) == CORE_OK);
    EXPECT(124, receive_message_type(service, app,
                                     GUI_WINDOW_OPERATION_RESULT,
                                     &message) == CORE_OK);
    EXPECT(125, gui_decode_basic_result(&message, GUI_WINDOW_OPERATION_RESULT,
                                        &basic) == CORE_OK &&
                basic.error == CORE_OK);
    EXPECT(126, ipc_message_destroy(&message) == CORE_OK);

    activate.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    activate.prefix.request_id = 4;
    activate.window_id = created.window_id;
    EXPECT(127, gui_encode_shell_activate(&activate,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(128, send_message(shell, GUI_SHELL_ACTIVATE, payload, size) ==
                CORE_OK);
    EXPECT(129, receive_message_type(service, shell,
                                     GUI_WINDOW_OPERATION_RESULT,
                                     &message) == CORE_OK);
    EXPECT(130, ipc_message_destroy(&message) == CORE_OK);

    frame.device_id = 9;
    frame.sequence = 1;
    frame.event_count = 3;
    frame.events[0] = (GuiIoInputEvent){ GUI_IO_INPUT_ABSOLUTE_AXIS,
                                         TEST_LINUX_ABS_X, 12 };
    frame.events[1] = (GuiIoInputEvent){ GUI_IO_INPUT_ABSOLUTE_AXIS,
                                         TEST_LINUX_ABS_Y, 34 };
    frame.events[2] = (GuiIoInputEvent){ GUI_IO_INPUT_KEY,
                                         TEST_LINUX_BTN_TOUCH, 1 };
    EXPECT(131, gui_encode_io_input_frame(&frame,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(132, send_message(io_peer, GUI_IO_INPUT_FRAME, payload, size) ==
                CORE_OK);
    EXPECT(133, receive_message_type(service, app, GUI_WINDOW_TOUCH,
                                     &message) == CORE_OK);
    EXPECT(134, gui_decode_window_touch(&message, &touch) == CORE_OK &&
                touch.window_id == created.window_id &&
                touch.phase == GUI_TOUCH_DOWN && touch.x == (12 << 8));
    EXPECT(135, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(136, ipc_connection_close(&app) == CORE_OK);
    EXPECT(137, ipc_connection_close(&shell) == CORE_OK);
    EXPECT(138, ipc_connection_close(&io_peer) == CORE_OK);
    EXPECT(139, ipc_server_close(&io_server) == CORE_OK);
    EXPECT(140, window_service_close(&service) == CORE_OK);
    EXPECT(141, string_destroy(&io_endpoint) == CORE_OK);
    EXPECT(142, string_destroy(&endpoint) == CORE_OK);
}

int main(void)
{
    test_window_lifecycle_input_and_cleanup();
    test_io_input_frame_routing();
    check_passed();
    return 0;
}
