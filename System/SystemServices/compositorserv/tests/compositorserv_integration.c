#define CHECK_TEST_NAME "compositorserv-integration"
#include "../../../SystemLibs/testing/check.h"

#include <compositorserv.h>
#include <displayserv.h>
#include <gui/protocol.h>
#include <special/ipc.h>
#include <sys/memory.h>
#include <sys/time.h>

static ErrorCode pump_pair(DisplayServiceHandle display,
                           CompositorServiceHandle compositor)
{
    ErrorCode result;
    if (display != NULL) {
        result = display_service_poll(display, 0);
        if (result != CORE_OK) return result;
    }
    return compositor_service_poll(compositor, 0);
}

static ErrorCode connect_client_pair(const String *endpoint,
                                     DisplayServiceHandle display,
                                     CompositorServiceHandle compositor,
                                     IpcConnectionHandle *out_client)
{
    IpcConnectionState state = IPC_CONNECTION_CONNECTING;
    usize attempt;
    ErrorCode result = ipc_connection_begin_connect(endpoint, out_client);

    if (result != CORE_OK) return result;
    for (attempt = 0; attempt < 2000 && state == IPC_CONNECTION_CONNECTING;
         ++attempt) {
        result = pump_pair(display, compositor);
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

static ErrorCode receive_message_pair(DisplayServiceHandle display,
                                      CompositorServiceHandle compositor,
                                      IpcConnectionHandle client,
                                      IpcMessage *out_message)
{
    usize attempt;

    for (attempt = 0; attempt < 4000; ++attempt) {
        IpcReceiveStatus status;
        ErrorCode result = pump_pair(display, compositor);
        if (result != CORE_OK) return result;
        result = ipc_connection_receive(client, &status, out_message);
        if (result != CORE_OK) return result;
        if (status == IPC_RECEIVE_MESSAGE) return CORE_OK;
        if (status == IPC_RECEIVE_PEER_CLOSED) return CORE_IO_ERROR;
    }
    return CORE_WOULD_BLOCK;
}

static usize count_open_fds(void)
{
    usize count = 0;
    i32 fd;

    /* F_DUPFD_CLOEXEC is already part of the native syscall ABI used by the
     * rest of the project.  Duplicating into a range outside the scan lets us
     * test whether a descriptor is live without needing /proc. */
    for (fd = 0; fd < 256; ++fd) {
        i32 duplicate = fcntl(fd, F_DUPFD_CLOEXEC, 512);
        if (duplicate >= 0) {
            ++count;
            (void)close(duplicate);
        }
    }
    return count;
}

static bool token_copy(u8 *destination, const u8 *source, usize size)
{
    usize index;
    if (destination == NULL || source == NULL) return false;
    for (index = 0; index < size; ++index) destination[index] = source[index];
    return true;
}

static ErrorCode wait_for_surface_events(DisplayServiceHandle display,
                                         CompositorServiceHandle compositor,
                                         IpcConnectionHandle app,
                                         u64 expected_frame_request,
                                         u64 expected_commit_serial,
                                         u64 expected_released_buffer,
                                         bool require_release)
{
    bool frame_done = false;
    bool released = !require_release;
    usize attempt;

    for (attempt = 0; attempt < 8000 && (!frame_done || !released); ++attempt) {
        IpcReceiveStatus status;
        IpcMessage message;
        ErrorCode result = pump_pair(display, compositor);
        if (result != CORE_OK) return result;
        result = ipc_connection_receive(app, &status, &message);
        if (result != CORE_OK) return result;
        if (status == IPC_RECEIVE_PEER_CLOSED) return CORE_IO_ERROR;
        if (status != IPC_RECEIVE_MESSAGE) continue;

        if (message.type == GUI_SURFACE_FRAME_DONE) {
            GuiSurfaceFrameDoneEvent event;
            result = gui_decode_surface_frame_done(&message, &event);
            if (result == CORE_OK && event.request_id == expected_frame_request &&
                event.commit_serial == expected_commit_serial &&
                event.presented_at_ms != 0) {
                frame_done = true;
            }
        } else if (message.type == GUI_SURFACE_BUFFER_RELEASED) {
            GuiBufferReleasedEvent event;
            result = gui_decode_buffer_released(&message, &event);
            if (result == CORE_OK && event.buffer_id == expected_released_buffer &&
                event.serial != 0) {
                released = true;
            }
        }
        (void)ipc_message_destroy(&message);
        if (result != CORE_OK) return result;
    }
    return (frame_done && released) ? CORE_OK : CORE_WOULD_BLOCK;
}

static void test_display_pipeline_and_events(void)
{
    String compositor_endpoint;
    String display_endpoint;
    DisplayServiceHandle display;
    CompositorServiceHandle compositor;
    CompositorServiceConfig config;
    IpcConnectionHandle app;
    IpcConnectionHandle controller;
    SharedMemory first_memory;
    SharedMemory second_memory;
    u8 controller_token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE] = { 0x31 };
    u8 presenter_token[COMPOSITOR_SERVICE_PRESENTER_TOKEN_SIZE] = { 0x72 };
    u8 payload[256];
    usize size;
    usize index;
    IpcMessage message;
    GuiRequestPrefix create = { GUI_PROTOCOL_VERSION, 1000 };
    GuiSurfaceCreateResult created;
    GuiBufferImportRequest import_request;
    GuiBufferImportResult first_buffer;
    GuiBufferImportResult second_buffer;
    GuiControllerBindRequest bind_controller;
    GuiSceneBindRequest bind;
    GuiSceneGeometryRequest geometry;
    GuiSceneStackRequest stack;
    GuiSceneOutputRequest output;
    GuiSceneVisibilityRequest visibility;
    GuiSurfaceAttachRequest attach;
    GuiSurfaceDamageRequest damage;
    GuiSurfaceFrameRequest frame_request;
    GuiSurfaceCommitRequest commit;
    GuiBasicResult basic;

    EXPECT(1, string_from_c_str("/tmp/punos-compositorserv-e2e-test",
                                  &compositor_endpoint) == CORE_OK);
    EXPECT(2, string_from_c_str("/tmp/punos-displayserv-e2e-test",
                                  &display_endpoint) == CORE_OK);
    EXPECT(3, display_service_create(&display_endpoint, presenter_token,
                                       &display) == CORE_OK);

    config.endpoint = &compositor_endpoint;
    config.controller_token = controller_token;
    config.display_endpoint = &display_endpoint;
    config.presenter_token = presenter_token;
    EXPECT(4, compositor_service_create_config(&config, &compositor) == CORE_OK);

    /* Drive the bind-presenter + display.list handshake and allow the initial
     * empty frame (if any) to complete before creating application content. */
    for (index = 0; index < 64; ++index) {
        EXPECT(5, pump_pair(display, compositor) == CORE_OK);
    }

    EXPECT(6, connect_client_pair(&compositor_endpoint, display, compositor,
                                    &app) == CORE_OK);
    EXPECT(7, connect_client_pair(&compositor_endpoint, display, compositor,
                                    &controller) == CORE_OK);

    EXPECT(8, gui_encode_versioned_request(create,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(9, send_message(app, GUI_SURFACE_CREATE, payload, size) == CORE_OK);
    EXPECT(10, receive_message_pair(display, compositor, app, &message) == CORE_OK);
    EXPECT(11, gui_decode_surface_create_result(&message, &created) == CORE_OK &&
                created.result.error == CORE_OK);
    EXPECT(12, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(13, memory_shared_create(64, &first_memory) == CORE_OK);
    EXPECT(14, memory_shared_create(64, &second_memory) == CORE_OK);
    for (index = 0; index < 16; ++index) {
        first_memory.data[index * 4 + 0] = 10;
        first_memory.data[index * 4 + 1] = 20;
        first_memory.data[index * 4 + 2] = 30;
        first_memory.data[index * 4 + 3] = 0;
        second_memory.data[index * 4 + 0] = 40;
        second_memory.data[index * 4 + 1] = 50;
        second_memory.data[index * 4 + 2] = 60;
        second_memory.data[index * 4 + 3] = 0;
    }

    import_request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    import_request.width = 4;
    import_request.height = 4;
    import_request.stride = 16;
    import_request.pixel_format = GUI_PIXEL_XRGB8888;
    import_request.byte_size = 64;

    import_request.prefix.request_id = 1001;
    EXPECT(15, gui_encode_buffer_import(&import_request,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(16, send_message_with_descriptor(app, GUI_SURFACE_BUFFER_IMPORT,
        payload, size, first_memory.fd) == CORE_OK);
    EXPECT(17, receive_message_pair(display, compositor, app, &message) == CORE_OK);
    EXPECT(18, gui_decode_buffer_import_result(&message, &first_buffer) == CORE_OK &&
                first_buffer.result.error == CORE_OK && first_buffer.buffer_id != 0);
    EXPECT(19, ipc_message_destroy(&message) == CORE_OK);

    import_request.prefix.request_id = 1002;
    EXPECT(20, gui_encode_buffer_import(&import_request,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(21, send_message_with_descriptor(app, GUI_SURFACE_BUFFER_IMPORT,
        payload, size, second_memory.fd) == CORE_OK);
    EXPECT(22, receive_message_pair(display, compositor, app, &message) == CORE_OK);
    EXPECT(23, gui_decode_buffer_import_result(&message, &second_buffer) == CORE_OK &&
                second_buffer.result.error == CORE_OK && second_buffer.buffer_id != 0);
    EXPECT(24, ipc_message_destroy(&message) == CORE_OK);

    bind_controller.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION, 1010 };
    EXPECT(25, token_copy(bind_controller.controller_token, controller_token,
                           GUI_CONTROLLER_TOKEN_SIZE));
    EXPECT(26, gui_encode_controller_bind(&bind_controller,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(27, send_message(controller, GUI_COMPOSITOR_BIND_CONTROLLER,
                             payload, size) == CORE_OK);
    EXPECT(28, receive_message_pair(display, compositor, controller,
                                     &message) == CORE_OK);
    EXPECT(29, gui_decode_basic_result(&message,
        GUI_COMPOSITOR_BIND_CONTROLLER_RESULT, &basic) == CORE_OK &&
        basic.error == CORE_OK);
    EXPECT(30, ipc_message_destroy(&message) == CORE_OK);

    bind.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION, 1011 };
    bind.window_id = 5001;
    bind.surface_id = created.surface_id;
    EXPECT(31, token_copy(bind.surface_bind_token, created.surface_bind_token,
                           GUI_SURFACE_BIND_TOKEN_SIZE));
    EXPECT(32, gui_encode_scene_bind(&bind,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(33, send_message(controller, GUI_SCENE_BIND_WINDOW,
                             payload, size) == CORE_OK);
    EXPECT(34, receive_message_pair(display, compositor, controller,
                                     &message) == CORE_OK);
    EXPECT(35, gui_decode_basic_result(&message, GUI_SCENE_RESULT, &basic) == CORE_OK &&
                basic.error == CORE_OK);
    EXPECT(36, ipc_message_destroy(&message) == CORE_OK);

    geometry.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION, 1012 };
    geometry.window_id = 5001;
    geometry.geometry = (GuiSceneRect){ 0, 0, 4, 4 };
    geometry.clip_x = 0;
    geometry.clip_y = 0;
    geometry.clip_width = 0;
    geometry.clip_height = 0;
    EXPECT(37, gui_encode_scene_geometry(&geometry,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(38, send_message(controller, GUI_SCENE_SET_GEOMETRY,
                             payload, size) == CORE_OK);
    EXPECT(39, receive_message_pair(display, compositor, controller,
                                     &message) == CORE_OK);
    EXPECT(40, gui_decode_basic_result(&message, GUI_SCENE_RESULT, &basic) == CORE_OK &&
                basic.error == CORE_OK);
    EXPECT(41, ipc_message_destroy(&message) == CORE_OK);

    stack.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION, 1013 };
    stack.window_id = 5001;
    stack.layer = GUI_SCENE_LAYER_APP;
    stack.above_window_id = 0;
    EXPECT(42, gui_encode_scene_stack(&stack,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(43, send_message(controller, GUI_SCENE_SET_STACK, payload, size) == CORE_OK);
    EXPECT(44, receive_message_pair(display, compositor, controller,
                                     &message) == CORE_OK);
    EXPECT(45, gui_decode_basic_result(&message, GUI_SCENE_RESULT, &basic) == CORE_OK &&
                basic.error == CORE_OK);
    EXPECT(46, ipc_message_destroy(&message) == CORE_OK);

    output.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION, 1014 };
    output.window_id = 5001;
    output.display_id = 1;
    EXPECT(47, gui_encode_scene_output(&output,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(48, send_message(controller, GUI_SCENE_SET_OUTPUT, payload, size) == CORE_OK);
    EXPECT(49, receive_message_pair(display, compositor, controller,
                                     &message) == CORE_OK);
    EXPECT(50, gui_decode_basic_result(&message, GUI_SCENE_RESULT, &basic) == CORE_OK &&
                basic.error == CORE_OK);
    EXPECT(51, ipc_message_destroy(&message) == CORE_OK);

    attach.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION, 1020 };
    attach.surface_id = created.surface_id;
    attach.buffer_id = first_buffer.buffer_id;
    EXPECT(52, gui_encode_surface_attach(&attach,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(53, send_message(app, GUI_SURFACE_ATTACH, payload, size) == CORE_OK);

    damage.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION, 1021 };
    damage.surface_id = created.surface_id;
    damage.flags = GUI_FULL_DAMAGE;
    damage.rect_count = 0;
    EXPECT(54, gui_encode_surface_damage(&damage,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(55, send_message(app, GUI_SURFACE_DAMAGE, payload, size) == CORE_OK);

    frame_request.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION, 1022 };
    frame_request.surface_id = created.surface_id;
    EXPECT(56, gui_encode_surface_frame_request(&frame_request,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(57, send_message(app, GUI_SURFACE_REQUEST_FRAME, payload, size) == CORE_OK);

    commit.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION, 1023 };
    commit.surface_id = created.surface_id;
    commit.commit_serial = 1;
    EXPECT(58, gui_encode_surface_commit(&commit,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(59, send_message(app, GUI_SURFACE_COMMIT, payload, size) == CORE_OK);
    EXPECT(60, receive_message_pair(display, compositor, app, &message) == CORE_OK);
    EXPECT(61, gui_decode_basic_result(&message, GUI_SURFACE_COMMIT_RESULT,
                                        &basic) == CORE_OK && basic.error == CORE_OK);
    EXPECT(62, ipc_message_destroy(&message) == CORE_OK);

    visibility.prefix = (GuiRequestPrefix){ GUI_PROTOCOL_VERSION, 1024 };
    visibility.window_id = 5001;
    visibility.visibility = GUI_SCENE_VISIBLE;
    EXPECT(63, gui_encode_scene_visibility(&visibility,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(64, send_message(controller, GUI_SCENE_SET_VISIBILITY,
                             payload, size) == CORE_OK);
    EXPECT(65, receive_message_pair(display, compositor, controller,
                                     &message) == CORE_OK);
    EXPECT(66, gui_decode_basic_result(&message, GUI_SCENE_RESULT, &basic) == CORE_OK &&
                basic.error == CORE_OK);
    EXPECT(67, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(107, receive_message_pair(display, compositor, app, &message) == CORE_OK);
    {
        GuiSurfaceOutputEvent event;
        EXPECT(108, message.type == GUI_SURFACE_OUTPUT_ENTER &&
                    gui_decode_surface_output_event(&message, GUI_SURFACE_OUTPUT_ENTER,
                                                    &event) == CORE_OK &&
                    event.surface_id == created.surface_id &&
                    event.display_id != 0);
    }
    EXPECT(109, ipc_message_destroy(&message) == CORE_OK);

    /* This can only succeed if compositor -> display.present + FD -> displayserv
     * completed and displayserv emitted display.presented. */
    EXPECT(68, wait_for_surface_events(display, compositor, app, 1022, 1,
                                        0, false) == CORE_OK);

    attach.prefix.request_id = 1030;
    attach.buffer_id = second_buffer.buffer_id;
    EXPECT(69, gui_encode_surface_attach(&attach,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(70, send_message(app, GUI_SURFACE_ATTACH, payload, size) == CORE_OK);
    damage.prefix.request_id = 1031;
    EXPECT(71, gui_encode_surface_damage(&damage,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(72, send_message(app, GUI_SURFACE_DAMAGE, payload, size) == CORE_OK);
    frame_request.prefix.request_id = 1032;
    EXPECT(73, gui_encode_surface_frame_request(&frame_request,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(74, send_message(app, GUI_SURFACE_REQUEST_FRAME, payload, size) == CORE_OK);
    commit.prefix.request_id = 1033;
    commit.commit_serial = 2;
    EXPECT(75, gui_encode_surface_commit(&commit,
        (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
    EXPECT(76, send_message(app, GUI_SURFACE_COMMIT, payload, size) == CORE_OK);
    EXPECT(77, receive_message_pair(display, compositor, app, &message) == CORE_OK);
    EXPECT(78, gui_decode_basic_result(&message, GUI_SURFACE_COMMIT_RESULT,
                                        &basic) == CORE_OK && basic.error == CORE_OK);
    EXPECT(79, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(80, wait_for_surface_events(display, compositor, app, 1032, 2,
                                        first_buffer.buffer_id, true) == CORE_OK);

    /* A displayserv restart must cause output_leave, reconnect, a fresh
     * subscribe/list handshake, output_enter and a full redraw request. */
    EXPECT(110, display_service_close(&display) == CORE_OK);
    for (index = 0; index < 32; ++index) {
        EXPECT(111, compositor_service_poll(compositor, 0) == CORE_OK);
    }
    EXPECT(112, receive_message_pair(NULL, compositor, app, &message) == CORE_OK);
    {
        GuiSurfaceOutputEvent event;
        EXPECT(113, message.type == GUI_SURFACE_OUTPUT_LEAVE &&
                    gui_decode_surface_output_event(&message, GUI_SURFACE_OUTPUT_LEAVE,
                                                    &event) == CORE_OK &&
                    event.surface_id == created.surface_id);
    }
    EXPECT(114, ipc_message_destroy(&message) == CORE_OK);

    {
        DurationMs remaining = 0;
        EXPECT(115, time_sleep(300, &remaining) == CORE_OK);
    }
    EXPECT(116, display_service_create(&display_endpoint, presenter_token,
                                        &display) == CORE_OK);
    for (index = 0; index < 512; ++index) {
        EXPECT(117, pump_pair(display, compositor) == CORE_OK);
    }

    EXPECT(118, receive_message_pair(display, compositor, app, &message) == CORE_OK);
    {
        GuiSurfaceOutputEvent event;
        EXPECT(119, message.type == GUI_SURFACE_OUTPUT_ENTER &&
                    gui_decode_surface_output_event(&message, GUI_SURFACE_OUTPUT_ENTER,
                                                    &event) == CORE_OK &&
                    event.surface_id == created.surface_id);
    }
    EXPECT(120, ipc_message_destroy(&message) == CORE_OK);
    EXPECT(121, receive_message_pair(display, compositor, app, &message) == CORE_OK);
    {
        GuiSurfaceRedrawEvent event;
        EXPECT(122, message.type == GUI_SURFACE_FULL_REDRAW &&
                    gui_decode_surface_redraw(&message, &event) == CORE_OK &&
                    event.surface_id == created.surface_id && event.reason == 3);
    }
    EXPECT(123, ipc_message_destroy(&message) == CORE_OK);

    EXPECT(81, ipc_connection_close(&app) == CORE_OK);
    EXPECT(82, ipc_connection_close(&controller) == CORE_OK);
    for (index = 0; index < 8; ++index) EXPECT(83, pump_pair(display, compositor) == CORE_OK);
    EXPECT(84, memory_shared_close(&first_memory) == CORE_OK);
    EXPECT(85, memory_shared_close(&second_memory) == CORE_OK);
    EXPECT(86, compositor_service_close(&compositor) == CORE_OK);
    EXPECT(87, display_service_close(&display) == CORE_OK);
    EXPECT(88, string_destroy(&compositor_endpoint) == CORE_OK);
    EXPECT(89, string_destroy(&display_endpoint) == CORE_OK);
}

static void test_fd_cleanup_strict(void)
{
    String endpoint;
    CompositorServiceHandle service;
    IpcConnectionHandle client;
    SharedMemory memory;
    u8 token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE] = { 0x44 };
    u8 payload[64];
    usize size;
    usize iteration;
    usize before;
    usize after;
    GuiBufferImportRequest request;
    GuiBufferImportResult result;
    IpcMessage message;

    EXPECT(90, string_from_c_str("/tmp/punos-compositorserv-fd-test",
                                  &endpoint) == CORE_OK);
    EXPECT(91, compositor_service_create(&endpoint, token, &service) == CORE_OK);
    EXPECT(92, memory_shared_create(64, &memory) == CORE_OK);

    request.prefix.protocol_version = GUI_PROTOCOL_VERSION;
    request.width = 4;
    request.height = 4;
    request.stride = 16;
    request.pixel_format = GUI_PIXEL_XRGB8888;
    request.byte_size = 64;

    before = count_open_fds();
    for (iteration = 0; iteration < 40; ++iteration) {
        request.prefix.request_id = 2000 + iteration;
        EXPECT(93, connect_client_pair(&endpoint, NULL, service, &client) == CORE_OK);
        EXPECT(94, gui_encode_buffer_import(&request,
            (Bytes){ payload, sizeof(payload) }, &size) == CORE_OK);
        EXPECT(95, send_message_with_descriptor(client, GUI_SURFACE_BUFFER_IMPORT,
            payload, size, memory.fd) == CORE_OK);
        /* display is disabled for this service, so plain compositor polling is
         * enough; connect_client_pair cannot be used with NULL display here. */
        {
            usize attempt;
            IpcReceiveStatus status = IPC_RECEIVE_NO_MESSAGE;
            for (attempt = 0; attempt < 1000 && status == IPC_RECEIVE_NO_MESSAGE;
                 ++attempt) {
                EXPECT(96, compositor_service_poll(service, 0) == CORE_OK);
                EXPECT(97, ipc_connection_receive(client, &status, &message) == CORE_OK);
            }
            EXPECT(98, status == IPC_RECEIVE_MESSAGE);
        }
        EXPECT(99, gui_decode_buffer_import_result(&message, &result) == CORE_OK &&
                    result.result.error == CORE_OK);
        EXPECT(100, ipc_message_destroy(&message) == CORE_OK);
        EXPECT(101, ipc_connection_close(&client) == CORE_OK);
        EXPECT(102, compositor_service_poll(service, 0) == CORE_OK);
    }
    after = count_open_fds();
    EXPECT(103, before == after);

    EXPECT(104, memory_shared_close(&memory) == CORE_OK);
    EXPECT(105, compositor_service_close(&service) == CORE_OK);
    EXPECT(106, string_destroy(&endpoint) == CORE_OK);
}

int main(void)
{
    test_display_pipeline_and_events();
    /* The strict descriptor count is intentionally separate from the broader
     * runtime cleanup stress test. */
    test_fd_cleanup_strict();
    check_passed();
    return 0;
}
