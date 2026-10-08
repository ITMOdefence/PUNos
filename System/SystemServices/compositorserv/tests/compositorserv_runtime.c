#define CHECK_TEST_NAME "compositorserv"
#include "../../../SystemLibs/testing/check.h"

#include <compositorserv.h>
#include <gui/protocol.h>
#include <special/ipc.h>
#include <sys/memory.h>
#include "../src/renderer.h"

static ErrorCode connect_client(const String *endpoint,
                                CompositorServiceHandle service,
                                IpcConnectionHandle *out_client)
{
    IpcConnectionState state = IPC_CONNECTION_CONNECTING;
    usize attempt;
    ErrorCode result = ipc_connection_begin_connect(endpoint, out_client);

    if (result != CORE_OK) return result;
    for (attempt = 0; attempt < 1000 && state == IPC_CONNECTION_CONNECTING;
         ++attempt) {
        result = compositor_service_poll(service, 0);
        if (result != CORE_OK) return result;
        result = ipc_connection_finish_connect(*out_client, &state);
        if (result != CORE_OK) return result;
    }
    return state == IPC_CONNECTION_OPEN ? CORE_OK : CORE_WOULD_BLOCK;
}

static ErrorCode send_message_with_descriptor(IpcConnectionHandle client,
                                              IpcMessageType type,
                                              u8 *payload, usize size,
                                              i32 descriptor)
{
    IpcMessage message = { type, 0, { payload, size }, descriptor, 0 };
    bool pending;
    ErrorCode result = ipc_connection_send(client, &message);

    if (result != CORE_OK) return result;
    return ipc_connection_flush(client, &pending);
}

static ErrorCode send_message(IpcConnectionHandle client, IpcMessageType type,
                              u8 *payload, usize size)
{
    return send_message_with_descriptor(client, type, payload, size,
                                        IPC_NO_DESCRIPTOR);
}

static ErrorCode receive_message(CompositorServiceHandle service,
                                 IpcConnectionHandle client,
                                 IpcMessage *out_message)
{
    usize attempt;

    for (attempt = 0; attempt < 1000; ++attempt) {
        IpcReceiveStatus status;
        ErrorCode result = compositor_service_poll(service, 0);
        if (result != CORE_OK) return result;
        result = ipc_connection_receive(client, &status, out_message);
        if (result != CORE_OK) return result;
        if (status == IPC_RECEIVE_MESSAGE) return CORE_OK;
        if (status == IPC_RECEIVE_PEER_CLOSED) return CORE_IO_ERROR;
    }
    return CORE_WOULD_BLOCK;
}

static void test_accept_connection(void)
{
    String endpoint;
    CompositorServiceHandle service;
    IpcConnectionHandle client;
    u8 token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE] = { 1 };

    EXPECT(1, string_from_c_str("/tmp/punos-compositorserv-test", &endpoint) ==
              CORE_OK);
    EXPECT(2, compositor_service_create(&endpoint, token, &service) == CORE_OK);
    EXPECT(3, connect_client(&endpoint, service, &client) == CORE_OK);
    EXPECT(4, ipc_connection_close(&client) == CORE_OK);
    EXPECT(5, compositor_service_poll(service, 0) == CORE_OK);
    EXPECT(6, compositor_service_close(&service) == CORE_OK);
    EXPECT(7, string_destroy(&endpoint) == CORE_OK);
}

static void test_surface_create_destroy(void)
{
    String endpoint;
    CompositorServiceHandle service;
    IpcConnectionHandle client;
    u8 controller_token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE] = { 2 };
    u8 payload[64];
    usize size;
    GuiRequestPrefix create = { GUI_PROTOCOL_VERSION, 1 };
    GuiSurfaceCreateResult created;
    GuiSurfaceIdRequest destroy;
    GuiBasicResult destroyed;
    IpcMessage message;

    EXPECT(20, string_from_c_str("/tmp/punos-compositorserv-surface-test",
                                 &endpoint) == CORE_OK);
    EXPECT(21, compositor_service_create(&endpoint, controller_token, &service) ==
               CORE_OK);
    EXPECT(22, connect_client(&endpoint, service, &client) == CORE_OK);

    EXPECT(23, gui_encode_versioned_request(create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(24, send_message(client, GUI_SURFACE_CREATE, payload, size) == CORE_OK);
    EXPECT(25, receive_message(service, client, &message) == CORE_OK);
    EXPECT(26, gui_decode_surface_create_result(&message, &created) == CORE_OK);
    EXPECT(27, created.result.error == CORE_OK);
    EXPECT(28, created.result.request_id == 1);
    EXPECT(29, created.surface_id != 0);
    EXPECT(30, ipc_message_destroy(&message) == CORE_OK);

    destroy.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    destroy.prefix.request_id = 2;
    destroy.surface_id = created.surface_id;
    EXPECT(31, gui_encode_surface_id_request(&destroy,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(32, send_message(client, GUI_SURFACE_DESTROY, payload, size) == CORE_OK);
    EXPECT(33, receive_message(service, client, &message) == CORE_OK);
    EXPECT(34, gui_decode_basic_result(&message, GUI_SURFACE_DESTROY_RESULT,
                                       &destroyed) == CORE_OK);
    EXPECT(35, destroyed.error == CORE_OK);
    EXPECT(36, destroyed.request_id == 2);
    EXPECT(37, ipc_message_destroy(&message) == CORE_OK);

    destroy.prefix.request_id = 3;
    EXPECT(38, gui_encode_surface_id_request(&destroy,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(39, send_message(client, GUI_SURFACE_DESTROY, payload, size) == CORE_OK);
    EXPECT(40, receive_message(service, client, &message) == CORE_OK);
    EXPECT(41, gui_decode_basic_result(&message, GUI_SURFACE_DESTROY_RESULT,
                                       &destroyed) == CORE_OK);
    EXPECT(42, destroyed.error == CORE_NOT_FOUND);
    EXPECT(43, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(44, ipc_connection_close(&client) == CORE_OK);
    EXPECT(45, compositor_service_poll(service, 0) == CORE_OK);
    EXPECT(46, compositor_service_close(&service) == CORE_OK);
    EXPECT(47, string_destroy(&endpoint) == CORE_OK);
}

static void test_buffer_import_destroy(void)
{
    String endpoint;
    CompositorServiceHandle service;
    IpcConnectionHandle client;
    u8 controller_token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE] = { 3 };
    SharedMemory memory;
    u8 payload[64];
    usize size;
    GuiBufferImportRequest import_request;
    GuiBufferImportResult imported;
    GuiBufferIdRequest destroy;
    GuiBasicResult destroyed;
    IpcMessage message;

    EXPECT(60, string_from_c_str("/tmp/punos-compositorserv-buffer-test",
                                 &endpoint) == CORE_OK);
    EXPECT(61, compositor_service_create(&endpoint, controller_token, &service) ==
               CORE_OK);
    EXPECT(62, connect_client(&endpoint, service, &client) == CORE_OK);
    EXPECT(63, memory_shared_create(64, &memory) == CORE_OK);

    import_request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    import_request.prefix.request_id = 10;
    import_request.width = 4;
    import_request.height = 4;
    import_request.stride = 16;
    import_request.pixel_format = GUI_PIXEL_XRGB8888;
    import_request.byte_size = 64;
    EXPECT(64, gui_encode_buffer_import(&import_request,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(65, send_message_with_descriptor(client, GUI_SURFACE_BUFFER_IMPORT,
        payload, size, memory.fd) == CORE_OK);
    EXPECT(66, receive_message(service, client, &message) == CORE_OK);
    EXPECT(67, gui_decode_buffer_import_result(&message, &imported) == CORE_OK);
    EXPECT(68, imported.result.error == CORE_OK);
    EXPECT(69, imported.result.request_id == 10);
    EXPECT(70, imported.buffer_id != 0);
    EXPECT(71, ipc_message_destroy(&message) == CORE_OK);

    destroy.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    destroy.prefix.request_id = 11;
    destroy.buffer_id = imported.buffer_id;
    EXPECT(72, gui_encode_buffer_id_request(&destroy,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(73, send_message(client, GUI_SURFACE_BUFFER_DESTROY, payload, size) ==
               CORE_OK);
    EXPECT(74, receive_message(service, client, &message) == CORE_OK);
    EXPECT(75, gui_decode_basic_result(&message, GUI_SURFACE_BUFFER_DESTROY_RESULT,
                                       &destroyed) == CORE_OK);
    EXPECT(76, destroyed.error == CORE_OK);
    EXPECT(77, destroyed.request_id == 11);
    EXPECT(78, ipc_message_destroy(&message) == CORE_OK);

    /* Metadata is valid, but byte_size must exactly match the backing object. */
    import_request.prefix.request_id = 12;
    import_request.byte_size = 68;
    EXPECT(79, gui_encode_buffer_import(&import_request,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(80, send_message_with_descriptor(client, GUI_SURFACE_BUFFER_IMPORT,
        payload, size, memory.fd) == CORE_OK);
    EXPECT(81, receive_message(service, client, &message) == CORE_OK);
    EXPECT(82, gui_decode_buffer_import_result(&message, &imported) == CORE_OK);
    EXPECT(83, imported.result.error == CORE_INVALID_ARGUMENT);
    EXPECT(84, imported.buffer_id == 0);
    EXPECT(85, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(86, memory_shared_close(&memory) == CORE_OK);
    EXPECT(87, ipc_connection_close(&client) == CORE_OK);
    EXPECT(88, compositor_service_poll(service, 0) == CORE_OK);
    EXPECT(89, compositor_service_close(&service) == CORE_OK);
    EXPECT(90, string_destroy(&endpoint) == CORE_OK);
}


static void test_surface_pending_commit(void)
{
    String endpoint;
    CompositorServiceHandle service;
    IpcConnectionHandle client;
    u8 controller_token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE] = { 4 };
    SharedMemory memory;
    u8 payload[128];
    usize size;
    IpcMessage message;
    GuiRequestPrefix create = { GUI_PROTOCOL_VERSION, 20 };
    GuiSurfaceCreateResult created;
    GuiBufferImportRequest import_request;
    GuiBufferImportResult imported;
    GuiSurfaceAttachRequest attach;
    GuiSurfaceDamageRequest damage;
    GuiSurfaceRegionRequest opaque;
    GuiSurfaceFrameRequest frame;
    GuiSurfaceCommitRequest commit;
    GuiBasicResult committed;

    EXPECT(100, string_from_c_str("/tmp/punos-compositorserv-commit-test",
                                  &endpoint) == CORE_OK);
    EXPECT(101, compositor_service_create(&endpoint, controller_token, &service) ==
                CORE_OK);
    EXPECT(102, connect_client(&endpoint, service, &client) == CORE_OK);

    EXPECT(103, gui_encode_versioned_request(create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(104, send_message(client, GUI_SURFACE_CREATE, payload, size) == CORE_OK);
    EXPECT(105, receive_message(service, client, &message) == CORE_OK);
    EXPECT(106, gui_decode_surface_create_result(&message, &created) == CORE_OK);
    EXPECT(107, created.result.error == CORE_OK);
    EXPECT(108, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(109, memory_shared_create(64, &memory) == CORE_OK);
    import_request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    import_request.prefix.request_id = 21;
    import_request.width = 4;
    import_request.height = 4;
    import_request.stride = 16;
    import_request.pixel_format = GUI_PIXEL_ARGB8888;
    import_request.byte_size = 64;
    EXPECT(110, gui_encode_buffer_import(&import_request,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(111, send_message_with_descriptor(client, GUI_SURFACE_BUFFER_IMPORT,
        payload, size, memory.fd) == CORE_OK);
    EXPECT(112, receive_message(service, client, &message) == CORE_OK);
    EXPECT(113, gui_decode_buffer_import_result(&message, &imported) == CORE_OK);
    EXPECT(114, imported.result.error == CORE_OK);
    EXPECT(115, ipc_message_destroy(&message) == CORE_OK);

    attach.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    attach.prefix.request_id = 22;
    attach.surface_id = created.surface_id;
    attach.buffer_id = imported.buffer_id;
    EXPECT(116, gui_encode_surface_attach(&attach,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(117, send_message(client, GUI_SURFACE_ATTACH, payload, size) == CORE_OK);

    damage.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    damage.prefix.request_id = 23;
    damage.surface_id = created.surface_id;
    damage.flags = GUI_FULL_DAMAGE;
    damage.rect_count = 0;
    EXPECT(118, gui_encode_surface_damage(&damage,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(119, send_message(client, GUI_SURFACE_DAMAGE, payload, size) == CORE_OK);

    opaque.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    opaque.prefix.request_id = 24;
    opaque.surface_id = created.surface_id;
    opaque.rect_count = 1;
    opaque.rects[0] = (GuiRect){ 0, 0, 2, 2 };
    EXPECT(120, gui_encode_surface_region(&opaque,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(121, send_message(client, GUI_SURFACE_SET_OPAQUE, payload, size) == CORE_OK);

    frame.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    frame.prefix.request_id = 25;
    frame.surface_id = created.surface_id;
    EXPECT(122, gui_encode_surface_frame_request(&frame,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(123, send_message(client, GUI_SURFACE_REQUEST_FRAME, payload, size) ==
                CORE_OK);

    commit.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    commit.prefix.request_id = 26;
    commit.surface_id = created.surface_id;
    commit.commit_serial = 1;
    EXPECT(124, gui_encode_surface_commit(&commit,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(125, send_message(client, GUI_SURFACE_COMMIT, payload, size) == CORE_OK);
    EXPECT(126, receive_message(service, client, &message) == CORE_OK);
    EXPECT(127, gui_decode_basic_result(&message, GUI_SURFACE_COMMIT_RESULT,
                                        &committed) == CORE_OK);
    EXPECT(128, committed.error == CORE_OK);
    EXPECT(129, committed.request_id == 26);
    EXPECT(130, ipc_message_destroy(&message) == CORE_OK);

    commit.prefix.request_id = 27;
    EXPECT(131, gui_encode_surface_commit(&commit,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(132, send_message(client, GUI_SURFACE_COMMIT, payload, size) == CORE_OK);
    EXPECT(133, receive_message(service, client, &message) == CORE_OK);
    EXPECT(134, gui_decode_basic_result(&message, GUI_SURFACE_COMMIT_RESULT,
                                        &committed) == CORE_OK);
    EXPECT(135, committed.error == CORE_INVALID_STATE);
    EXPECT(136, ipc_message_destroy(&message) == CORE_OK);

    attach.prefix.request_id = 28;
    attach.buffer_id = 0;
    EXPECT(137, gui_encode_surface_attach(&attach,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(138, send_message(client, GUI_SURFACE_ATTACH, payload, size) == CORE_OK);
    commit.prefix.request_id = 29;
    commit.commit_serial = 2;
    EXPECT(139, gui_encode_surface_commit(&commit,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(140, send_message(client, GUI_SURFACE_COMMIT, payload, size) == CORE_OK);
    EXPECT(141, receive_message(service, client, &message) == CORE_OK);
    EXPECT(142, gui_decode_basic_result(&message, GUI_SURFACE_COMMIT_RESULT,
                                        &committed) == CORE_OK);
    EXPECT(143, committed.error == CORE_OK);
    EXPECT(144, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(145, memory_shared_close(&memory) == CORE_OK);
    EXPECT(146, ipc_connection_close(&client) == CORE_OK);
    EXPECT(147, compositor_service_poll(service, 0) == CORE_OK);
    EXPECT(148, compositor_service_close(&service) == CORE_OK);
    EXPECT(149, string_destroy(&endpoint) == CORE_OK);
}


static void test_scene_controller_and_graph(void)
{
    String endpoint;
    CompositorServiceHandle service;
    IpcConnectionHandle app_client;
    IpcConnectionHandle controller;
    u8 controller_token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE] = { 9 };
    u8 payload[128];
    usize size;
    IpcMessage message;
    GuiRequestPrefix create = { GUI_PROTOCOL_VERSION, 40 };
    GuiSurfaceCreateResult created;
    GuiSurfaceCreateResult created_second;
    GuiControllerBindRequest bind_controller;
    GuiSceneBindRequest bind;
    GuiSceneGeometryRequest geometry;
    GuiSceneVisibilityRequest visibility;
    GuiSceneStackRequest stack;
    GuiSceneOutputRequest output;
    GuiSceneWindowRequest remove;
    GuiBasicResult result;
    usize index;

    EXPECT(150, string_from_c_str("/tmp/punos-compositorserv-scene-test",
                                  &endpoint) == CORE_OK);
    EXPECT(151, compositor_service_create(&endpoint, controller_token, &service) ==
                CORE_OK);
    EXPECT(152, connect_client(&endpoint, service, &app_client) == CORE_OK);
    EXPECT(153, connect_client(&endpoint, service, &controller) == CORE_OK);

    EXPECT(154, gui_encode_versioned_request(create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(155, send_message(app_client, GUI_SURFACE_CREATE, payload, size) == CORE_OK);
    EXPECT(156, receive_message(service, app_client, &message) == CORE_OK);
    EXPECT(157, gui_decode_surface_create_result(&message, &created) == CORE_OK);
    EXPECT(158, created.result.error == CORE_OK);
    EXPECT(159, ipc_message_destroy(&message) == CORE_OK);

    bind_controller.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    bind_controller.prefix.request_id = 41;
    for (index = 0; index < GUI_CONTROLLER_TOKEN_SIZE; ++index) {
        bind_controller.controller_token[index] = controller_token[index];
    }
    EXPECT(160, gui_encode_controller_bind(&bind_controller,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(161, send_message(controller, GUI_COMPOSITOR_BIND_CONTROLLER,
                             payload, size) == CORE_OK);
    EXPECT(162, receive_message(service, controller, &message) == CORE_OK);
    EXPECT(163, gui_decode_basic_result(&message,
        GUI_COMPOSITOR_BIND_CONTROLLER_RESULT, &result) == CORE_OK);
    EXPECT(164, result.error == CORE_OK && result.request_id == 41);
    EXPECT(165, ipc_message_destroy(&message) == CORE_OK);

    bind.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    bind.prefix.request_id = 42;
    bind.window_id = 1001;
    bind.surface_id = created.surface_id;
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        bind.surface_bind_token[index] = created.surface_bind_token[index];
    }
    EXPECT(166, gui_encode_scene_bind(&bind,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(167, send_message(controller, GUI_SCENE_BIND_WINDOW,
                             payload, size) == CORE_OK);
    EXPECT(168, receive_message(service, controller, &message) == CORE_OK);
    EXPECT(169, gui_decode_basic_result(&message, GUI_SCENE_RESULT, &result) == CORE_OK);
    EXPECT(170, result.error == CORE_OK && result.request_id == 42);
    EXPECT(171, ipc_message_destroy(&message) == CORE_OK);

    geometry.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    geometry.prefix.request_id = 43;
    geometry.window_id = 1001;
    geometry.geometry = (GuiSceneRect){ 0, 0, 4, 4 };
    geometry.clip_x = 0;
    geometry.clip_y = 0;
    geometry.clip_width = 0;
    geometry.clip_height = 0;
    EXPECT(172, gui_encode_scene_geometry(&geometry,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(173, send_message(controller, GUI_SCENE_SET_GEOMETRY,
                             payload, size) == CORE_OK);
    EXPECT(174, receive_message(service, controller, &message) == CORE_OK);
    EXPECT(175, gui_decode_basic_result(&message, GUI_SCENE_RESULT, &result) == CORE_OK);
    EXPECT(176, result.error == CORE_OK);
    EXPECT(177, ipc_message_destroy(&message) == CORE_OK);

    visibility.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    visibility.prefix.request_id = 44;
    visibility.window_id = 1001;
    visibility.visibility = GUI_SCENE_VISIBLE;
    EXPECT(178, gui_encode_scene_visibility(&visibility,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(179, send_message(controller, GUI_SCENE_SET_VISIBILITY,
                             payload, size) == CORE_OK);
    EXPECT(180, receive_message(service, controller, &message) == CORE_OK);
    EXPECT(181, gui_decode_basic_result(&message, GUI_SCENE_RESULT, &result) == CORE_OK);
    /* VISIBLE is invalid until the bound surface has a current buffer. */
    EXPECT(182, result.error == CORE_INVALID_STATE);
    EXPECT(183, ipc_message_destroy(&message) == CORE_OK);

    stack.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    stack.prefix.request_id = 45;
    stack.window_id = 1001;
    stack.layer = GUI_SCENE_LAYER_APP;
    stack.above_window_id = 0;
    EXPECT(184, gui_encode_scene_stack(&stack,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(185, send_message(controller, GUI_SCENE_SET_STACK,
                             payload, size) == CORE_OK);
    EXPECT(186, receive_message(service, controller, &message) == CORE_OK);
    EXPECT(187, gui_decode_basic_result(&message, GUI_SCENE_RESULT, &result) == CORE_OK);
    EXPECT(188, result.error == CORE_OK);
    EXPECT(189, ipc_message_destroy(&message) == CORE_OK);

    /* Stacking validation: self anchors, cross-layer anchors and cycles are
     * protocol errors and must leave the graph unchanged. */
    stack.prefix.request_id = 450;
    stack.above_window_id = 1001;
    EXPECT(225, gui_encode_scene_stack(&stack,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_INVALID_ARGUMENT);

    create.request_id = 451;
    EXPECT(230, gui_encode_versioned_request(create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(231, send_message(app_client, GUI_SURFACE_CREATE, payload, size) == CORE_OK);
    EXPECT(232, receive_message(service, app_client, &message) == CORE_OK);
    EXPECT(233, gui_decode_surface_create_result(&message, &created_second) == CORE_OK &&
                created_second.result.error == CORE_OK);
    EXPECT(234, ipc_message_destroy(&message) == CORE_OK);

    bind.prefix.request_id = 452;
    bind.window_id = 1002;
    bind.surface_id = created_second.surface_id;
    for (index = 0; index < GUI_SURFACE_BIND_TOKEN_SIZE; ++index) {
        bind.surface_bind_token[index] = created_second.surface_bind_token[index];
    }
    EXPECT(235, gui_encode_scene_bind(&bind,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(236, send_message(controller, GUI_SCENE_BIND_WINDOW, payload, size) == CORE_OK);
    EXPECT(237, receive_message(service, controller, &message) == CORE_OK);
    EXPECT(238, gui_decode_basic_result(&message, GUI_SCENE_RESULT, &result) == CORE_OK &&
                result.error == CORE_OK);
    EXPECT(239, ipc_message_destroy(&message) == CORE_OK);

    stack.prefix.request_id = 453;
    stack.window_id = 1002;
    stack.layer = GUI_SCENE_LAYER_DIALOG;
    stack.above_window_id = 0;
    EXPECT(240, gui_encode_scene_stack(&stack,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(241, send_message(controller, GUI_SCENE_SET_STACK, payload, size) == CORE_OK);
    EXPECT(242, receive_message(service, controller, &message) == CORE_OK);
    EXPECT(243, gui_decode_basic_result(&message, GUI_SCENE_RESULT, &result) == CORE_OK &&
                result.error == CORE_OK);
    EXPECT(244, ipc_message_destroy(&message) == CORE_OK);

    stack.prefix.request_id = 454;
    stack.window_id = 1001;
    stack.layer = GUI_SCENE_LAYER_APP;
    stack.above_window_id = 1002;
    EXPECT(245, gui_encode_scene_stack(&stack,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(246, send_message(controller, GUI_SCENE_SET_STACK, payload, size) == CORE_OK);
    EXPECT(247, receive_message(service, controller, &message) == CORE_OK);
    EXPECT(248, gui_decode_basic_result(&message, GUI_SCENE_RESULT, &result) == CORE_OK &&
                result.error == CORE_INVALID_ARGUMENT);
    EXPECT(249, ipc_message_destroy(&message) == CORE_OK);

    stack.prefix.request_id = 455;
    stack.window_id = 1002;
    stack.layer = GUI_SCENE_LAYER_APP;
    stack.above_window_id = 1001;
    EXPECT(250, gui_encode_scene_stack(&stack,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(251, send_message(controller, GUI_SCENE_SET_STACK, payload, size) == CORE_OK);
    EXPECT(252, receive_message(service, controller, &message) == CORE_OK);
    EXPECT(253, gui_decode_basic_result(&message, GUI_SCENE_RESULT, &result) == CORE_OK &&
                result.error == CORE_OK);
    EXPECT(254, ipc_message_destroy(&message) == CORE_OK);

    stack.prefix.request_id = 456;
    stack.window_id = 1001;
    stack.layer = GUI_SCENE_LAYER_APP;
    stack.above_window_id = 1002;
    EXPECT(200, gui_encode_scene_stack(&stack,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(201, send_message(controller, GUI_SCENE_SET_STACK, payload, size) == CORE_OK);
    EXPECT(202, receive_message(service, controller, &message) == CORE_OK);
    EXPECT(203, gui_decode_basic_result(&message, GUI_SCENE_RESULT, &result) == CORE_OK &&
                result.error == CORE_INVALID_STATE);
    EXPECT(204, ipc_message_destroy(&message) == CORE_OK);

    output.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    output.prefix.request_id = 46;
    output.window_id = 1001;
    output.display_id = 1;
    EXPECT(190, gui_encode_scene_output(&output,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(191, send_message(controller, GUI_SCENE_SET_OUTPUT,
                             payload, size) == CORE_OK);
    EXPECT(192, receive_message(service, controller, &message) == CORE_OK);
    EXPECT(193, gui_decode_basic_result(&message, GUI_SCENE_RESULT, &result) == CORE_OK);
    EXPECT(194, result.error == CORE_OK);
    EXPECT(195, ipc_message_destroy(&message) == CORE_OK);

    remove.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    remove.prefix.request_id = 47;
    remove.window_id = 1001;
    EXPECT(196, gui_encode_scene_window(&remove,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(197, send_message(controller, GUI_SCENE_REMOVE_WINDOW,
                             payload, size) == CORE_OK);
    EXPECT(198, receive_message(service, controller, &message) == CORE_OK);
    EXPECT(199, gui_decode_basic_result(&message, GUI_SCENE_RESULT, &result) == CORE_OK);
    EXPECT(150, result.error == CORE_OK);
    EXPECT(151, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(152, ipc_connection_close(&app_client) == CORE_OK);
    EXPECT(153, ipc_connection_close(&controller) == CORE_OK);
    EXPECT(154, compositor_service_poll(service, 0) == CORE_OK);
    EXPECT(155, compositor_service_poll(service, 0) == CORE_OK);
    EXPECT(156, compositor_service_close(&service) == CORE_OK);
    EXPECT(157, string_destroy(&endpoint) == CORE_OK);
}


static void test_controller_bind_failure_limit(void)
{
    String endpoint;
    CompositorServiceHandle service;
    IpcConnectionHandle client;
    u8 token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE] = { 0x55 };
    u8 payload[64];
    usize size;
    usize attempt;
    GuiControllerBindRequest request;

    EXPECT(210, string_from_c_str("/tmp/punos-compositorserv-controller-limit",
                                  &endpoint) == CORE_OK);
    EXPECT(211, compositor_service_create(&endpoint, token, &service) == CORE_OK);
    EXPECT(212, connect_client(&endpoint, service, &client) == CORE_OK);

    request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    for (attempt = 0; attempt < GUI_CONTROLLER_TOKEN_SIZE; ++attempt) {
        request.controller_token[attempt] = 0;
    }
    for (attempt = 0; attempt < 3; ++attempt) {
        IpcReceiveStatus status = IPC_RECEIVE_NO_MESSAGE;
        IpcMessage message;
        GuiBasicResult result;
        usize pump;

        request.prefix.request_id = 500 + attempt;
        EXPECT(213, gui_encode_controller_bind(&request,
            (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
        EXPECT(214, send_message(client, GUI_COMPOSITOR_BIND_CONTROLLER,
                                 payload, size) == CORE_OK);
        for (pump = 0; pump < 1000 && status == IPC_RECEIVE_NO_MESSAGE; ++pump) {
            EXPECT(215, compositor_service_poll(service, 0) == CORE_OK);
            EXPECT(216, ipc_connection_receive(client, &status, &message) == CORE_OK);
        }
        if (status == IPC_RECEIVE_MESSAGE) {
            EXPECT(217, gui_decode_basic_result(&message,
                GUI_COMPOSITOR_BIND_CONTROLLER_RESULT, &result) == CORE_OK &&
                result.error == CORE_PERMISSION_DENIED);
            EXPECT(218, ipc_message_destroy(&message) == CORE_OK);
        }
        if (attempt < 2) {
            EXPECT(219, status == IPC_RECEIVE_MESSAGE);
        }
    }
    {
        IpcReceiveStatus status = IPC_RECEIVE_NO_MESSAGE;
        IpcMessage message;
        usize pump;
        for (pump = 0; pump < 1000 && status != IPC_RECEIVE_PEER_CLOSED; ++pump) {
            EXPECT(220, compositor_service_poll(service, 0) == CORE_OK);
            EXPECT(221, ipc_connection_receive(client, &status, &message) == CORE_OK);
            if (status == IPC_RECEIVE_MESSAGE) {
                EXPECT(222, ipc_message_destroy(&message) == CORE_OK);
            }
        }
        EXPECT(223, status == IPC_RECEIVE_PEER_CLOSED);
    }

    (void)ipc_connection_close(&client);
    EXPECT(224, compositor_service_close(&service) == CORE_OK);
    EXPECT(225, string_destroy(&endpoint) == CORE_OK);
}

static void test_software_renderer(void)
{
    u8 output[4 * 4 * 4];
    u8 opaque[2 * 2 * 4] = {
        10, 20, 30, 0,   40, 50, 60, 0,
        70, 80, 90, 0,  100, 110, 120, 0
    };
    /* Premultiplied 50% red: B=0, G=0, R=128, A=128. */
    u8 translucent[4] = { 0, 0, 128, 128 };
    CompositorRenderTarget target = { output, 4, 4, 16 };
    CompositorRenderLayer layers[2];
    u8 *pixel;

    layers[0].pixels = opaque;
    layers[0].width = 2;
    layers[0].height = 2;
    layers[0].stride = 8;
    layers[0].format = GUI_PIXEL_XRGB8888;
    layers[0].x = 1;
    layers[0].y = 1;
    layers[0].clip_set = false;
    layers[0].clip_x = 0;
    layers[0].clip_y = 0;
    layers[0].clip_width = 0;
    layers[0].clip_height = 0;

    layers[1].pixels = translucent;
    layers[1].width = 1;
    layers[1].height = 1;
    layers[1].stride = 4;
    layers[1].format = GUI_PIXEL_ARGB8888;
    layers[1].x = 1;
    layers[1].y = 1;
    layers[1].clip_set = false;
    layers[1].clip_x = 0;
    layers[1].clip_y = 0;
    layers[1].clip_width = 0;
    layers[1].clip_height = 0;

    EXPECT(210, compositor_render_clear(&target, 1, 2, 3) == CORE_OK);
    EXPECT(211, compositor_render_layers(&target, layers, 2) == CORE_OK);

    pixel = output + 0 * 16 + 0 * 4;
    EXPECT(212, pixel[0] == 3 && pixel[1] == 2 && pixel[2] == 1 &&
                pixel[3] == 255);

    pixel = output + 1 * 16 + 1 * 4;
    EXPECT(213, pixel[0] == 5 && pixel[1] == 10 && pixel[2] == 143 &&
                pixel[3] == 255);

    pixel = output + 1 * 16 + 2 * 4;
    EXPECT(214, pixel[0] == 40 && pixel[1] == 50 && pixel[2] == 60 &&
                pixel[3] == 255);

    /* Verify clipping at the output edge: only one source pixel is visible. */
    layers[0].x = 3;
    layers[0].y = 3;
    EXPECT(215, compositor_render_clear(&target, 0, 0, 0) == CORE_OK);
    EXPECT(216, compositor_render_layer(&target, &layers[0]) == CORE_OK);
    pixel = output + 3 * 16 + 3 * 4;
    EXPECT(217, pixel[0] == 10 && pixel[1] == 20 && pixel[2] == 30 &&
                pixel[3] == 255);
    pixel = output + 2 * 16 + 3 * 4;
    EXPECT(218, pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 &&
                pixel[3] == 255);

    /* Verify an explicit source clip rectangle. */
    layers[0].x = 0;
    layers[0].y = 0;
    layers[0].clip_set = true;
    layers[0].clip_x = 1;
    layers[0].clip_y = 0;
    layers[0].clip_width = 1;
    layers[0].clip_height = 2;
    EXPECT(219, compositor_render_clear(&target, 0, 0, 0) == CORE_OK);
    EXPECT(220, compositor_render_layer(&target, &layers[0]) == CORE_OK);
    pixel = output + 0 * 16 + 0 * 4;
    EXPECT(221, pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0);
    pixel = output + 0 * 16 + 1 * 4;
    EXPECT(222, pixel[0] == 40 && pixel[1] == 50 && pixel[2] == 60);
}


static void test_resource_cleanup(void)
{
    String endpoint;
    CompositorServiceHandle service;
    IpcConnectionHandle client;
    u8 controller_token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE] = { 7 };
    SharedMemory memory;
    u8 payload[96];
    usize size;
    GuiRequestPrefix create = { GUI_PROTOCOL_VERSION, 300 };
    GuiSurfaceCreateResult created;
    GuiBufferImportRequest import_request;
    GuiBufferImportResult imported;
    GuiSurfaceAttachRequest attach;
    GuiSurfaceIdRequest destroy_surface;
    GuiBufferIdRequest destroy_buffer;
    GuiBasicResult basic;
    IpcMessage message;
    bool reconnect_cleanup_ok = true;
    usize iteration;

    EXPECT(223, string_from_c_str("/tmp/punos-compositorserv-cleanup-test",
                                  &endpoint) == CORE_OK);
    EXPECT(224, compositor_service_create(&endpoint, controller_token, &service) ==
                CORE_OK);
    EXPECT(225, connect_client(&endpoint, service, &client) == CORE_OK);
    EXPECT(226, memory_shared_create(64, &memory) == CORE_OK);

    EXPECT(227, gui_encode_versioned_request(create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(228, send_message(client, GUI_SURFACE_CREATE, payload, size) == CORE_OK);
    EXPECT(229, receive_message(service, client, &message) == CORE_OK);
    EXPECT(230, gui_decode_surface_create_result(&message, &created) == CORE_OK);
    EXPECT(231, created.result.error == CORE_OK);
    EXPECT(232, ipc_message_destroy(&message) == CORE_OK);

    import_request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    import_request.prefix.request_id = 301;
    import_request.width = 4;
    import_request.height = 4;
    import_request.stride = 16;
    import_request.pixel_format = GUI_PIXEL_XRGB8888;
    import_request.byte_size = 64;
    EXPECT(233, gui_encode_buffer_import(&import_request,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(234, send_message_with_descriptor(client, GUI_SURFACE_BUFFER_IMPORT,
        payload, size, memory.fd) == CORE_OK);
    EXPECT(235, receive_message(service, client, &message) == CORE_OK);
    EXPECT(236, gui_decode_buffer_import_result(&message, &imported) == CORE_OK);
    EXPECT(237, imported.result.error == CORE_OK);
    EXPECT(238, ipc_message_destroy(&message) == CORE_OK);

    attach.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    attach.prefix.request_id = 302;
    attach.surface_id = created.surface_id;
    attach.buffer_id = imported.buffer_id;
    EXPECT(239, gui_encode_surface_attach(&attach,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(240, send_message(client, GUI_SURFACE_ATTACH, payload, size) == CORE_OK);

    destroy_surface.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    destroy_surface.prefix.request_id = 303;
    destroy_surface.surface_id = created.surface_id;
    EXPECT(241, gui_encode_surface_id_request(&destroy_surface,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(242, send_message(client, GUI_SURFACE_DESTROY, payload, size) == CORE_OK);
    EXPECT(243, receive_message(service, client, &message) == CORE_OK);
    EXPECT(244, gui_decode_basic_result(&message, GUI_SURFACE_DESTROY_RESULT,
                                        &basic) == CORE_OK);
    EXPECT(245, basic.error == CORE_OK);
    EXPECT(246, ipc_message_destroy(&message) == CORE_OK);

    /* Destroying a surface must release a pending buffer reservation. */
    destroy_buffer.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    destroy_buffer.prefix.request_id = 304;
    destroy_buffer.buffer_id = imported.buffer_id;
    EXPECT(247, gui_encode_buffer_id_request(&destroy_buffer,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(248, send_message(client, GUI_SURFACE_BUFFER_DESTROY, payload, size) ==
                CORE_OK);
    EXPECT(249, receive_message(service, client, &message) == CORE_OK);
    if (gui_decode_basic_result(&message, GUI_SURFACE_BUFFER_DESTROY_RESULT,
                                &basic) != CORE_OK || basic.error != CORE_OK ||
        ipc_message_destroy(&message) != CORE_OK) {
        reconnect_cleanup_ok = false;
    }

    EXPECT(250, ipc_connection_close(&client) == CORE_OK);
    EXPECT(251, compositor_service_poll(service, 0) == CORE_OK);

    /* Reuse the same client slot more times than its buffer table capacity.
     * Every disconnect must unmap/close imported buffers and reset the table. */
    for (iteration = 0; iteration < 70 && reconnect_cleanup_ok; ++iteration) {
        import_request.prefix.request_id = 400 + iteration;
        if (connect_client(&endpoint, service, &client) != CORE_OK ||
            gui_encode_buffer_import(&import_request,
                (Bytes){ payload, sizeof(payload) }, &size) != CORE_OK ||
            send_message_with_descriptor(client, GUI_SURFACE_BUFFER_IMPORT,
                payload, size, memory.fd) != CORE_OK ||
            receive_message(service, client, &message) != CORE_OK ||
            gui_decode_buffer_import_result(&message, &imported) != CORE_OK ||
            imported.result.error != CORE_OK ||
            ipc_message_destroy(&message) != CORE_OK ||
            ipc_connection_close(&client) != CORE_OK ||
            compositor_service_poll(service, 0) != CORE_OK) {
            reconnect_cleanup_ok = false;
        }
    }
    EXPECT(252, reconnect_cleanup_ok);
    EXPECT(253, memory_shared_close(&memory) == CORE_OK);
    EXPECT(254, compositor_service_close(&service) == CORE_OK);
    EXPECT(255, string_destroy(&endpoint) == CORE_OK);
}

int main(void)
{
    test_accept_connection();
    test_surface_create_destroy();
    test_buffer_import_destroy();
    test_surface_pending_commit();
    test_scene_controller_and_graph();
    test_controller_bind_failure_limit();
    test_software_renderer();
    test_resource_cleanup();
    check_passed();
    return 0;
}
