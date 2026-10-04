/* 1-12: server/connect/state; 13-28: copy/framing/fragmentation;
 * 29-37: limits/backpressure; 38-54: peer closure, malformed frames/paths. */
#define CHECK_TEST_NAME "ipclib"
#include "../../../SystemLibs/testing/check.h"
#include <special/ipc.h>
#include <sys/memory.h>

int main(void)
{
    String endpoint;
    IpcServerHandle server = NULL;
    IpcConnectionHandle client = NULL, peer = NULL;
    IpcConnectionState state;
    IpcReceiveStatus status;
    IpcMessage received;
    bool pending;
    i32 fd;
    u8 payload[] = {1, 2, 3, 4}, header[] = {4, 0, 0, 0, 9, 0, 0, 0};
    /* PID-qualified paths also permit concurrent host checks. */
    char path[] = "/tmp/punos-ipc-0000000000.sock";
    u32 pid = (u32)getpid();
    for (usize i = 0; i < 10; ++i) { path[23 - i] = (char)('0' + pid % 10); pid /= 10; }
    EXPECT(1, string_from_c_str(path, &endpoint) == CORE_OK);
    EXPECT(2, ipc_server_create(NULL, &server) == CORE_INVALID_ARGUMENT);
    EXPECT(3, ipc_server_create(&endpoint, &server) == CORE_OK);
    EXPECT(4, ipc_server_get_fd(server, &fd) == CORE_OK && fd >= 0);
    EXPECT(5, ipc_server_accept(server, &peer) == CORE_WOULD_BLOCK);
    EXPECT(6, ipc_connection_begin_connect(&endpoint, &client) == CORE_OK);
    EXPECT(7, ipc_connection_send(client, &(IpcMessage){0, {NULL, 0}}) == CORE_INVALID_STATE);
    EXPECT(8, ipc_connection_finish_connect(client, &state) == CORE_OK && state == IPC_CONNECTION_STATE_OPEN);
    EXPECT(9, ipc_server_accept(server, &peer) == CORE_OK);
    EXPECT(10, ipc_connection_finish_connect(client, &state) == CORE_INVALID_STATE);
    EXPECT(11, ipc_connection_get_fd(peer, &fd) == CORE_OK);
    EXPECT(12, ipc_connection_receive(peer, &status, &received) == CORE_OK && status == IPC_RECEIVE_STATUS_NO_MESSAGE);
    EXPECT(13, ipc_connection_send(client, &(IpcMessage){0x12345678, {payload, 4}}) == CORE_OK);
    payload[0] = 99;
    EXPECT(14, ipc_connection_receive(peer, &status, &received) == CORE_OK && status == IPC_RECEIVE_STATUS_NO_MESSAGE);
    EXPECT(15, ipc_connection_send(client, &(IpcMessage){7, {NULL, 0}}) == CORE_OK);
    EXPECT(16, ipc_connection_flush(client, &pending) == CORE_OK && !pending);
    EXPECT(17, ipc_connection_receive(peer, &status, &received) == CORE_OK && status == IPC_RECEIVE_STATUS_MESSAGE && received.type == 0x12345678 && received.payload.size == 4 && received.payload.data[0] == 1);
    EXPECT(18, ipc_message_destroy(&received) == CORE_OK && received.payload.data == NULL);
    EXPECT(19, ipc_connection_receive(peer, &status, &received) == CORE_OK && status == IPC_RECEIVE_STATUS_MESSAGE && received.type == 7 && received.payload.size == 0);
    EXPECT(20, ipc_message_destroy(&received) == CORE_OK);
    EXPECT(21, send(fd, header, 3, MSG_NOSIGNAL) == 3);
    EXPECT(22, ipc_connection_receive(client, &status, &received) == CORE_OK && status == IPC_RECEIVE_STATUS_NO_MESSAGE);
    EXPECT(23, send(fd, header + 3, 5, MSG_NOSIGNAL) == 5 && send(fd, payload, 2, MSG_NOSIGNAL) == 2);
    EXPECT(24, ipc_connection_receive(client, &status, &received) == CORE_OK && status == IPC_RECEIVE_STATUS_NO_MESSAGE);
    EXPECT(25, send(fd, payload + 2, 2, MSG_NOSIGNAL) == 2);
    EXPECT(26, ipc_connection_receive(client, &status, &received) == CORE_OK && status == IPC_RECEIVE_STATUS_MESSAGE && received.type == 9 && received.payload.size == 4 && received.payload.data[0] == 99);
    EXPECT(27, ipc_message_destroy(&received) == CORE_OK);
    EXPECT(28, ipc_connection_send(client, &(IpcMessage){1, {NULL, 2}}) == CORE_INVALID_ARGUMENT);
    MemoryBlock large;
    EXPECT(29, memory_allocate(IPC_MESSAGE_MAX_SIZE, &large) == CORE_OK);
    EXPECT(30, ipc_connection_send(client, &(IpcMessage){1, {large.data, IPC_MESSAGE_MAX_SIZE + 1}}) == CORE_OVERFLOW);
    for (usize i = 0; i < 3; ++i)
        EXPECT(31, ipc_connection_send(client, &(IpcMessage){1, {large.data, large.size}}) == CORE_OK);
    EXPECT(32, ipc_connection_send(client, &(IpcMessage){1, {large.data, large.size}}) == CORE_OVERFLOW);
    EXPECT(33, ipc_connection_flush(client, &pending) == CORE_OK && pending);
    usize messages = 0;
    for (usize attempts = 0; attempts < 10000 && (pending || messages < 3); ++attempts) {
        EXPECT(34, ipc_connection_receive(peer, &status, &received) == CORE_OK);
        if (status == IPC_RECEIVE_STATUS_MESSAGE) {
            EXPECT(35, received.payload.size == IPC_MESSAGE_MAX_SIZE && received.payload.data[large.size - 1] == 0);
            ++messages; (void)ipc_message_destroy(&received);
        }
        EXPECT(36, ipc_connection_flush(client, &pending) == CORE_OK);
    }
    EXPECT(37, messages == 3 && !pending && memory_free(&large) == CORE_OK);
    EXPECT(38, ipc_connection_close(&peer) == CORE_OK);
    EXPECT(39, ipc_connection_receive(client, &status, &received) == CORE_OK && status == IPC_RECEIVE_STATUS_PEER_CLOSED);
    EXPECT(40, ipc_connection_get_fd(client, &fd) == CORE_INVALID_STATE);
    EXPECT(41, ipc_connection_close(&client) == CORE_OK);
    EXPECT(42, ipc_connection_begin_connect(&endpoint, &client) == CORE_OK && ipc_connection_finish_connect(client, &state) == CORE_OK && ipc_server_accept(server, &peer) == CORE_OK);
    EXPECT(43, ipc_connection_get_fd(peer, &fd) == CORE_OK);
    u8 bad[] = {1, 0, 16, 0, 0, 0, 0, 0}; /* max + 1 */
    EXPECT(44, send(fd, bad, sizeof(bad), MSG_NOSIGNAL) == (ssize_t)sizeof(bad));
    EXPECT(45, ipc_connection_receive(client, &status, &received) == CORE_IO_ERROR);
    EXPECT(46, ipc_connection_close(&client) == CORE_OK && ipc_connection_close(&peer) == CORE_OK && ipc_server_close(&server) == CORE_OK && server == NULL);
    EXPECT(47, ipc_connection_begin_connect(&endpoint, &client) == CORE_NOT_FOUND && string_destroy(&endpoint) == CORE_OK);
    EXPECT(48, string_from_c_str(path, &endpoint) == CORE_OK && ipc_server_create(&endpoint, &server) == CORE_OK && ipc_connection_begin_connect(&endpoint, &client) == CORE_OK && ipc_connection_finish_connect(client, &state) == CORE_OK && ipc_server_accept(server, &peer) == CORE_OK);
    EXPECT(49, ipc_connection_get_fd(peer, &fd) == CORE_OK && send(fd, header, 3, MSG_NOSIGNAL) == 3 && ipc_connection_close(&peer) == CORE_OK);
    EXPECT(50, ipc_connection_receive(client, &status, &received) == CORE_IO_ERROR);
    EXPECT(51, ipc_connection_receive(client, &status, &received) == CORE_INVALID_STATE);
    EXPECT(52, ipc_connection_close(&client) == CORE_OK && ipc_server_close(&server) == CORE_OK);
    char too_long[109];
    for (usize i = 0; i < 108; ++i) too_long[i] = 'a';
    too_long[108] = 0;
    String long_endpoint;
    EXPECT(53, string_from_c_str(too_long, &long_endpoint) == CORE_OK && ipc_server_create(&long_endpoint, &server) == CORE_INVALID_PATH);
    EXPECT(54, string_destroy(&long_endpoint) == CORE_OK && string_destroy(&endpoint) == CORE_OK);
    check_passed(); return 0;
}
