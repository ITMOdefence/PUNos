/*
 * Runtime test of the ipclib operations against DevDoc/SpecialLibs/ipclib.md.
 *
 * Sockets are created under /tmp, present on the host and inside the PUNos
 * initramfs. The checks are numbered sequentially from 1, so a failure names one
 * assertion: check_fail() exits with the number, which means two assertions
 * sharing a number could not be told apart.
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

/* Endpoint shared by the connect-outcome test, built once in main(). */
static String test_endpoint;

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
    IpcServerHandle server = NULL;

    EXPECT(1, ipc_server_create(NULL, &server) == CORE_INVALID_ARGUMENT);
    EXPECT(2, ipc_server_create(&endpoint, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(3, ipc_server_create(&endpoint, &server) == CORE_OK && server != NULL);

    i32 fd = -1;
    EXPECT(4, ipc_server_get_fd(server, &fd) == CORE_OK && fd >= 0);
    EXPECT(5, ipc_server_get_fd(NULL, &fd) == CORE_INVALID_ARGUMENT);
    EXPECT(6, ipc_server_get_fd(server, NULL) == CORE_INVALID_ARGUMENT);

    EXPECT(7, ipc_server_close(NULL) == CORE_INVALID_ARGUMENT);
    {
        IpcServerHandle null_server = NULL;
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
    IpcServerHandle server = NULL;

    EXPECT(11, ipc_server_create(&endpoint, &server) == CORE_INVALID_PATH);

    destroy_str(&endpoint);
}

static void test_connection_close(void)
{
    String endpoint = make_str(TEST_ENDPOINT);
    IpcServerHandle server = NULL;
    IpcConnectionHandle conn = NULL;

    EXPECT(12, ipc_server_create(&endpoint, &server) == CORE_OK);
    EXPECT(13, ipc_server_accept(server, &conn) == CORE_WOULD_BLOCK);
    EXPECT(14, ipc_connection_close(NULL) == CORE_INVALID_ARGUMENT);
    {
        IpcConnectionHandle null_conn = NULL;
        EXPECT(15, ipc_connection_close(&null_conn) == CORE_INVALID_ARGUMENT);
    }

    EXPECT(16, ipc_server_close(&server) == CORE_OK);
    destroy_str(&endpoint);
}

static void test_connection_state_transitions(void)
{
    String endpoint = make_str(TEST_ENDPOINT_2);
    IpcServerHandle server = NULL;
    IpcConnectionHandle server_conn = NULL;
    IpcConnectionHandle client_conn = NULL;
    IpcConnectionState state;

    EXPECT(17, ipc_server_create(&endpoint, &server) == CORE_OK);
    EXPECT(18, ipc_connection_begin_connect(&endpoint, &client_conn) == CORE_OK);

    EXPECT(19, ipc_connection_finish_connect(client_conn, &state) == CORE_OK);
    EXPECT(20, state == IPC_CONNECTION_STATE_OPEN || state == IPC_CONNECTION_STATE_CONNECTING);

    for (int i = 0; i < 100 && state != IPC_CONNECTION_STATE_OPEN; i++) {
        ipc_connection_finish_connect(client_conn, &state);
    }
    EXPECT(21, state == IPC_CONNECTION_STATE_OPEN);

    EXPECT(22, ipc_server_accept(server, &server_conn) == CORE_OK);
    EXPECT(23, server_conn != NULL);

    i32 client_fd = -1;
    i32 server_fd = -1;
    EXPECT(24, ipc_connection_get_fd(client_conn, &client_fd) == CORE_OK && client_fd >= 0);
    EXPECT(25, ipc_connection_get_fd(server_conn, &server_fd) == CORE_OK && server_fd >= 0);
    EXPECT(26, client_fd != server_fd);

    EXPECT(27, ipc_connection_get_fd(NULL, &client_fd) == CORE_INVALID_ARGUMENT);
    EXPECT(28, ipc_connection_get_fd(client_conn, NULL) == CORE_INVALID_ARGUMENT);

    EXPECT(29, ipc_connection_close(&client_conn) == CORE_OK);
    EXPECT(30, client_conn == NULL);
    EXPECT(31, ipc_connection_close(&server_conn) == CORE_OK);
    EXPECT(32, server_conn == NULL);
    EXPECT(33, ipc_server_close(&server) == CORE_OK);

    destroy_str(&endpoint);
}

static void test_send_receive(void)
{
    String endpoint = make_str(TEST_ENDPOINT);
    IpcServerHandle server = NULL;
    IpcConnectionHandle server_conn = NULL;
    IpcConnectionHandle client_conn = NULL;
    IpcConnectionState state;

    EXPECT(34, ipc_server_create(&endpoint, &server) == CORE_OK);
    EXPECT(35, ipc_connection_begin_connect(&endpoint, &client_conn) == CORE_OK);

    for (int i = 0; i < 100; i++) {
        ipc_connection_finish_connect(client_conn, &state);
        if (state == IPC_CONNECTION_STATE_OPEN) break;
    }
    EXPECT(36, state == IPC_CONNECTION_STATE_OPEN);
    EXPECT(37, ipc_server_accept(server, &server_conn) == CORE_OK);

    u8 payload[] = { 'h', 'e', 'l', 'l', 'o' };
    IpcMessage msg;
    msg.type = 42;
    msg.payload.data = payload;
    msg.payload.size = sizeof(payload);

    EXPECT(38, ipc_connection_send(client_conn, &msg) == CORE_OK);

    bool has_pending = false;
    for (int i = 0; i < 100 && !has_pending; i++) {
        ipc_connection_flush(client_conn, &has_pending);
    }
    EXPECT(39, has_pending == false);

    IpcReceiveStatus recv_status;
    IpcMessage recv_msg;
    recv_msg.payload.data = NULL;
    recv_msg.payload.size = 0;

    for (int i = 0; i < 100; i++) {
        ipc_connection_receive(server_conn, &recv_status, &recv_msg);
        if (recv_status == IPC_RECEIVE_STATUS_MESSAGE) break;
    }
    EXPECT(40, recv_status == IPC_RECEIVE_STATUS_MESSAGE);
    EXPECT(41, recv_msg.type == 42);
    EXPECT(42, recv_msg.payload.size == 5);
    EXPECT(43, recv_msg.payload.data[0] == 'h' && recv_msg.payload.data[4] == 'o');

    EXPECT(44, ipc_message_destroy(&recv_msg) == CORE_OK);
    EXPECT(45, recv_msg.payload.data == NULL && recv_msg.payload.size == 0);

    EXPECT(46, ipc_connection_send(client_conn, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(47, ipc_connection_send(NULL, &msg) == CORE_INVALID_ARGUMENT);
    EXPECT(48, ipc_connection_flush(client_conn, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(49, ipc_connection_flush(NULL, &has_pending) == CORE_INVALID_ARGUMENT);
    EXPECT(50, ipc_connection_receive(server_conn, NULL, &recv_msg) == CORE_INVALID_ARGUMENT);
    EXPECT(51, ipc_connection_receive(NULL, &recv_status, &recv_msg) == CORE_INVALID_ARGUMENT);
    EXPECT(52, ipc_message_destroy(NULL) == CORE_INVALID_ARGUMENT);

    EXPECT(53, ipc_connection_close(&client_conn) == CORE_OK);
    EXPECT(54, ipc_connection_close(&server_conn) == CORE_OK);
    EXPECT(55, ipc_server_close(&server) == CORE_OK);

    destroy_str(&endpoint);
}

static void test_multiple_messages(void)
{
    String endpoint = make_str(TEST_ENDPOINT_2);
    IpcServerHandle server = NULL;
    IpcConnectionHandle server_conn = NULL;
    IpcConnectionHandle client_conn = NULL;
    IpcConnectionState state;

    EXPECT(56, ipc_server_create(&endpoint, &server) == CORE_OK);
    EXPECT(57, ipc_connection_begin_connect(&endpoint, &client_conn) == CORE_OK);

    for (int i = 0; i < 100; i++) {
        ipc_connection_finish_connect(client_conn, &state);
        if (state == IPC_CONNECTION_STATE_OPEN) break;
    }
    EXPECT(58, state == IPC_CONNECTION_STATE_OPEN);
    EXPECT(59, ipc_server_accept(server, &server_conn) == CORE_OK);

    for (u32 i = 0; i < 10; i++) {
        IpcMessage msg;
        msg.type = i;
        msg.payload.data = (u8 *)&i;
        msg.payload.size = sizeof(i);
        EXPECT(60, ipc_connection_send(client_conn, &msg) == CORE_OK);
    }

    bool has_pending = true;
    for (int i = 0; i < 100 && has_pending; i++) {
        ipc_connection_flush(client_conn, &has_pending);
    }
    EXPECT(61, has_pending == false);

    for (u32 expected = 0; expected < 10; expected++) {
        IpcReceiveStatus recv_status;
        IpcMessage recv_msg;
        recv_msg.payload.data = NULL;
        recv_msg.payload.size = 0;

        for (int i = 0; i < 100; i++) {
            ipc_connection_receive(server_conn, &recv_status, &recv_msg);
            if (recv_status == IPC_RECEIVE_STATUS_MESSAGE) break;
        }
        EXPECT(62, recv_status == IPC_RECEIVE_STATUS_MESSAGE);
        EXPECT(63, recv_msg.type == expected);
        EXPECT(64, recv_msg.payload.size == sizeof(u32));

        ipc_message_destroy(&recv_msg);
    }

    EXPECT(65, ipc_connection_close(&client_conn) == CORE_OK);
    EXPECT(66, ipc_connection_close(&server_conn) == CORE_OK);
    EXPECT(67, ipc_server_close(&server) == CORE_OK);

    destroy_str(&endpoint);
}

static void test_send_too_large(void)
{
    String endpoint = make_str(TEST_ENDPOINT);
    IpcServerHandle server = NULL;
    IpcConnectionHandle server_conn = NULL;
    IpcConnectionHandle client_conn = NULL;
    IpcConnectionState state;

    EXPECT(68, ipc_server_create(&endpoint, &server) == CORE_OK);
    EXPECT(69, ipc_connection_begin_connect(&endpoint, &client_conn) == CORE_OK);

    for (int i = 0; i < 100; i++) {
        ipc_connection_finish_connect(client_conn, &state);
        if (state == IPC_CONNECTION_STATE_OPEN) break;
    }
    EXPECT(70, state == IPC_CONNECTION_STATE_OPEN);
    EXPECT(71, ipc_server_accept(server, &server_conn) == CORE_OK);

    MemoryBlock large_block;
    EXPECT(72, memory_allocate(IPC_MESSAGE_MAX_SIZE + 1, &large_block) == CORE_OK);

    IpcMessage msg;
    msg.type = 1;
    msg.payload.data = large_block.data;
    msg.payload.size = large_block.size;

    EXPECT(73, ipc_connection_send(client_conn, &msg) == CORE_OVERFLOW);

    memory_free(&large_block);

    EXPECT(74, ipc_connection_close(&client_conn) == CORE_OK);
    EXPECT(75, ipc_connection_close(&server_conn) == CORE_OK);
    EXPECT(76, ipc_server_close(&server) == CORE_OK);

    destroy_str(&endpoint);
}

/* Issue 13: an empty endpoint is not an endpoint, and one that is too long
 * cannot be represented in sockaddr_un. Both are refused up front. */
static void test_endpoint_validation(void)
{
    IpcServerHandle server = NULL;
    IpcConnectionHandle conn = NULL;

    String empty = make_str("");
    EXPECT(77, ipc_server_create(&empty, &server) == CORE_INVALID_ARGUMENT);
    EXPECT(78, ipc_connection_begin_connect(&empty, &conn) == CORE_INVALID_ARGUMENT);

    String null_endpoint;
    null_endpoint.data = NULL;
    null_endpoint.length = 0;
    EXPECT(79, ipc_server_create(&null_endpoint, &server) == CORE_INVALID_ARGUMENT);
    EXPECT(80, ipc_connection_begin_connect(&null_endpoint, &conn) == CORE_INVALID_ARGUMENT);

    destroy_str(&empty);
}

/* Issue 8: a final connect error has to surface through ErrorCode instead of
 * being reported as "still connecting", and it has to keep surfacing: reading
 * SO_ERROR clears the kernel's copy, so a second call would otherwise see 0
 * and wrongly report the socket as Open. */
static void test_connect_failure(void)
{
    /* Nothing is bound here, so connect() fails immediately with ENOENT or
     * ECONNREFUSED rather than staying in progress. */
    String endpoint = make_str("/tmp/ipc_absent_endpoint.sock");
    IpcConnectionHandle conn = NULL;
    IpcConnectionState state = IPC_CONNECTION_STATE_OPEN;
    ErrorCode first;

    if (ipc_connection_begin_connect(&endpoint, &conn) != CORE_OK) {
        destroy_str(&endpoint);
        return;
    }

    first = ipc_connection_finish_connect(conn, &state);
    if (first == CORE_OK) {
        /* Still in progress on this kernel: not a failure case. */
        EXPECT(81, state == IPC_CONNECTION_STATE_CONNECTING);
    } else {
        /* A final error, reported as such and not as a success. */
        EXPECT(82, state != IPC_CONNECTION_STATE_OPEN);
        EXPECT(83, first == CORE_NOT_FOUND || first == CORE_IO_ERROR);

        /* Reading SO_ERROR again must not turn a dead socket into Open. */
        state = IPC_CONNECTION_STATE_OPEN;
        EXPECT(84, ipc_connection_finish_connect(conn, &state) == first);
        EXPECT(85, state != IPC_CONNECTION_STATE_OPEN);
    }

    EXPECT(86, ipc_connection_close(&conn) == CORE_OK);
    destroy_str(&endpoint);
}

/* --- driven through tests/socket_fake.c ---------------------------------- */

/* Control surface of the fake. */
void fake_socket_arm(i32 error_to_report);
void fake_socket_reset(void);
bool fake_socket_error_pending(void);

/* Opens a client connection against a real listening server, so only the
 * reported connect outcome is faked. */
static IpcConnectionHandle connect_to_live_server(IpcServerHandle server)
{
    IpcConnectionHandle conn = NULL;
    IpcConnectionHandle accepted = NULL;
    i32 server_fd = -1;

    if (server == NULL || ipc_server_get_fd(server, &server_fd) != CORE_OK) {
        return NULL;
    }
    (void)ipc_connection_begin_connect(&test_endpoint, &conn);
    if (conn == NULL) {
        return NULL;
    }
    /* Drain the accept queue so the connect really completes on the server. */
    (void)ipc_server_accept(server, &accepted);
    if (accepted != NULL) {
        (void)ipc_connection_close(&accepted);
    }
    return conn;
}

/*
 * Issue 8: ipc_connection_finish_connect() treated every non-zero SO_ERROR as
 * "still connecting" and never reported a failure, and because reading SO_ERROR
 * clears the kernel's copy, a second call could see 0 and declare a socket that
 * never connected to be Open.
 */
static void test_connect_outcomes(void)
{
    IpcServerHandle server = NULL;
    IpcConnectionHandle conn;
    IpcConnectionState state;

    if (ipc_server_create(&test_endpoint, &server) != CORE_OK) {
        return;
    }

    /* 1. The kernel has not finished: still Connecting, and that is not an
     *    error. */
    conn = connect_to_live_server(server);
    if (conn != NULL) {
        fake_socket_arm(EINPROGRESS);
        state = IPC_CONNECTION_STATE_OPEN;
        EXPECT(87, ipc_connection_finish_connect(conn, &state) == CORE_OK);
        EXPECT(88, state == IPC_CONNECTION_STATE_CONNECTING);

        /* 2. Then it succeeds: the connection opens. */
        fake_socket_arm(0);
        EXPECT(89, ipc_connection_finish_connect(conn, &state) == CORE_OK);
        EXPECT(90, state == IPC_CONNECTION_STATE_OPEN);
        EXPECT(91, ipc_connection_close(&conn) == CORE_OK);
    }
    fake_socket_reset();

    /* 3. A final failure has to surface as an ErrorCode. out_state is only
     *    defined on CORE_OK, so the connection is probed through send() to see
     *    whether it was declared Open: a failed connect stays in Connecting
     *    and refuses to send. */
    conn = connect_to_live_server(server);
    if (conn != NULL) {
        IpcMessage message;

        message.type = 1;
        message.payload.data = NULL;
        message.payload.size = 0;

        fake_socket_arm(ECONNREFUSED);
        state = IPC_CONNECTION_STATE_CONNECTING;
        EXPECT(92, ipc_connection_finish_connect(conn, &state) == CORE_IO_ERROR);
        EXPECT(93, ipc_connection_send(conn, &message) == CORE_INVALID_STATE);

        /* 4. The kernel cleared the error when it was read, so the old code
         *    came back here, saw 0 and reported Open - and then the message
         *    above would have gone through. The failure has to repeat. */
        EXPECT(94, !fake_socket_error_pending());
        state = IPC_CONNECTION_STATE_CONNECTING;
        EXPECT(95, ipc_connection_finish_connect(conn, &state) == CORE_IO_ERROR);
        EXPECT(96, ipc_connection_send(conn, &message) == CORE_INVALID_STATE);
        EXPECT(97, ipc_connection_close(&conn) == CORE_OK);
    }
    fake_socket_reset();

    /* 5. ENOENT, the other error worth distinguishing, maps to CORE_NOT_FOUND. */
    conn = connect_to_live_server(server);
    if (conn != NULL) {
        fake_socket_arm(ENOENT);
        state = IPC_CONNECTION_STATE_CONNECTING;
        EXPECT(98, ipc_connection_finish_connect(conn, &state) == CORE_NOT_FOUND);
        EXPECT(99, ipc_connection_close(&conn) == CORE_OK);
    }
    fake_socket_reset();

    /* 6. A healthy connection still opens with no faking at all. */
    conn = connect_to_live_server(server);
    if (conn != NULL) {
        state = IPC_CONNECTION_STATE_CONNECTING;
        EXPECT(100, ipc_connection_finish_connect(conn, &state) == CORE_OK);
        EXPECT(101, state == IPC_CONNECTION_STATE_OPEN);
        EXPECT(102, ipc_connection_close(&conn) == CORE_OK);
    }

    EXPECT(103, ipc_server_close(&server) == CORE_OK);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    test_endpoint = make_str(TEST_ENDPOINT);

    test_server_create_close();
    test_server_endpoint_too_long();
    test_connection_close();
    test_connection_state_transitions();
    test_send_receive();
    test_multiple_messages();
    test_send_too_large();
    test_endpoint_validation();
    test_connect_failure();
    test_connect_outcomes();

    destroy_str(&test_endpoint);

    check_passed();
    return 0;
}
