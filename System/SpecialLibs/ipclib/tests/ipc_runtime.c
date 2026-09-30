/*
 * Runtime test of the ipclib operations against DevDoc/SpecialLibs/ipclib.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 * Sockets are created under /tmp, present on the host and inside the PUNos
 * initramfs.
 *
 * Check number groups:
 *   1-10   ipc_server_create / get_fd / close
 *   11-19  endpoint longer than sockaddr_un can hold
 *   20-23  accept with no pending connection, connection_close on NULL
 *   30-46  begin_connect, finish_connect, get_fd, close
 *   50-71  send, flush, receive, ipc_message_destroy
 *   80-91  ten messages queued at once, read back one frame per call
 *   100-108 payload above IPC_MESSAGE_MAX_SIZE
 */

#define CHECK_TEST_NAME "ipclib"
#include <check.h>

#include <punos/syscall/syscall.h>
#include <punos/syscall/constants.h>
#include <special/ipc.h>
#include <sys/memory.h>
#include <sys/string.h>

#define TEST_ENDPOINT "/tmp/ipc_test.sock"
#define TEST_ENDPOINT_2 "/tmp/ipc_test2.sock"

static String make_str(const char *text)
{
    String s;
    if (string_from_c_str(text, &s) != CORE_OK) {
        check_fail(250);
    }
    return s;
}

static void destroy_str(String *s)
{
    (void)string_destroy(s);
}

static void test_server_create_close(void)
{
    String endpoint = make_str(TEST_ENDPOINT);
    IpcServerHandle *server = NULL;

    EXPECT(1, ipc_server_create(NULL, &server) == CORE_INVALID_ARGUMENT);
    EXPECT(2, ipc_server_create(&endpoint, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(3, ipc_server_create(&endpoint, &server) == CORE_OK && server != NULL);

    i32 fd = -1;
    EXPECT(4, ipc_server_get_fd(server, &fd) == CORE_OK && fd >= 0);
    EXPECT(5, ipc_server_get_fd(NULL, &fd) == CORE_INVALID_ARGUMENT);
    EXPECT(6, ipc_server_get_fd(server, NULL) == CORE_INVALID_ARGUMENT);

    EXPECT(7, ipc_server_close(NULL) == CORE_INVALID_ARGUMENT);
    {
        IpcServerHandle *null_server = NULL;
        EXPECT(8, ipc_server_close(&null_server) == CORE_INVALID_ARGUMENT);
    }
    EXPECT(9, ipc_server_close(&server) == CORE_OK);
    EXPECT(10, server == NULL);

    destroy_str(&endpoint);
}

static void test_server_endpoint_too_long(void)
{
    char long_path[200];
    for (int i = 0; i < 199; i++) {
        long_path[i] = 'a';
    }
    long_path[199] = '\0';

    String endpoint = make_str(long_path);
    IpcServerHandle *server = NULL;

    EXPECT(11, ipc_server_create(&endpoint, &server) == CORE_INVALID_PATH);

    destroy_str(&endpoint);
}

static void test_connection_close(void)
{
    String endpoint = make_str(TEST_ENDPOINT);
    IpcServerHandle *server = NULL;
    IpcConnectionHandle *conn = NULL;

    EXPECT(20, ipc_server_create(&endpoint, &server) == CORE_OK);
    EXPECT(21, ipc_server_accept(server, &conn) == CORE_WOULD_BLOCK);
    EXPECT(22, ipc_connection_close(NULL) == CORE_INVALID_ARGUMENT);
    {
        IpcConnectionHandle *null_conn = NULL;
        EXPECT(23, ipc_connection_close(&null_conn) == CORE_INVALID_ARGUMENT);
    }

    EXPECT(24, ipc_server_close(&server) == CORE_OK);
    destroy_str(&endpoint);
}

static void test_connection_state_transitions(void)
{
    String endpoint = make_str(TEST_ENDPOINT_2);
    IpcServerHandle *server = NULL;
    IpcConnectionHandle *server_conn = NULL;
    IpcConnectionHandle *client_conn = NULL;
    IpcConnectionState state;

    EXPECT(30, ipc_server_create(&endpoint, &server) == CORE_OK);
    EXPECT(31, ipc_connection_begin_connect(&endpoint, &client_conn) == CORE_OK);

    EXPECT(32, ipc_connection_finish_connect(client_conn, &state) == CORE_OK);
    EXPECT(33, state == IPC_CONNECTION_STATE_OPEN || state == IPC_CONNECTION_STATE_CONNECTING);

    for (int i = 0; i < 100 && state != IPC_CONNECTION_STATE_OPEN; i++) {
        ipc_connection_finish_connect(client_conn, &state);
    }
    EXPECT(34, state == IPC_CONNECTION_STATE_OPEN);

    EXPECT(35, ipc_server_accept(server, &server_conn) == CORE_OK);
    EXPECT(36, server_conn != NULL);

    i32 client_fd = -1;
    i32 server_fd = -1;
    EXPECT(37, ipc_connection_get_fd(client_conn, &client_fd) == CORE_OK && client_fd >= 0);
    EXPECT(38, ipc_connection_get_fd(server_conn, &server_fd) == CORE_OK && server_fd >= 0);
    EXPECT(39, client_fd != server_fd);

    EXPECT(40, ipc_connection_get_fd(NULL, &client_fd) == CORE_INVALID_ARGUMENT);
    EXPECT(41, ipc_connection_get_fd(client_conn, NULL) == CORE_INVALID_ARGUMENT);

    EXPECT(42, ipc_connection_close(&client_conn) == CORE_OK);
    EXPECT(43, client_conn == NULL);
    EXPECT(44, ipc_connection_close(&server_conn) == CORE_OK);
    EXPECT(45, server_conn == NULL);
    EXPECT(46, ipc_server_close(&server) == CORE_OK);

    destroy_str(&endpoint);
}

static void test_send_receive(void)
{
    String endpoint = make_str(TEST_ENDPOINT);
    IpcServerHandle *server = NULL;
    IpcConnectionHandle *server_conn = NULL;
    IpcConnectionHandle *client_conn = NULL;
    IpcConnectionState state;

    EXPECT(50, ipc_server_create(&endpoint, &server) == CORE_OK);
    EXPECT(51, ipc_connection_begin_connect(&endpoint, &client_conn) == CORE_OK);

    for (int i = 0; i < 100; i++) {
        ipc_connection_finish_connect(client_conn, &state);
        if (state == IPC_CONNECTION_STATE_OPEN) break;
    }
    EXPECT(52, state == IPC_CONNECTION_STATE_OPEN);
    EXPECT(53, ipc_server_accept(server, &server_conn) == CORE_OK);

    u8 payload[] = { 'h', 'e', 'l', 'l', 'o' };
    IpcMessage msg;
    msg.type = 42;
    msg.payload.data = payload;
    msg.payload.size = sizeof(payload);

    EXPECT(54, ipc_connection_send(client_conn, &msg) == CORE_OK);

    bool has_pending = false;
    for (int i = 0; i < 100 && !has_pending; i++) {
        ipc_connection_flush(client_conn, &has_pending);
    }
    EXPECT(55, has_pending == false);

    IpcReceiveStatus recv_status;
    IpcMessage recv_msg;
    recv_msg.payload.data = NULL;
    recv_msg.payload.size = 0;

    for (int i = 0; i < 100; i++) {
        ipc_connection_receive(server_conn, &recv_status, &recv_msg);
        if (recv_status == IPC_RECEIVE_STATUS_MESSAGE) break;
    }
    EXPECT(56, recv_status == IPC_RECEIVE_STATUS_MESSAGE);
    EXPECT(57, recv_msg.type == 42);
    EXPECT(58, recv_msg.payload.size == 5);
    EXPECT(59, recv_msg.payload.data[0] == 'h' && recv_msg.payload.data[4] == 'o');

    EXPECT(60, ipc_message_destroy(&recv_msg) == CORE_OK);
    EXPECT(61, recv_msg.payload.data == NULL && recv_msg.payload.size == 0);

    EXPECT(62, ipc_connection_send(client_conn, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(63, ipc_connection_send(NULL, &msg) == CORE_INVALID_ARGUMENT);
    EXPECT(64, ipc_connection_flush(client_conn, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(65, ipc_connection_flush(NULL, &has_pending) == CORE_INVALID_ARGUMENT);
    EXPECT(66, ipc_connection_receive(server_conn, NULL, &recv_msg) == CORE_INVALID_ARGUMENT);
    EXPECT(67, ipc_connection_receive(NULL, &recv_status, &recv_msg) == CORE_INVALID_ARGUMENT);
    EXPECT(68, ipc_message_destroy(NULL) == CORE_INVALID_ARGUMENT);

    EXPECT(69, ipc_connection_close(&client_conn) == CORE_OK);
    EXPECT(70, ipc_connection_close(&server_conn) == CORE_OK);
    EXPECT(71, ipc_server_close(&server) == CORE_OK);

    destroy_str(&endpoint);
}

static void test_multiple_messages(void)
{
    String endpoint = make_str(TEST_ENDPOINT_2);
    IpcServerHandle *server = NULL;
    IpcConnectionHandle *server_conn = NULL;
    IpcConnectionHandle *client_conn = NULL;
    IpcConnectionState state;

    EXPECT(80, ipc_server_create(&endpoint, &server) == CORE_OK);
    EXPECT(81, ipc_connection_begin_connect(&endpoint, &client_conn) == CORE_OK);

    for (int i = 0; i < 100; i++) {
        ipc_connection_finish_connect(client_conn, &state);
        if (state == IPC_CONNECTION_STATE_OPEN) break;
    }
    EXPECT(82, state == IPC_CONNECTION_STATE_OPEN);
    EXPECT(83, ipc_server_accept(server, &server_conn) == CORE_OK);

    for (u32 i = 0; i < 10; i++) {
        IpcMessage msg;
        msg.type = i;
        msg.payload.data = (u8 *)&i;
        msg.payload.size = sizeof(i);
        EXPECT(84, ipc_connection_send(client_conn, &msg) == CORE_OK);
    }

    bool has_pending = true;
    for (int i = 0; i < 100 && has_pending; i++) {
        ipc_connection_flush(client_conn, &has_pending);
    }
    EXPECT(85, has_pending == false);

    for (u32 expected = 0; expected < 10; expected++) {
        IpcReceiveStatus recv_status;
        IpcMessage recv_msg;
        recv_msg.payload.data = NULL;
        recv_msg.payload.size = 0;

        for (int i = 0; i < 100; i++) {
            ipc_connection_receive(server_conn, &recv_status, &recv_msg);
            if (recv_status == IPC_RECEIVE_STATUS_MESSAGE) break;
        }
        EXPECT(86, recv_status == IPC_RECEIVE_STATUS_MESSAGE);
        EXPECT(87, recv_msg.type == expected);
        EXPECT(88, recv_msg.payload.size == sizeof(u32));

        ipc_message_destroy(&recv_msg);
    }

    EXPECT(89, ipc_connection_close(&client_conn) == CORE_OK);
    EXPECT(90, ipc_connection_close(&server_conn) == CORE_OK);
    EXPECT(91, ipc_server_close(&server) == CORE_OK);

    destroy_str(&endpoint);
}

static void test_send_too_large(void)
{
    String endpoint = make_str(TEST_ENDPOINT);
    IpcServerHandle *server = NULL;
    IpcConnectionHandle *server_conn = NULL;
    IpcConnectionHandle *client_conn = NULL;
    IpcConnectionState state;

    EXPECT(100, ipc_server_create(&endpoint, &server) == CORE_OK);
    EXPECT(101, ipc_connection_begin_connect(&endpoint, &client_conn) == CORE_OK);

    for (int i = 0; i < 100; i++) {
        ipc_connection_finish_connect(client_conn, &state);
        if (state == IPC_CONNECTION_STATE_OPEN) break;
    }
    EXPECT(102, state == IPC_CONNECTION_STATE_OPEN);
    EXPECT(103, ipc_server_accept(server, &server_conn) == CORE_OK);

    MemoryBlock large_block;
    EXPECT(104, memory_allocate(IPC_MESSAGE_MAX_SIZE + 1, &large_block) == CORE_OK);

    IpcMessage msg;
    msg.type = 1;
    msg.payload.data = large_block.data;
    msg.payload.size = large_block.size;

    EXPECT(105, ipc_connection_send(client_conn, &msg) == CORE_OVERFLOW);

    memory_free(&large_block);

    EXPECT(106, ipc_connection_close(&client_conn) == CORE_OK);
    EXPECT(107, ipc_connection_close(&server_conn) == CORE_OK);
    EXPECT(108, ipc_server_close(&server) == CORE_OK);

    destroy_str(&endpoint);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    test_server_create_close();
    test_server_endpoint_too_long();
    test_connection_close();
    test_connection_state_transitions();
    test_send_receive();
    test_multiple_messages();
    test_send_too_large();

    check_passed();
    return 0;
}
