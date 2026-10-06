#define CHECK_TEST_NAME "ipclib"
#include "../../../SystemLibs/testing/check.h"

#include <special/ipc.h>
#include <sys/memory.h>

static void test_round_trip(void)
{
    static const char path[] = "/tmp/punos-ipclib-test";
    u8 payload_data[3] = { 7, 8, 9 };
    String endpoint;
    IpcServerHandle server;
    IpcConnectionHandle client;
    IpcConnectionHandle peer = NULL;
    IpcConnectionState state;
    SharedMemory shared;
    SharedMemoryMapping imported;
    IpcMessage outgoing = { 42, 0, { payload_data, sizeof(payload_data) },
                            IPC_NO_DESCRIPTOR, 0 };
    IpcMessage incoming;
    IpcReceiveStatus receive_status = IPC_RECEIVE_NO_MESSAGE;
    bool pending = true;
    usize attempt;

    EXPECT(1, string_from_c_str(path, &endpoint) == CORE_OK);
    EXPECT(2, ipc_server_create(&endpoint, &server) == CORE_OK);
    EXPECT(3, ipc_connection_begin_connect(&endpoint, &client) == CORE_OK);

    for (attempt = 0; attempt < 10000 && peer == NULL; ++attempt) {
        ErrorCode result = ipc_server_accept(server, &peer);
        EXPECT(4, result == CORE_OK || result == CORE_WOULD_BLOCK);
    }
    EXPECT(5, peer != NULL);
    state = IPC_CONNECTION_CONNECTING;
    for (attempt = 0; attempt < 10000 && state == IPC_CONNECTION_CONNECTING;
         ++attempt) {
        EXPECT(6, ipc_connection_finish_connect(client, &state) == CORE_OK);
    }
    EXPECT(7, state == IPC_CONNECTION_OPEN);

    EXPECT(8, memory_shared_create(4096, &shared) == CORE_OK);
    shared.data[0] = 0xa5;
    outgoing.descriptor = shared.fd;
    EXPECT(9, ipc_connection_send(client, &outgoing) == CORE_OK);
    for (attempt = 0; attempt < 10000 && pending; ++attempt) {
        EXPECT(10, ipc_connection_flush(client, &pending) == CORE_OK);
    }
    EXPECT(11, !pending);

    for (attempt = 0;
         attempt < 10000 && receive_status == IPC_RECEIVE_NO_MESSAGE;
         ++attempt) {
        EXPECT(12, ipc_connection_receive(peer, &receive_status, &incoming) ==
                   CORE_OK);
    }
    EXPECT(13, receive_status == IPC_RECEIVE_MESSAGE);
    EXPECT(14, incoming.type == 42 && incoming.payload.size == 3);
    EXPECT(15, incoming.payload.data[0] == 7 && incoming.payload.data[1] == 8 &&
               incoming.payload.data[2] == 9 && incoming.descriptor >= 0);
    EXPECT(16, memory_shared_map_readonly(incoming.descriptor, 4096,
                                          &imported) == CORE_OK);
    EXPECT(17, imported.data[0] == 0xa5);
    EXPECT(18, memory_shared_unmap(&imported) == CORE_OK);
    EXPECT(19, ipc_message_destroy(&incoming) == CORE_OK);
    EXPECT(20, memory_shared_close(&shared) == CORE_OK);
    EXPECT(21, ipc_connection_close(&peer) == CORE_OK);
    EXPECT(22, ipc_connection_close(&client) == CORE_OK);
    EXPECT(23, ipc_server_close(&server) == CORE_OK);
    EXPECT(24, string_destroy(&endpoint) == CORE_OK);
}

void main(void)
{
    test_round_trip();
    check_passed();
}
