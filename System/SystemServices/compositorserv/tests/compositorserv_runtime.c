#define CHECK_TEST_NAME "compositorserv"
#include "../../../SystemLibs/testing/check.h"

#include <compositorserv.h>
#include <gui/protocol.h>
#include <special/ipc.h>
#include <sys/memory.h>

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

int main(void)
{
    test_accept_connection();
    test_surface_create_destroy();
    test_buffer_import_destroy();
    test_surface_pending_commit();
    check_passed();
    return 0;
}
