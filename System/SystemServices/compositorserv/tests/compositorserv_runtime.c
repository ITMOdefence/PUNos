#define CHECK_TEST_NAME "compositorserv"
#include "../../../SystemLibs/testing/check.h"

#include <compositorserv.h>
#include <gui/protocol.h>
#include <special/ipc.h>

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

static ErrorCode send_message(IpcConnectionHandle client, IpcMessageType type,
                              u8 *payload, usize size)
{
    IpcMessage message = { type, 0, { payload, size }, IPC_NO_DESCRIPTOR, 0 };
    bool pending;
    ErrorCode result = ipc_connection_send(client, &message);

    if (result != CORE_OK) return result;
    return ipc_connection_flush(client, &pending);
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

int main(void)
{
    test_accept_connection();
    test_surface_create_destroy();
    check_passed();
    return 0;
}
