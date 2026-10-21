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
    IpcConnectionState state = IPC_CONNECTION_STATE_CONNECTING;
    usize attempt;
    ErrorCode result = ipc_connection_begin_connect(endpoint, out_client);

    if (result != CORE_OK) return result;
    for (attempt = 0; attempt < 1000 && state == IPC_CONNECTION_STATE_CONNECTING;
         ++attempt) {
        result = window_service_poll(service, 0);
        if (result != CORE_OK) return result;
        result = ipc_connection_finish_connect(*out_client, &state);
        if (result != CORE_OK) return result;
    }
    return state == IPC_CONNECTION_STATE_OPEN ? CORE_OK : CORE_WOULD_BLOCK;
}

static ErrorCode send_message(IpcConnectionHandle client, IpcMessageType type,
                              u8 *payload, usize size)
{
    IpcMessage message = { type, { payload, size } };
    bool pending;
    ErrorCode result = ipc_connection_send(client, &message);

    if (result != CORE_OK) return result;
    return ipc_connection_flush(client, &pending);
}

static ErrorCode send_basic_message(IpcConnectionHandle client,
                                    IpcMessageType type,
                                    ErrorCode error, u64 request_id)
{
    GuiBasicResult basic = { error, request_id };
    u8 payload[64];
    usize size;
    ErrorCode result = gui_encode_basic_result(
        &basic, (Bytes){ payload, sizeof(payload) }, &size);

    if (result != CORE_OK) return result;
    return send_message(client, type, payload, size);
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
        if (status == IPC_RECEIVE_STATUS_PEER_CLOSED) return CORE_IO_ERROR;
        if (status == IPC_RECEIVE_STATUS_NO_MESSAGE) continue;
        if (message.type == type) {
            *out_message = message;
            return CORE_OK;
        }
        (void)ipc_message_destroy(&message);
    }
    return CORE_WOULD_BLOCK;
}

static ErrorCode receive_visibility_for(WindowServiceHandle service,
                                        IpcConnectionHandle client,
                                        u64 window_id, bool visible)
{
    usize attempt;

    for (attempt = 0; attempt < 2000; ++attempt) {
        IpcReceiveStatus status;
        IpcMessage message;
        ErrorCode result = window_service_poll(service, 0);
        if (result != CORE_OK) return result;
        result = ipc_connection_receive(client, &status, &message);
        if (result != CORE_OK) return result;
        if (status == IPC_RECEIVE_STATUS_PEER_CLOSED) return CORE_IO_ERROR;
        if (status == IPC_RECEIVE_STATUS_NO_MESSAGE) continue;
        if (message.type == GUI_WINDOW_VISIBILITY_CHANGED) {
            GuiWindowVisibilityEvent event;
            result = gui_decode_window_visibility(&message, &event);
            if (result != CORE_OK) {
                (void)ipc_message_destroy(&message);
                return result;
            }
            if (event.window_id == window_id && event.visible == visible) {
                (void)ipc_message_destroy(&message);
                return CORE_OK;
            }
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

    EXPECT(31, receive_message_type(service, app, GUI_WINDOW_CONFIGURE,
                                    &message) == CORE_OK);
    EXPECT(32, gui_decode_window_configure(&message, &configure) == CORE_OK &&
               configure.window_id == window_id &&
               configure.orientation == GUI_ORIENTATION_NORMAL);
    EXPECT(33, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(34, window_service_set_display_metrics(service, 1920, 1080,
        GUI_SCALE_1X, GUI_ORIENTATION_LEFT,
        (GuiInsets){ 10, 20, 30, 40 }) == CORE_OK);
    EXPECT(35, receive_message_type(service, app, GUI_WINDOW_CONFIGURE,
                                    &message) == CORE_OK);
    EXPECT(36, gui_decode_window_configure(&message, &configure) == CORE_OK &&
               configure.orientation == GUI_ORIENTATION_LEFT &&
               configure.safe_insets.left == 40);
    EXPECT(37, ipc_message_destroy(&message) == CORE_OK);

    touch.window_id = 0;
    touch.input_serial = 50;
    touch.timestamp_ms = 1000;
    touch.touch_id = 1;
    touch.phase = GUI_TOUCH_DOWN;
    touch.x = 9 << 8;
    touch.y = 8 << 8;
    EXPECT(38, window_service_route_touch(service, &touch) == CORE_OK);
    EXPECT(39, receive_message_type(service, app, GUI_WINDOW_TOUCH,
                                    &message) == CORE_OK);
    EXPECT(40, gui_decode_window_touch(&message, &decoded_touch) == CORE_OK &&
               decoded_touch.window_id == window_id &&
               decoded_touch.x == (9 << 8));
    EXPECT(41, ipc_message_destroy(&message) == CORE_OK);

    id_request.prefix.request_id = 5;
    id_request.window_id = window_id;
    EXPECT(59, gui_encode_window_id_request(&id_request,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(60, send_message(app, GUI_WINDOW_HIDE, payload, size) == CORE_OK);
    EXPECT(61, receive_message_type(service, app, GUI_WINDOW_TOUCH,
                                    &message) == CORE_OK);
    EXPECT(62, gui_decode_window_touch(&message, &decoded_touch) == CORE_OK &&
               decoded_touch.window_id == window_id &&
               decoded_touch.phase == GUI_TOUCH_CANCEL);
    EXPECT(63, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(42, connect_client(&endpoint, service, &app2) == CORE_OK);
    id_request.prefix.request_id = 6;
    id_request.window_id = window_id;
    EXPECT(43, gui_encode_window_id_request(&id_request,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(44, send_message(app2, GUI_WINDOW_DESTROY, payload, size) ==
               CORE_OK);
    EXPECT(45, receive_message_type(service, app2, GUI_WINDOW_DESTROY_RESULT,
                                    &message) == CORE_OK);
    EXPECT(46, gui_decode_basic_result(&message, GUI_WINDOW_DESTROY_RESULT,
                                       &basic) == CORE_OK &&
               basic.error == CORE_PERMISSION_DENIED);
    EXPECT(47, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(48, ipc_connection_close(&app) == CORE_OK);
    EXPECT(49, window_service_poll(service, 0) == CORE_OK);
    activate.prefix.request_id = 7;
    EXPECT(50, gui_encode_shell_activate(&activate,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(51, send_message(shell, GUI_SHELL_ACTIVATE, payload, size) ==
               CORE_OK);
    EXPECT(52, receive_message_type(service, shell,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(53, gui_decode_basic_result(&message, GUI_WINDOW_OPERATION_RESULT,
                                       &basic) == CORE_OK &&
               basic.error == CORE_NOT_FOUND);
    EXPECT(54, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(55, ipc_connection_close(&app2) == CORE_OK);
    EXPECT(56, ipc_connection_close(&shell) == CORE_OK);
    EXPECT(57, window_service_close(&service) == CORE_OK);
    EXPECT(58, string_destroy(&endpoint) == CORE_OK);
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

static void test_scene_bind_result_controls_create(void)
{
    String endpoint;
    String compositor_endpoint;
    IpcServerHandle compositor_server;
    IpcConnectionHandle compositor_peer;
    WindowServiceHandle service;
    WindowServiceConfig config;
    IpcConnectionHandle app;
    u8 shell_token[WINDOW_SERVICE_SHELL_TOKEN_SIZE];
    u8 controller_token[WINDOW_SERVICE_CONTROLLER_TOKEN_SIZE];
    u8 payload[320];
    usize size;
    IpcMessage message;
    GuiControllerBindRequest controller_bind;
    GuiWindowCreateRequest create = { 0 };
    GuiSceneBindRequest scene_bind;
    GuiWindowCreateResult created;

    fill_token(shell_token, WINDOW_SERVICE_SHELL_TOKEN_SIZE, 110);
    fill_token(controller_token, WINDOW_SERVICE_CONTROLLER_TOKEN_SIZE, 140);
    EXPECT(150, string_from_c_str("/tmp/punos-windowserv-scene-window",
                                  &endpoint) == CORE_OK);
    EXPECT(151, string_from_c_str("/tmp/punos-windowserv-scene-compositor",
                                  &compositor_endpoint) == CORE_OK);
    EXPECT(152, ipc_server_create(&compositor_endpoint,
                                  &compositor_server) == CORE_OK);

    config.endpoint = &endpoint;
    config.compositor_endpoint = &compositor_endpoint;
    config.io_endpoint = NULL;
    config.controller_token = controller_token;
    config.shell_token = shell_token;
    config.viewport_width = 100;
    config.viewport_height = 200;
    config.scale = GUI_SCALE_1X;
    config.orientation = GUI_ORIENTATION_NORMAL;
    config.safe_insets = (GuiInsets){ 0, 0, 0, 0 };
    EXPECT(153, window_service_create_config(&config, &service) == CORE_OK);
    EXPECT(154, accept_peer(compositor_server, service,
                            &compositor_peer) == CORE_OK);
    EXPECT(155, receive_message_type(service, compositor_peer,
                                     GUI_COMPOSITOR_BIND_CONTROLLER,
                                     &message) == CORE_OK);
    EXPECT(156, gui_decode_controller_bind(&message, &controller_bind) ==
                CORE_OK);
    EXPECT(157, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(158, send_basic_message(compositor_peer,
                                   GUI_COMPOSITOR_BIND_CONTROLLER_RESULT,
                                   CORE_OK,
                                   controller_bind.prefix.request_id) ==
                CORE_OK);
    EXPECT(159, window_service_poll(service, 0) == CORE_OK);

    EXPECT(160, connect_client(&endpoint, service, &app) == CORE_OK);
    create.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    create.prefix.request_id = 1;
    create.surface_id = 500;
    fill_token(create.surface_bind_token, GUI_SURFACE_BIND_TOKEN_SIZE, 180);
    create.role = GUI_WINDOW_ROLE_APP;
    create.flags = GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD;
    EXPECT(161, gui_encode_window_create(&create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(162, send_message(app, GUI_WINDOW_CREATE, payload, size) == CORE_OK);
    EXPECT(163, receive_message_type(service, compositor_peer,
                                     GUI_SCENE_BIND_WINDOW,
                                     &message) == CORE_OK);
    EXPECT(164, gui_decode_scene_bind(&message, &scene_bind) == CORE_OK &&
                scene_bind.surface_id == 500);
    EXPECT(165, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(166, send_basic_message(compositor_peer, GUI_SCENE_RESULT,
                                   CORE_PERMISSION_DENIED,
                                   scene_bind.prefix.request_id) == CORE_OK);
    EXPECT(167, receive_message_type(service, app, GUI_WINDOW_CREATE_RESULT,
                                     &message) == CORE_OK);
    EXPECT(168, gui_decode_window_create_result(&message, &created) ==
                CORE_OK &&
                created.result.error == CORE_PERMISSION_DENIED &&
                created.window_id == 0);
    EXPECT(169, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(170, ipc_connection_close(&app) == CORE_OK);
    EXPECT(171, ipc_connection_close(&compositor_peer) == CORE_OK);
    EXPECT(172, ipc_server_close(&compositor_server) == CORE_OK);
    EXPECT(173, window_service_close(&service) == CORE_OK);
    EXPECT(174, string_destroy(&compositor_endpoint) == CORE_OK);
    EXPECT(175, string_destroy(&endpoint) == CORE_OK);
}

static void test_scene_policy_create_and_pending_destroy(void)
{
    String endpoint;
    String compositor_endpoint;
    IpcServerHandle compositor_server;
    IpcConnectionHandle compositor_peer;
    WindowServiceHandle service;
    WindowServiceConfig config;
    IpcConnectionHandle app;
    u8 shell_token[WINDOW_SERVICE_SHELL_TOKEN_SIZE];
    u8 controller_token[WINDOW_SERVICE_CONTROLLER_TOKEN_SIZE];
    u8 payload[320];
    usize size;
    IpcMessage message;
    IpcReceiveStatus status;
    GuiControllerBindRequest controller_bind;
    GuiWindowCreateRequest create = { 0 };
    GuiSceneBindRequest scene_bind;
    GuiSceneGeometryRequest geometry;
    GuiSceneStackRequest stack;
    GuiSceneOutputRequest output;
    GuiSceneVisibilityRequest visibility;
    GuiWindowCreateResult created;
    GuiWindowLifecycleEvent lifecycle;
    GuiWindowVisibilityEvent window_visibility;
    GuiWindowConfigureEvent configure_event;
    GuiWindowIdRequest show;
    GuiWindowIdRequest destroy;
    GuiSceneWindowRequest scene_remove;
    GuiBasicResult basic;

    fill_token(shell_token, WINDOW_SERVICE_SHELL_TOKEN_SIZE, 10);
    fill_token(controller_token, WINDOW_SERVICE_CONTROLLER_TOKEN_SIZE, 40);
    EXPECT(180, string_from_c_str("/tmp/punos-windowserv-policy-window",
                                  &endpoint) == CORE_OK);
    EXPECT(181, string_from_c_str("/tmp/punos-windowserv-policy-compositor",
                                  &compositor_endpoint) == CORE_OK);
    EXPECT(182, ipc_server_create(&compositor_endpoint,
                                  &compositor_server) == CORE_OK);

    config.endpoint = &endpoint;
    config.compositor_endpoint = &compositor_endpoint;
    config.io_endpoint = NULL;
    config.controller_token = controller_token;
    config.shell_token = shell_token;
    config.viewport_width = 100;
    config.viewport_height = 200;
    config.scale = GUI_SCALE_1X;
    config.orientation = GUI_ORIENTATION_NORMAL;
    config.safe_insets = (GuiInsets){ 0, 0, 0, 0 };
    EXPECT(183, window_service_create_config(&config, &service) == CORE_OK);
    EXPECT(184, accept_peer(compositor_server, service,
                            &compositor_peer) == CORE_OK);
    EXPECT(185, receive_message_type(service, compositor_peer,
                                     GUI_COMPOSITOR_BIND_CONTROLLER,
                                     &message) == CORE_OK);
    EXPECT(186, gui_decode_controller_bind(&message, &controller_bind) ==
                CORE_OK);
    EXPECT(187, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(188, send_basic_message(compositor_peer,
                                   GUI_COMPOSITOR_BIND_CONTROLLER_RESULT,
                                   CORE_OK,
                                   controller_bind.prefix.request_id) ==
                CORE_OK);
    EXPECT(189, window_service_poll(service, 0) == CORE_OK);

    EXPECT(190, connect_client(&endpoint, service, &app) == CORE_OK);
    create.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    create.prefix.request_id = 1;
    create.surface_id = 700;
    fill_token(create.surface_bind_token, GUI_SURFACE_BIND_TOKEN_SIZE, 70);
    create.role = GUI_WINDOW_ROLE_APP;
    create.flags = GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD;
    EXPECT(191, gui_encode_window_create(&create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(192, send_message(app, GUI_WINDOW_CREATE, payload, size) == CORE_OK);
    EXPECT(193, receive_message_type(service, compositor_peer,
                                     GUI_SCENE_BIND_WINDOW,
                                     &message) == CORE_OK);
    EXPECT(194, gui_decode_scene_bind(&message, &scene_bind) == CORE_OK);
    EXPECT(195, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(196, send_basic_message(compositor_peer, GUI_SCENE_RESULT,
                                   CORE_OK,
                                   scene_bind.prefix.request_id) == CORE_OK);
    EXPECT(197, receive_message_type(service, compositor_peer,
                                     GUI_SCENE_SET_GEOMETRY,
                                     &message) == CORE_OK);
    EXPECT(198, gui_decode_scene_geometry(&message, &geometry) == CORE_OK);
    EXPECT(199, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(200, ipc_connection_receive(app, &status, &message) == CORE_OK &&
                status == IPC_RECEIVE_STATUS_NO_MESSAGE);
    EXPECT(201, send_basic_message(compositor_peer, GUI_SCENE_RESULT,
                                   CORE_OK,
                                   geometry.prefix.request_id) == CORE_OK);
    EXPECT(202, receive_message_type(service, compositor_peer,
                                     GUI_SCENE_SET_STACK,
                                     &message) == CORE_OK);
    EXPECT(203, gui_decode_scene_stack(&message, &stack) == CORE_OK);
    EXPECT(204, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(205, send_basic_message(compositor_peer, GUI_SCENE_RESULT,
                                   CORE_OK,
                                   stack.prefix.request_id) == CORE_OK);
    EXPECT(206, receive_message_type(service, compositor_peer,
                                     GUI_SCENE_SET_OUTPUT,
                                     &message) == CORE_OK);
    EXPECT(207, gui_decode_scene_output(&message, &output) == CORE_OK &&
                output.display_id == 0);
    EXPECT(208, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(209, send_basic_message(compositor_peer, GUI_SCENE_RESULT,
                                   CORE_OK,
                                   output.prefix.request_id) == CORE_OK);
    EXPECT(210, receive_message_type(service, compositor_peer,
                                     GUI_SCENE_SET_VISIBILITY,
                                     &message) == CORE_OK);
    EXPECT(211, gui_decode_scene_visibility(&message, &visibility) ==
                CORE_OK && visibility.visibility == GUI_SCENE_HIDDEN);
    EXPECT(212, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(213, send_basic_message(compositor_peer, GUI_SCENE_RESULT,
                                   CORE_OK,
                                   visibility.prefix.request_id) == CORE_OK);
    EXPECT(214, receive_message_type(service, app, GUI_WINDOW_CREATE_RESULT,
                                     &message) == CORE_OK);
    EXPECT(215, gui_decode_window_create_result(&message, &created) ==
                CORE_OK && created.result.error == CORE_OK &&
                created.window_id != 0);
    EXPECT(216, ipc_message_destroy(&message) == CORE_OK);

    show.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    show.prefix.request_id = 2;
    show.window_id = created.window_id;
    EXPECT(217, gui_encode_window_id_request(&show,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(218, send_message(app, GUI_WINDOW_SHOW, payload, size) == CORE_OK);
    EXPECT(219, receive_message_type(service, app,
                                     GUI_WINDOW_OPERATION_RESULT,
                                     &message) == CORE_OK);
    EXPECT(220, gui_decode_basic_result(&message, GUI_WINDOW_OPERATION_RESULT,
                                        &basic) == CORE_OK &&
                basic.error == CORE_OK);
    EXPECT(221, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(222, receive_message_type(service, compositor_peer,
                                     GUI_SCENE_SET_GEOMETRY,
                                     &message) == CORE_OK);
    EXPECT(223, gui_decode_scene_geometry(&message, &geometry) == CORE_OK);
    EXPECT(224, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(225, receive_message_type(service, app,
                                     GUI_WINDOW_LIFECYCLE_CHANGED,
                                     &message) == CORE_OK);
    EXPECT(226, gui_decode_window_lifecycle(&message, &lifecycle) == CORE_OK &&
                lifecycle.lifecycle == GUI_WINDOW_LIFECYCLE_BACKGROUND);
    EXPECT(227, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(228, receive_message_type(service, app,
                                     GUI_WINDOW_VISIBILITY_CHANGED,
                                     &message) == CORE_OK);
    EXPECT(229, gui_decode_window_visibility(&message, &window_visibility) ==
                CORE_OK && !window_visibility.visible);
    EXPECT(230, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(231, receive_message_type(service, app, GUI_WINDOW_CONFIGURE,
                                     &message) == CORE_OK);
    EXPECT(232, gui_decode_window_configure(&message, &configure_event) ==
                CORE_OK && configure_event.window_id == created.window_id);
    EXPECT(233, ipc_message_destroy(&message) == CORE_OK);
    destroy.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    destroy.prefix.request_id = 3;
    destroy.window_id = created.window_id;
    EXPECT(234, gui_encode_window_id_request(&destroy,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(235, send_message(app, GUI_WINDOW_DESTROY, payload, size) == CORE_OK);
    EXPECT(236, window_service_poll(service, 0) == CORE_OK);
    EXPECT(237, ipc_connection_receive(app, &status, &message) == CORE_OK &&
                status == IPC_RECEIVE_STATUS_NO_MESSAGE);
    EXPECT(238, send_basic_message(compositor_peer, GUI_SCENE_RESULT,
                                   CORE_OK,
                                   geometry.prefix.request_id) == CORE_OK);
    EXPECT(239, receive_message_type(service, compositor_peer,
                                     GUI_SCENE_REMOVE_WINDOW,
                                     &message) == CORE_OK);
    EXPECT(240, gui_decode_scene_window(&message, &scene_remove) == CORE_OK &&
                scene_remove.window_id == created.window_id);
    EXPECT(241, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(242, send_basic_message(compositor_peer, GUI_SCENE_RESULT,
                                   CORE_OK,
                                   scene_remove.prefix.request_id) == CORE_OK);
    EXPECT(243, receive_message_type(service, app, GUI_WINDOW_DESTROY_RESULT,
                                     &message) == CORE_OK);
    EXPECT(244, gui_decode_basic_result(&message, GUI_WINDOW_DESTROY_RESULT,
                                        &basic) == CORE_OK &&
                basic.error == CORE_OK);
    EXPECT(245, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(246, ipc_connection_close(&app) == CORE_OK);
    EXPECT(247, ipc_connection_close(&compositor_peer) == CORE_OK);
    EXPECT(248, ipc_server_close(&compositor_server) == CORE_OK);
    EXPECT(249, window_service_close(&service) == CORE_OK);
    EXPECT(250, string_destroy(&compositor_endpoint) == CORE_OK);
    EXPECT(251, string_destroy(&endpoint) == CORE_OK);
}

static void test_scene_update_error_suspends_and_removes(void)
{
    String endpoint;
    String compositor_endpoint;
    IpcServerHandle compositor_server;
    IpcConnectionHandle compositor_peer;
    WindowServiceHandle service;
    WindowServiceConfig config;
    IpcConnectionHandle app;
    u8 shell_token[WINDOW_SERVICE_SHELL_TOKEN_SIZE];
    u8 controller_token[WINDOW_SERVICE_CONTROLLER_TOKEN_SIZE];
    u8 payload[320];
    usize size;
    IpcMessage message;
    IpcReceiveStatus status;
    GuiControllerBindRequest controller_bind;
    GuiWindowCreateRequest create = { 0 };
    GuiSceneBindRequest scene_bind;
    GuiSceneGeometryRequest geometry;
    GuiSceneStackRequest stack;
    GuiSceneOutputRequest output;
    GuiSceneVisibilityRequest visibility;
    GuiSceneWindowRequest scene_remove;
    GuiWindowCreateResult created;
    GuiWindowIdRequest show;
    GuiWindowServiceEvent service_event;
    GuiBasicResult basic;

    fill_token(shell_token, WINDOW_SERVICE_SHELL_TOKEN_SIZE, 20);
    fill_token(controller_token, WINDOW_SERVICE_CONTROLLER_TOKEN_SIZE, 50);
    EXPECT(1, string_from_c_str("/tmp/punos-windowserv-scene-error-window",
                                &endpoint) == CORE_OK);
    EXPECT(2, string_from_c_str("/tmp/punos-windowserv-scene-error-compositor",
                                &compositor_endpoint) == CORE_OK);
    EXPECT(3, ipc_server_create(&compositor_endpoint,
                                &compositor_server) == CORE_OK);

    config.endpoint = &endpoint;
    config.compositor_endpoint = &compositor_endpoint;
    config.io_endpoint = NULL;
    config.controller_token = controller_token;
    config.shell_token = shell_token;
    config.viewport_width = 100;
    config.viewport_height = 200;
    config.scale = GUI_SCALE_1X;
    config.orientation = GUI_ORIENTATION_NORMAL;
    config.safe_insets = (GuiInsets){ 0, 0, 0, 0 };
    EXPECT(4, window_service_create_config(&config, &service) == CORE_OK);
    EXPECT(5, accept_peer(compositor_server, service,
                          &compositor_peer) == CORE_OK);
    EXPECT(6, receive_message_type(service, compositor_peer,
                                   GUI_COMPOSITOR_BIND_CONTROLLER,
                                   &message) == CORE_OK);
    EXPECT(7, gui_decode_controller_bind(&message, &controller_bind) ==
              CORE_OK);
    EXPECT(8, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(9, send_basic_message(compositor_peer,
                                 GUI_COMPOSITOR_BIND_CONTROLLER_RESULT,
                                 CORE_OK,
                                 controller_bind.prefix.request_id) ==
              CORE_OK);
    EXPECT(10, window_service_poll(service, 0) == CORE_OK);

    EXPECT(11, connect_client(&endpoint, service, &app) == CORE_OK);
    create.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    create.prefix.request_id = 1;
    create.surface_id = 800;
    fill_token(create.surface_bind_token, GUI_SURFACE_BIND_TOKEN_SIZE, 80);
    create.role = GUI_WINDOW_ROLE_APP;
    create.flags = GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD;
    EXPECT(12, gui_encode_window_create(&create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(13, send_message(app, GUI_WINDOW_CREATE, payload, size) == CORE_OK);
    EXPECT(14, receive_message_type(service, compositor_peer,
                                    GUI_SCENE_BIND_WINDOW,
                                    &message) == CORE_OK);
    EXPECT(15, gui_decode_scene_bind(&message, &scene_bind) == CORE_OK);
    EXPECT(16, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(17, send_basic_message(compositor_peer, GUI_SCENE_RESULT,
                                  CORE_OK,
                                  scene_bind.prefix.request_id) == CORE_OK);
    EXPECT(18, receive_message_type(service, compositor_peer,
                                    GUI_SCENE_SET_GEOMETRY,
                                    &message) == CORE_OK);
    EXPECT(19, gui_decode_scene_geometry(&message, &geometry) == CORE_OK);
    EXPECT(20, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(21, send_basic_message(compositor_peer, GUI_SCENE_RESULT,
                                  CORE_OK,
                                  geometry.prefix.request_id) == CORE_OK);
    EXPECT(22, receive_message_type(service, compositor_peer,
                                    GUI_SCENE_SET_STACK,
                                    &message) == CORE_OK);
    EXPECT(23, gui_decode_scene_stack(&message, &stack) == CORE_OK);
    EXPECT(24, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(25, send_basic_message(compositor_peer, GUI_SCENE_RESULT,
                                  CORE_OK,
                                  stack.prefix.request_id) == CORE_OK);
    EXPECT(26, receive_message_type(service, compositor_peer,
                                    GUI_SCENE_SET_OUTPUT,
                                    &message) == CORE_OK);
    EXPECT(27, gui_decode_scene_output(&message, &output) == CORE_OK);
    EXPECT(28, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(29, send_basic_message(compositor_peer, GUI_SCENE_RESULT,
                                  CORE_OK,
                                  output.prefix.request_id) == CORE_OK);
    EXPECT(30, receive_message_type(service, compositor_peer,
                                    GUI_SCENE_SET_VISIBILITY,
                                    &message) == CORE_OK);
    EXPECT(31, gui_decode_scene_visibility(&message, &visibility) ==
               CORE_OK);
    EXPECT(32, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(33, send_basic_message(compositor_peer, GUI_SCENE_RESULT,
                                  CORE_OK,
                                  visibility.prefix.request_id) == CORE_OK);
    EXPECT(34, receive_message_type(service, app, GUI_WINDOW_CREATE_RESULT,
                                    &message) == CORE_OK);
    EXPECT(35, gui_decode_window_create_result(&message, &created) ==
               CORE_OK && created.result.error == CORE_OK);
    EXPECT(36, ipc_message_destroy(&message) == CORE_OK);

    show.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    show.prefix.request_id = 2;
    show.window_id = created.window_id;
    EXPECT(37, gui_encode_window_id_request(&show,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(38, send_message(app, GUI_WINDOW_SHOW, payload, size) == CORE_OK);
    EXPECT(39, receive_message_type(service, app,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(40, gui_decode_basic_result(&message, GUI_WINDOW_OPERATION_RESULT,
                                       &basic) == CORE_OK &&
               basic.error == CORE_OK);
    EXPECT(41, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(42, receive_message_type(service, compositor_peer,
                                    GUI_SCENE_SET_GEOMETRY,
                                    &message) == CORE_OK);
    EXPECT(43, gui_decode_scene_geometry(&message, &geometry) == CORE_OK);
    EXPECT(44, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(45, send_basic_message(compositor_peer, GUI_SCENE_RESULT,
                                  CORE_INVALID_STATE,
                                  geometry.prefix.request_id) == CORE_OK);
    EXPECT(46, receive_message_type(service, app,
                                    GUI_WINDOW_RENDERING_SUSPENDED,
                                    &message) == CORE_OK);
    EXPECT(47, gui_decode_window_service_event(&message, &service_event) ==
               CORE_OK &&
               service_event.window_id == created.window_id);
    EXPECT(48, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(49, receive_message_type(service, compositor_peer,
                                    GUI_SCENE_REMOVE_WINDOW,
                                    &message) == CORE_OK);
    EXPECT(50, gui_decode_scene_window(&message, &scene_remove) == CORE_OK &&
               scene_remove.window_id == created.window_id);
    EXPECT(51, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(52, send_basic_message(compositor_peer, GUI_SCENE_RESULT,
                                  CORE_NOT_FOUND,
                                  scene_remove.prefix.request_id) == CORE_OK);
    EXPECT(53, window_service_poll(service, 0) == CORE_OK);
    EXPECT(54, ipc_connection_receive(compositor_peer, &status, &message) ==
               CORE_OK && status == IPC_RECEIVE_STATUS_NO_MESSAGE);

    EXPECT(55, ipc_connection_close(&app) == CORE_OK);
    EXPECT(56, ipc_connection_close(&compositor_peer) == CORE_OK);
    EXPECT(57, ipc_server_close(&compositor_server) == CORE_OK);
    EXPECT(58, window_service_close(&service) == CORE_OK);
    EXPECT(59, string_destroy(&compositor_endpoint) == CORE_OK);
    EXPECT(60, string_destroy(&endpoint) == CORE_OK);
}

static void test_modal_and_lock_input_routing(void)
{
    String endpoint;
    WindowServiceHandle service;
    IpcConnectionHandle app;
    IpcConnectionHandle shell;
    u8 shell_token[WINDOW_SERVICE_SHELL_TOKEN_SIZE];
    u8 payload[320];
    usize size;
    IpcMessage message;
    GuiShellBindRequest bind = { 0 };
    GuiWindowCreateRequest create = { 0 };
    GuiWindowCreateResult app_created;
    GuiWindowCreateResult dialog_created;
    GuiWindowCreateResult lock_created;
    GuiWindowIdRequest show;
    GuiShellActivateRequest activate;
    GuiWindowTouchEvent touch;
    GuiWindowTouchEvent decoded_touch;
    GuiWindowKeyEvent key;

    fill_token(shell_token, WINDOW_SERVICE_SHELL_TOKEN_SIZE, 210);
    EXPECT(200, string_from_c_str("/tmp/punos-windowserv-modal-lock",
                                  &endpoint) == CORE_OK);
    EXPECT(201, window_service_create(&endpoint, shell_token, &service) ==
                CORE_OK);
    EXPECT(202, connect_client(&endpoint, service, &app) == CORE_OK);
    EXPECT(203, connect_client(&endpoint, service, &shell) == CORE_OK);

    bind.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    bind.prefix.request_id = 1;
    fill_token(bind.shell_token, GUI_SHELL_TOKEN_SIZE, 210);
    EXPECT(204, gui_encode_shell_bind(&bind,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(205, send_message(shell, GUI_WINDOW_BIND_SHELL, payload, size) ==
                CORE_OK);
    EXPECT(206, receive_message_type(service, shell,
                                     GUI_WINDOW_BIND_SHELL_RESULT,
                                     &message) == CORE_OK);
    EXPECT(207, ipc_message_destroy(&message) == CORE_OK);

    create.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    create.prefix.request_id = 2;
    create.surface_id = 600;
    fill_token(create.surface_bind_token, GUI_SURFACE_BIND_TOKEN_SIZE, 220);
    create.role = GUI_WINDOW_ROLE_APP;
    create.flags = GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD;
    EXPECT(208, gui_encode_window_create(&create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(209, send_message(app, GUI_WINDOW_CREATE, payload, size) == CORE_OK);
    EXPECT(210, receive_message_type(service, app, GUI_WINDOW_CREATE_RESULT,
                                     &message) == CORE_OK);
    EXPECT(211, gui_decode_window_create_result(&message, &app_created) ==
                CORE_OK && app_created.result.error == CORE_OK &&
                app_created.window_id != 0);
    EXPECT(212, ipc_message_destroy(&message) == CORE_OK);

    show.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    show.prefix.request_id = 3;
    show.window_id = app_created.window_id;
    EXPECT(213, gui_encode_window_id_request(&show,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(214, send_message(app, GUI_WINDOW_SHOW, payload, size) == CORE_OK);
    EXPECT(215, receive_message_type(service, app,
                                     GUI_WINDOW_OPERATION_RESULT,
                                     &message) == CORE_OK);
    EXPECT(216, ipc_message_destroy(&message) == CORE_OK);

    activate.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    activate.prefix.request_id = 4;
    activate.window_id = app_created.window_id;
    EXPECT(217, gui_encode_shell_activate(&activate,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(218, send_message(shell, GUI_SHELL_ACTIVATE, payload, size) ==
                CORE_OK);
    EXPECT(219, receive_message_type(service, shell,
                                     GUI_WINDOW_OPERATION_RESULT,
                                     &message) == CORE_OK);
    EXPECT(220, ipc_message_destroy(&message) == CORE_OK);

    create.prefix.request_id = 5;
    create.surface_id = 601;
    fill_token(create.surface_bind_token, GUI_SURFACE_BIND_TOKEN_SIZE, 230);
    create.role = GUI_WINDOW_ROLE_DIALOG;
    create.parent_id = app_created.window_id;
    create.flags = 0;
    EXPECT(221, gui_encode_window_create(&create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(222, send_message(app, GUI_WINDOW_CREATE, payload, size) == CORE_OK);
    EXPECT(223, receive_message_type(service, app, GUI_WINDOW_CREATE_RESULT,
                                     &message) == CORE_OK);
    EXPECT(224, gui_decode_window_create_result(&message, &dialog_created) ==
                CORE_OK && dialog_created.result.error == CORE_OK &&
                dialog_created.window_id != 0);
    EXPECT(225, ipc_message_destroy(&message) == CORE_OK);

    show.prefix.request_id = 6;
    show.window_id = dialog_created.window_id;
    EXPECT(226, gui_encode_window_id_request(&show,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(227, send_message(app, GUI_WINDOW_SHOW, payload, size) == CORE_OK);
    EXPECT(228, receive_message_type(service, app,
                                     GUI_WINDOW_OPERATION_RESULT,
                                     &message) == CORE_OK);
    EXPECT(229, ipc_message_destroy(&message) == CORE_OK);

    touch.window_id = 0;
    touch.input_serial = 70;
    touch.timestamp_ms = 1000;
    touch.touch_id = 2;
    touch.phase = GUI_TOUCH_DOWN;
    touch.x = 1 << 8;
    touch.y = 2 << 8;
    EXPECT(230, window_service_route_touch(service, &touch) == CORE_OK);
    EXPECT(231, receive_message_type(service, app, GUI_WINDOW_TOUCH,
                                     &message) == CORE_OK);
    EXPECT(232, gui_decode_window_touch(&message, &decoded_touch) == CORE_OK &&
                decoded_touch.window_id == dialog_created.window_id);
    EXPECT(233, ipc_message_destroy(&message) == CORE_OK);
    touch.input_serial = 71;
    touch.phase = GUI_TOUCH_UP;
    EXPECT(234, window_service_route_touch(service, &touch) == CORE_OK);
    EXPECT(235, receive_message_type(service, app, GUI_WINDOW_TOUCH,
                                     &message) == CORE_OK);
    EXPECT(236, ipc_message_destroy(&message) == CORE_OK);

    key.window_id = 0;
    key.input_serial = 80;
    key.timestamp_ms = 1000;
    key.key_code = 30;
    key.phase = GUI_KEY_DOWN;
    key.modifiers = 0;
    EXPECT(254, window_service_route_key(service, &key) == CORE_NOT_FOUND);

    create.prefix.request_id = 7;
    create.surface_id = 602;
    fill_token(create.surface_bind_token, GUI_SURFACE_BIND_TOKEN_SIZE, 240);
    create.role = GUI_WINDOW_ROLE_LOCK;
    create.parent_id = 0;
    create.flags = 0;
    EXPECT(237, gui_encode_window_create(&create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(238, send_message(shell, GUI_WINDOW_CREATE, payload, size) == CORE_OK);
    EXPECT(239, receive_message_type(service, shell, GUI_WINDOW_CREATE_RESULT,
                                     &message) == CORE_OK);
    EXPECT(240, gui_decode_window_create_result(&message, &lock_created) ==
                CORE_OK && lock_created.result.error == CORE_OK &&
                lock_created.window_id != 0);
    EXPECT(241, ipc_message_destroy(&message) == CORE_OK);

    show.prefix.request_id = 8;
    show.window_id = lock_created.window_id;
    EXPECT(242, gui_encode_window_id_request(&show,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(243, send_message(shell, GUI_WINDOW_SHOW, payload, size) == CORE_OK);
    EXPECT(244, receive_message_type(service, shell,
                                     GUI_WINDOW_OPERATION_RESULT,
                                     &message) == CORE_OK);
    EXPECT(245, ipc_message_destroy(&message) == CORE_OK);

    touch.input_serial = 72;
    touch.touch_id = 3;
    touch.phase = GUI_TOUCH_DOWN;
    EXPECT(246, window_service_route_touch(service, &touch) == CORE_OK);
    EXPECT(247, receive_message_type(service, shell, GUI_WINDOW_TOUCH,
                                     &message) == CORE_OK);
    EXPECT(248, gui_decode_window_touch(&message, &decoded_touch) == CORE_OK &&
                decoded_touch.window_id == lock_created.window_id);
    EXPECT(249, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(250, ipc_connection_close(&app) == CORE_OK);
    EXPECT(251, ipc_connection_close(&shell) == CORE_OK);
    EXPECT(252, window_service_close(&service) == CORE_OK);
    EXPECT(253, string_destroy(&endpoint) == CORE_OK);
}

static void test_dialog_visibility_follows_parent_activation(void)
{
    String endpoint;
    WindowServiceHandle service;
    IpcConnectionHandle app;
    IpcConnectionHandle shell;
    u8 shell_token[WINDOW_SERVICE_SHELL_TOKEN_SIZE];
    u8 payload[320];
    usize size;
    IpcMessage message;
    GuiShellBindRequest bind = { 0 };
    GuiWindowCreateRequest create = { 0 };
    GuiWindowCreateResult app_created;
    GuiWindowCreateResult dialog_created;
    GuiWindowCreateResult app2_created;
    GuiWindowIdRequest show;
    GuiShellActivateRequest activate;

    fill_token(shell_token, WINDOW_SERVICE_SHELL_TOKEN_SIZE, 170);
    EXPECT(1, string_from_c_str("/tmp/punos-windowserv-dialog-visibility",
                                &endpoint) == CORE_OK);
    EXPECT(2, window_service_create(&endpoint, shell_token, &service) ==
              CORE_OK);
    EXPECT(3, connect_client(&endpoint, service, &app) == CORE_OK);
    EXPECT(4, connect_client(&endpoint, service, &shell) == CORE_OK);

    bind.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    bind.prefix.request_id = 1;
    fill_token(bind.shell_token, GUI_SHELL_TOKEN_SIZE, 170);
    EXPECT(5, gui_encode_shell_bind(&bind, (Bytes){ payload, sizeof(payload) },
                                    &size) == CORE_OK);
    EXPECT(6, send_message(shell, GUI_WINDOW_BIND_SHELL, payload, size) ==
              CORE_OK);
    EXPECT(7, receive_message_type(service, shell,
                                   GUI_WINDOW_BIND_SHELL_RESULT,
                                   &message) == CORE_OK);
    EXPECT(8, ipc_message_destroy(&message) == CORE_OK);

    create.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    create.prefix.request_id = 2;
    create.surface_id = 1000;
    fill_token(create.surface_bind_token, GUI_SURFACE_BIND_TOKEN_SIZE, 20);
    create.role = GUI_WINDOW_ROLE_APP;
    create.parent_id = 0;
    create.flags = GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD;
    EXPECT(9, gui_encode_window_create(&create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(10, send_message(app, GUI_WINDOW_CREATE, payload, size) == CORE_OK);
    EXPECT(11, receive_message_type(service, app, GUI_WINDOW_CREATE_RESULT,
                                    &message) == CORE_OK);
    EXPECT(12, gui_decode_window_create_result(&message, &app_created) ==
               CORE_OK && app_created.result.error == CORE_OK);
    EXPECT(13, ipc_message_destroy(&message) == CORE_OK);

    show.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    show.prefix.request_id = 3;
    show.window_id = app_created.window_id;
    EXPECT(14, gui_encode_window_id_request(&show,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(15, send_message(app, GUI_WINDOW_SHOW, payload, size) == CORE_OK);
    EXPECT(16, receive_message_type(service, app,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(17, ipc_message_destroy(&message) == CORE_OK);

    activate.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    activate.prefix.request_id = 4;
    activate.window_id = app_created.window_id;
    EXPECT(18, gui_encode_shell_activate(&activate,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(19, send_message(shell, GUI_SHELL_ACTIVATE, payload, size) ==
               CORE_OK);
    EXPECT(20, receive_message_type(service, shell,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(21, ipc_message_destroy(&message) == CORE_OK);

    create.prefix.request_id = 5;
    create.surface_id = 1001;
    fill_token(create.surface_bind_token, GUI_SURFACE_BIND_TOKEN_SIZE, 30);
    create.role = GUI_WINDOW_ROLE_DIALOG;
    create.parent_id = app_created.window_id;
    create.flags = 0;
    EXPECT(22, gui_encode_window_create(&create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(23, send_message(app, GUI_WINDOW_CREATE, payload, size) == CORE_OK);
    EXPECT(24, receive_message_type(service, app, GUI_WINDOW_CREATE_RESULT,
                                    &message) == CORE_OK);
    EXPECT(25, gui_decode_window_create_result(&message, &dialog_created) ==
               CORE_OK && dialog_created.result.error == CORE_OK);
    EXPECT(26, ipc_message_destroy(&message) == CORE_OK);

    show.prefix.request_id = 6;
    show.window_id = dialog_created.window_id;
    EXPECT(27, gui_encode_window_id_request(&show,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(28, send_message(app, GUI_WINDOW_SHOW, payload, size) == CORE_OK);
    EXPECT(29, receive_message_type(service, app,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(30, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(31, receive_visibility_for(service, app,
                                      dialog_created.window_id, true) ==
               CORE_OK);

    create.prefix.request_id = 7;
    create.surface_id = 1002;
    fill_token(create.surface_bind_token, GUI_SURFACE_BIND_TOKEN_SIZE, 40);
    create.role = GUI_WINDOW_ROLE_APP;
    create.parent_id = 0;
    create.flags = GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD;
    EXPECT(32, gui_encode_window_create(&create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(33, send_message(app, GUI_WINDOW_CREATE, payload, size) == CORE_OK);
    EXPECT(34, receive_message_type(service, app, GUI_WINDOW_CREATE_RESULT,
                                    &message) == CORE_OK);
    EXPECT(35, gui_decode_window_create_result(&message, &app2_created) ==
               CORE_OK && app2_created.result.error == CORE_OK);
    EXPECT(36, ipc_message_destroy(&message) == CORE_OK);

    show.prefix.request_id = 8;
    show.window_id = app2_created.window_id;
    EXPECT(37, gui_encode_window_id_request(&show,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(38, send_message(app, GUI_WINDOW_SHOW, payload, size) == CORE_OK);
    EXPECT(39, receive_message_type(service, app,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(40, ipc_message_destroy(&message) == CORE_OK);

    activate.prefix.request_id = 9;
    activate.window_id = app2_created.window_id;
    EXPECT(41, gui_encode_shell_activate(&activate,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(42, send_message(shell, GUI_SHELL_ACTIVATE, payload, size) ==
               CORE_OK);
    EXPECT(43, receive_message_type(service, shell,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(44, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(45, receive_visibility_for(service, app,
                                      dialog_created.window_id, false) ==
               CORE_OK);

    EXPECT(46, ipc_connection_close(&app) == CORE_OK);
    EXPECT(47, ipc_connection_close(&shell) == CORE_OK);
    EXPECT(48, window_service_close(&service) == CORE_OK);
    EXPECT(49, string_destroy(&endpoint) == CORE_OK);
}

static void test_overview_navigation_targets_home(void)
{
    String endpoint;
    WindowServiceHandle service;
    IpcConnectionHandle app;
    IpcConnectionHandle shell;
    u8 shell_token[WINDOW_SERVICE_SHELL_TOKEN_SIZE];
    u8 payload[320];
    usize size;
    IpcMessage message;
    GuiShellBindRequest bind = { 0 };
    GuiWindowCreateRequest create = { 0 };
    GuiWindowCreateResult home_created;
    GuiWindowCreateResult app_created;
    GuiWindowIdRequest show;
    GuiShellActivateRequest activate;
    GuiShellNavigateRequest navigate = { 0 };
    GuiWindowNavigationEvent navigation;
    GuiBasicResult basic;

    fill_token(shell_token, WINDOW_SERVICE_SHELL_TOKEN_SIZE, 180);
    EXPECT(1, string_from_c_str("/tmp/punos-windowserv-overview",
                                &endpoint) == CORE_OK);
    EXPECT(2, window_service_create(&endpoint, shell_token, &service) ==
              CORE_OK);
    EXPECT(3, connect_client(&endpoint, service, &app) == CORE_OK);
    EXPECT(4, connect_client(&endpoint, service, &shell) == CORE_OK);

    bind.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    bind.prefix.request_id = 1;
    fill_token(bind.shell_token, GUI_SHELL_TOKEN_SIZE, 180);
    EXPECT(5, gui_encode_shell_bind(&bind, (Bytes){ payload, sizeof(payload) },
                                    &size) == CORE_OK);
    EXPECT(6, send_message(shell, GUI_WINDOW_BIND_SHELL, payload, size) ==
              CORE_OK);
    EXPECT(7, receive_message_type(service, shell,
                                   GUI_WINDOW_BIND_SHELL_RESULT,
                                   &message) == CORE_OK);
    EXPECT(8, ipc_message_destroy(&message) == CORE_OK);

    create.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    create.prefix.request_id = 2;
    create.surface_id = 1100;
    fill_token(create.surface_bind_token, GUI_SURFACE_BIND_TOKEN_SIZE, 50);
    create.role = GUI_WINDOW_ROLE_HOME;
    create.parent_id = 0;
    create.flags = GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD;
    EXPECT(9, gui_encode_window_create(&create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(10, send_message(shell, GUI_WINDOW_CREATE, payload, size) ==
               CORE_OK);
    EXPECT(11, receive_message_type(service, shell, GUI_WINDOW_CREATE_RESULT,
                                    &message) == CORE_OK);
    EXPECT(12, gui_decode_window_create_result(&message, &home_created) ==
               CORE_OK && home_created.result.error == CORE_OK);
    EXPECT(13, ipc_message_destroy(&message) == CORE_OK);

    show.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    show.prefix.request_id = 3;
    show.window_id = home_created.window_id;
    EXPECT(14, gui_encode_window_id_request(&show,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(15, send_message(shell, GUI_WINDOW_SHOW, payload, size) == CORE_OK);
    EXPECT(16, receive_message_type(service, shell,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(17, ipc_message_destroy(&message) == CORE_OK);

    create.prefix.request_id = 4;
    create.surface_id = 1101;
    fill_token(create.surface_bind_token, GUI_SURFACE_BIND_TOKEN_SIZE, 60);
    create.role = GUI_WINDOW_ROLE_APP;
    create.flags = GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD;
    EXPECT(18, gui_encode_window_create(&create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(19, send_message(app, GUI_WINDOW_CREATE, payload, size) == CORE_OK);
    EXPECT(20, receive_message_type(service, app, GUI_WINDOW_CREATE_RESULT,
                                    &message) == CORE_OK);
    EXPECT(21, gui_decode_window_create_result(&message, &app_created) ==
               CORE_OK && app_created.result.error == CORE_OK);
    EXPECT(22, ipc_message_destroy(&message) == CORE_OK);

    show.prefix.request_id = 5;
    show.window_id = app_created.window_id;
    EXPECT(23, gui_encode_window_id_request(&show,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(24, send_message(app, GUI_WINDOW_SHOW, payload, size) == CORE_OK);
    EXPECT(25, receive_message_type(service, app,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(26, ipc_message_destroy(&message) == CORE_OK);

    activate.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    activate.prefix.request_id = 6;
    activate.window_id = app_created.window_id;
    EXPECT(27, gui_encode_shell_activate(&activate,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(28, send_message(shell, GUI_SHELL_ACTIVATE, payload, size) ==
               CORE_OK);
    EXPECT(29, receive_message_type(service, shell,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(30, ipc_message_destroy(&message) == CORE_OK);

    navigate.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    navigate.prefix.request_id = 7;
    navigate.action = GUI_NAVIGATION_OVERVIEW;
    navigate.target_id = 0;
    EXPECT(31, gui_encode_shell_navigate(&navigate,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(32, send_message(shell, GUI_SHELL_NAVIGATE, payload, size) ==
               CORE_OK);
    EXPECT(33, receive_message_type(service, shell,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(34, gui_decode_basic_result(&message, GUI_WINDOW_OPERATION_RESULT,
                                       &basic) == CORE_OK &&
               basic.error == CORE_OK && basic.request_id == 7);
    EXPECT(35, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(36, receive_message_type(service, shell,
                                    GUI_WINDOW_NAVIGATION_REQUESTED,
                                    &message) == CORE_OK);
    EXPECT(37, gui_decode_window_navigation(&message, &navigation) ==
               CORE_OK && navigation.window_id == home_created.window_id &&
               navigation.action == GUI_NAVIGATION_OVERVIEW);
    EXPECT(38, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(39, ipc_connection_close(&app) == CORE_OK);
    EXPECT(40, ipc_connection_close(&shell) == CORE_OK);
    EXPECT(41, window_service_close(&service) == CORE_OK);
    EXPECT(42, string_destroy(&endpoint) == CORE_OK);
}

static void test_navigation_close_lifecycle(void)
{
    String endpoint;
    WindowServiceHandle service;
    IpcConnectionHandle app;
    IpcConnectionHandle shell;
    u8 shell_token[WINDOW_SERVICE_SHELL_TOKEN_SIZE];
    u8 payload[320];
    usize size;
    IpcMessage message;
    GuiShellBindRequest bind = { 0 };
    GuiWindowCreateRequest create = { 0 };
    GuiWindowCreateResult created;
    GuiWindowIdRequest show;
    GuiShellActivateRequest activate;
    GuiShellNavigateRequest navigate = { 0 };
    GuiWindowCloseEvent close_event;
    GuiWindowCloseResponseRequest close_response = { 0 };
    GuiBasicResult basic;

    fill_token(shell_token, WINDOW_SERVICE_SHELL_TOKEN_SIZE, 30);
    EXPECT(1, string_from_c_str("/tmp/punos-windowserv-navigation-close",
                                &endpoint) == CORE_OK);
    EXPECT(2, window_service_create(&endpoint, shell_token, &service) ==
              CORE_OK);
    EXPECT(3, connect_client(&endpoint, service, &app) == CORE_OK);
    EXPECT(4, connect_client(&endpoint, service, &shell) == CORE_OK);

    bind.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    bind.prefix.request_id = 1;
    fill_token(bind.shell_token, GUI_SHELL_TOKEN_SIZE, 30);
    EXPECT(5, gui_encode_shell_bind(&bind, (Bytes){ payload, sizeof(payload) },
                                    &size) == CORE_OK);
    EXPECT(6, send_message(shell, GUI_WINDOW_BIND_SHELL, payload, size) ==
              CORE_OK);
    EXPECT(7, receive_message_type(service, shell,
                                   GUI_WINDOW_BIND_SHELL_RESULT,
                                   &message) == CORE_OK);
    EXPECT(8, ipc_message_destroy(&message) == CORE_OK);

    create.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    create.prefix.request_id = 2;
    create.surface_id = 900;
    fill_token(create.surface_bind_token, GUI_SURFACE_BIND_TOKEN_SIZE, 90);
    create.role = GUI_WINDOW_ROLE_APP;
    create.flags = GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD;
    EXPECT(9, gui_encode_window_create(&create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(10, send_message(app, GUI_WINDOW_CREATE, payload, size) == CORE_OK);
    EXPECT(11, receive_message_type(service, app, GUI_WINDOW_CREATE_RESULT,
                                    &message) == CORE_OK);
    EXPECT(12, gui_decode_window_create_result(&message, &created) == CORE_OK &&
               created.result.error == CORE_OK);
    EXPECT(13, ipc_message_destroy(&message) == CORE_OK);

    show.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    show.prefix.request_id = 3;
    show.window_id = created.window_id;
    EXPECT(14, gui_encode_window_id_request(&show,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(15, send_message(app, GUI_WINDOW_SHOW, payload, size) == CORE_OK);
    EXPECT(16, receive_message_type(service, app,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(17, ipc_message_destroy(&message) == CORE_OK);

    activate.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    activate.prefix.request_id = 4;
    activate.window_id = created.window_id;
    EXPECT(18, gui_encode_shell_activate(&activate,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(19, send_message(shell, GUI_SHELL_ACTIVATE, payload, size) ==
               CORE_OK);
    EXPECT(20, receive_message_type(service, shell,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(21, ipc_message_destroy(&message) == CORE_OK);

    navigate.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    navigate.prefix.request_id = 5;
    navigate.action = GUI_NAVIGATION_BACK;
    navigate.target_id = 0;
    EXPECT(22, gui_encode_shell_navigate(&navigate,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(23, send_message(shell, GUI_SHELL_NAVIGATE, payload, size) ==
               CORE_OK);
    EXPECT(24, receive_message_type(service, shell,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(25, gui_decode_basic_result(&message, GUI_WINDOW_OPERATION_RESULT,
                                       &basic) == CORE_OK &&
               basic.error == CORE_OK);
    EXPECT(26, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(27, receive_message_type(service, app, GUI_WINDOW_CLOSE_REQUESTED,
                                    &message) == CORE_OK);
    EXPECT(28, gui_decode_window_close_event(&message, &close_event) ==
               CORE_OK && close_event.window_id == created.window_id);
    EXPECT(29, ipc_message_destroy(&message) == CORE_OK);

    close_response.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    close_response.prefix.request_id = 6;
    close_response.window_id = created.window_id;
    close_response.close_serial = close_event.close_serial + 1;
    close_response.response = GUI_CLOSE_ALLOW;
    EXPECT(30, gui_encode_window_close_response(&close_response,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(31, send_message(app, GUI_WINDOW_RESPOND_CLOSE, payload, size) ==
               CORE_OK);
    EXPECT(32, receive_message_type(service, app,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(33, gui_decode_basic_result(&message, GUI_WINDOW_OPERATION_RESULT,
                                       &basic) == CORE_OK &&
               basic.error == CORE_INVALID_STATE);
    EXPECT(34, ipc_message_destroy(&message) == CORE_OK);

    close_response.prefix.request_id = 7;
    close_response.close_serial = close_event.close_serial;
    close_response.response = GUI_CLOSE_DEFER;
    EXPECT(35, gui_encode_window_close_response(&close_response,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(36, send_message(app, GUI_WINDOW_RESPOND_CLOSE, payload, size) ==
               CORE_OK);
    EXPECT(37, receive_message_type(service, app,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(38, gui_decode_basic_result(&message, GUI_WINDOW_OPERATION_RESULT,
                                       &basic) == CORE_OK &&
               basic.error == CORE_OK);
    EXPECT(39, ipc_message_destroy(&message) == CORE_OK);

    navigate.prefix.request_id = 8;
    EXPECT(40, gui_encode_shell_navigate(&navigate,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(41, send_message(shell, GUI_SHELL_NAVIGATE, payload, size) ==
               CORE_OK);
    EXPECT(42, receive_message_type(service, shell,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(43, gui_decode_basic_result(&message, GUI_WINDOW_OPERATION_RESULT,
                                       &basic) == CORE_OK &&
               basic.error == CORE_OK && basic.request_id == 8);
    EXPECT(44, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(45, receive_message_type(service, app, GUI_WINDOW_CLOSE_REQUESTED,
                                    &message) == CORE_OK);
    EXPECT(46, gui_decode_window_close_event(&message, &close_event) ==
               CORE_OK && close_event.window_id == created.window_id);
    EXPECT(47, ipc_message_destroy(&message) == CORE_OK);

    close_response.prefix.request_id = 9;
    close_response.close_serial = close_event.close_serial;
    close_response.response = GUI_CLOSE_ALLOW;
    EXPECT(48, gui_encode_window_close_response(&close_response,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(49, send_message(app, GUI_WINDOW_RESPOND_CLOSE, payload, size) ==
               CORE_OK);
    EXPECT(50, receive_message_type(service, app,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(51, gui_decode_basic_result(&message, GUI_WINDOW_OPERATION_RESULT,
                                       &basic) == CORE_OK &&
               basic.error == CORE_OK);
    EXPECT(52, ipc_message_destroy(&message) == CORE_OK);

    activate.prefix.request_id = 10;
    EXPECT(53, gui_encode_shell_activate(&activate,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(54, send_message(shell, GUI_SHELL_ACTIVATE, payload, size) ==
               CORE_OK);
    EXPECT(55, receive_message_type(service, shell,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(56, gui_decode_basic_result(&message, GUI_WINDOW_OPERATION_RESULT,
                                       &basic) == CORE_OK &&
               basic.error == CORE_NOT_FOUND && basic.request_id == 10);
    EXPECT(57, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(58, ipc_connection_close(&app) == CORE_OK);
    EXPECT(59, ipc_connection_close(&shell) == CORE_OK);
    EXPECT(60, window_service_close(&service) == CORE_OK);
    EXPECT(61, string_destroy(&endpoint) == CORE_OK);
}

static void test_io_reconnect_cancel_and_orientation(void)
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
    GuiIoDeviceEvent device = { 0 };
    GuiWindowTouchEvent touch;

    fill_token(shell_token, WINDOW_SERVICE_SHELL_TOKEN_SIZE, 40);
    EXPECT(1, string_from_c_str("/tmp/punos-windowserv-io-reconnect-window",
                                &endpoint) == CORE_OK);
    EXPECT(2, string_from_c_str("/tmp/punos-windowserv-io-reconnect-fake",
                                &io_endpoint) == CORE_OK);
    EXPECT(3, ipc_server_create(&io_endpoint, &io_server) == CORE_OK);

    config.endpoint = &endpoint;
    config.compositor_endpoint = NULL;
    config.io_endpoint = &io_endpoint;
    config.controller_token = NULL;
    config.shell_token = shell_token;
    config.viewport_width = 100;
    config.viewport_height = 200;
    config.scale = GUI_SCALE_1X;
    config.orientation = GUI_ORIENTATION_RIGHT;
    config.safe_insets = (GuiInsets){ 0, 0, 0, 0 };
    EXPECT(4, window_service_create_config(&config, &service) == CORE_OK);
    EXPECT(5, accept_peer(io_server, service, &io_peer) == CORE_OK);
    EXPECT(6, receive_message_type(service, io_peer, GUI_IO_SUBSCRIBE,
                                   &message) == CORE_OK);
    EXPECT(7, gui_decode_io_subscribe(&message, &subscribe) == CORE_OK &&
              (subscribe.device_class_mask & GUI_INPUT_DEVICE_TOUCH) != 0);
    EXPECT(8, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(9, gui_encode_io_result(&io_result,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(10, send_message(io_peer, GUI_IO_SUBSCRIBE_RESULT, payload, size) ==
                CORE_OK);
    EXPECT(11, window_service_poll(service, 0) == CORE_OK);

    EXPECT(12, connect_client(&endpoint, service, &app) == CORE_OK);
    EXPECT(13, connect_client(&endpoint, service, &shell) == CORE_OK);
    bind.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    bind.prefix.request_id = 1;
    fill_token(bind.shell_token, GUI_SHELL_TOKEN_SIZE, 40);
    EXPECT(14, gui_encode_shell_bind(&bind,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(15, send_message(shell, GUI_WINDOW_BIND_SHELL, payload, size) ==
               CORE_OK);
    EXPECT(16, receive_message_type(service, shell,
                                    GUI_WINDOW_BIND_SHELL_RESULT,
                                    &message) == CORE_OK);
    EXPECT(17, ipc_message_destroy(&message) == CORE_OK);

    create.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    create.prefix.request_id = 2;
    create.surface_id = 910;
    fill_token(create.surface_bind_token, GUI_SURFACE_BIND_TOKEN_SIZE, 100);
    create.role = GUI_WINDOW_ROLE_APP;
    create.flags = GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD;
    EXPECT(18, gui_encode_window_create(&create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(19, send_message(app, GUI_WINDOW_CREATE, payload, size) == CORE_OK);
    EXPECT(20, receive_message_type(service, app, GUI_WINDOW_CREATE_RESULT,
                                    &message) == CORE_OK);
    EXPECT(21, gui_decode_window_create_result(&message, &created) == CORE_OK &&
               created.result.error == CORE_OK);
    EXPECT(22, ipc_message_destroy(&message) == CORE_OK);

    show.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    show.prefix.request_id = 3;
    show.window_id = created.window_id;
    EXPECT(23, gui_encode_window_id_request(&show,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(24, send_message(app, GUI_WINDOW_SHOW, payload, size) == CORE_OK);
    EXPECT(25, receive_message_type(service, app,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(26, ipc_message_destroy(&message) == CORE_OK);

    activate.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    activate.prefix.request_id = 4;
    activate.window_id = created.window_id;
    EXPECT(27, gui_encode_shell_activate(&activate,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(28, send_message(shell, GUI_SHELL_ACTIVATE, payload, size) ==
               CORE_OK);
    EXPECT(29, receive_message_type(service, shell,
                                    GUI_WINDOW_OPERATION_RESULT,
                                    &message) == CORE_OK);
    EXPECT(30, gui_decode_basic_result(&message, GUI_WINDOW_OPERATION_RESULT,
                                       &basic) == CORE_OK &&
               basic.error == CORE_OK);
    EXPECT(31, ipc_message_destroy(&message) == CORE_OK);

    frame.device_id = 9;
    frame.sequence = 1;
    frame.event_count = 3;
    frame.events[0] = (GuiIoInputEvent){ GUI_IO_INPUT_ABSOLUTE_AXIS,
                                         TEST_LINUX_ABS_X, 150 };
    frame.events[1] = (GuiIoInputEvent){ GUI_IO_INPUT_ABSOLUTE_AXIS,
                                         TEST_LINUX_ABS_Y, 34 };
    frame.events[2] = (GuiIoInputEvent){ GUI_IO_INPUT_KEY,
                                         TEST_LINUX_BTN_TOUCH, 1 };
    EXPECT(32, gui_encode_io_input_frame(&frame,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(33, send_message(io_peer, GUI_IO_INPUT_FRAME, payload, size) ==
               CORE_OK);
    EXPECT(34, receive_message_type(service, app, GUI_WINDOW_TOUCH,
                                    &message) == CORE_OK);
    EXPECT(35, gui_decode_window_touch(&message, &touch) == CORE_OK &&
               touch.window_id == created.window_id &&
               touch.phase == GUI_TOUCH_DOWN &&
               touch.timestamp_ms != 0 &&
               touch.x == (65 << 8) && touch.y == (150 << 8));
    EXPECT(36, ipc_message_destroy(&message) == CORE_OK);

    device.device_id = 9;
    device.classes = GUI_INPUT_DEVICE_TOUCH;
    EXPECT(37, gui_encode_io_device_event(&device,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(38, send_message(io_peer, GUI_IO_DEVICE_REMOVED, payload, size) ==
               CORE_OK);
    EXPECT(39, receive_message_type(service, app, GUI_WINDOW_TOUCH,
                                    &message) == CORE_OK);
    EXPECT(40, gui_decode_window_touch(&message, &touch) == CORE_OK &&
               touch.window_id == created.window_id &&
               touch.phase == GUI_TOUCH_CANCEL);
    EXPECT(41, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(42, ipc_connection_close(&io_peer) == CORE_OK);
    EXPECT(43, window_service_poll(service, 0) == CORE_OK);
    EXPECT(44, accept_peer(io_server, service, &io_peer) == CORE_OK);
    EXPECT(45, receive_message_type(service, io_peer, GUI_IO_SUBSCRIBE,
                                    &message) == CORE_OK);
    EXPECT(46, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(47, gui_encode_io_result(&io_result,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(48, send_message(io_peer, GUI_IO_SUBSCRIBE_RESULT, payload, size) ==
               CORE_OK);
    EXPECT(49, window_service_poll(service, 0) == CORE_OK);

    frame.sequence = 2;
    frame.events[0] = (GuiIoInputEvent){ GUI_IO_INPUT_KEY,
                                         TEST_LINUX_BTN_TOUCH, 1 };
    frame.events[1] = (GuiIoInputEvent){ GUI_IO_INPUT_ABSOLUTE_AXIS,
                                         TEST_LINUX_ABS_X, 20 };
    frame.events[2] = (GuiIoInputEvent){ GUI_IO_INPUT_ABSOLUTE_AXIS,
                                         TEST_LINUX_ABS_Y, 30 };
    EXPECT(50, gui_encode_io_input_frame(&frame,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(51, send_message(io_peer, GUI_IO_INPUT_FRAME, payload, size) ==
               CORE_OK);
    EXPECT(52, receive_message_type(service, app, GUI_WINDOW_TOUCH,
                                    &message) == CORE_OK);
    EXPECT(53, gui_decode_window_touch(&message, &touch) == CORE_OK &&
               touch.window_id == created.window_id &&
               touch.phase == GUI_TOUCH_DOWN &&
               touch.x == (69 << 8) && touch.y == (20 << 8));
    EXPECT(54, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(55, ipc_connection_close(&app) == CORE_OK);
    EXPECT(56, ipc_connection_close(&shell) == CORE_OK);
    EXPECT(57, ipc_connection_close(&io_peer) == CORE_OK);
    EXPECT(58, ipc_server_close(&io_server) == CORE_OK);
    EXPECT(59, window_service_close(&service) == CORE_OK);
    EXPECT(60, string_destroy(&io_endpoint) == CORE_OK);
    EXPECT(61, string_destroy(&endpoint) == CORE_OK);
}

int main(void)
{
    test_window_lifecycle_input_and_cleanup();
    test_io_input_frame_routing();
    test_scene_bind_result_controls_create();
    test_scene_policy_create_and_pending_destroy();
    test_scene_update_error_suspends_and_removes();
    test_modal_and_lock_input_routing();
    test_dialog_visibility_follows_parent_activation();
    test_overview_navigation_targets_home();
    test_navigation_close_lifecycle();
    test_io_reconnect_cancel_and_orientation();
    check_passed();
    return 0;
}
