#ifndef PUNOS_SPECIAL_IPC_H
#define PUNOS_SPECIAL_IPC_H

/*
 * ipclib (special/ipc) - local inter-process communication between PUNos
 * components through non-blocking Unix domain sockets.
 *
 * Specification: DevDoc/SpecialLibs/ipclib.md
 *
 * The library creates servers and connections, passes typed messages with
 * frame boundaries and provides file descriptors for registration in
 * sys/eventloop. All operations return ErrorCode; see <sys/core.h> for the
 * conventions.
 */

#include <sys/core.h>
#include <sys/memory.h>
#include <sys/string.h>

#define IPC_ENDPOINT_MAX_LENGTH ((usize)107)
#define IPC_FRAME_HEADER_SIZE ((usize)8)
#define IPC_MESSAGE_MAX_SIZE ((usize)1048576)
#define IPC_OUTGOING_QUEUE_MAX_SIZE ((usize)4194304)
#define IPC_NO_DESCRIPTOR (-1)

typedef struct IpcServerHandle *IpcServerHandle;
typedef struct IpcConnectionHandle *IpcConnectionHandle;

typedef u32 IpcMessageType;

typedef struct IpcMessage {
    IpcMessageType type;
    Bytes payload;
} IpcMessage;

typedef enum IpcConnectionState {
    IPC_CONNECTION_STATE_CONNECTING = 0,
    IPC_CONNECTION_STATE_OPEN = 1,
    IPC_CONNECTION_STATE_PEER_CLOSED = 2
} IpcConnectionState;

typedef enum IpcReceiveStatus {
    IPC_RECEIVE_STATUS_NO_MESSAGE = 0,
    IPC_RECEIVE_STATUS_MESSAGE = 1,
    IPC_RECEIVE_STATUS_PEER_CLOSED = 2
} IpcReceiveStatus;

_Static_assert(sizeof(IpcMessage) == 24, "IpcMessage ABI mismatch");
_Static_assert(sizeof(IpcConnectionState) == 4, "IpcConnectionState ABI mismatch");
_Static_assert(sizeof(IpcReceiveStatus) == 4, "IpcReceiveStatus ABI mismatch");

/*
 * Creates a non-blocking Unix socket server bound to endpoint.
 *
 * Unconditionally removes any existing object at endpoint before binding.
 *
 * Errors: CORE_INVALID_ARGUMENT if endpoint or out_server is NULL;
 * CORE_INVALID_PATH if endpoint is too long for sockaddr_un; errors from
 * core_error_from_errno() for socket/bind/listen failures; errors of libmem.
 */
ErrorCode ipc_server_create(const String *endpoint, IpcServerHandle *out_server);

/*
 * Stores server's non-owning file descriptor in *out_fd.
 *
 * Errors: CORE_INVALID_ARGUMENT if server or out_fd is NULL.
 */
ErrorCode ipc_server_get_fd(IpcServerHandle server, i32 *out_fd);

/*
 * Accepts one pending connection through accept4() with SOCK_NONBLOCK and
 * SOCK_CLOEXEC.
 *
 * Errors: CORE_INVALID_ARGUMENT if server or out_connection is NULL;
 * CORE_WOULD_BLOCK if no connection is pending; errors from
 * core_error_from_errno() for accept4() failures; errors of libmem.
 */
ErrorCode ipc_server_accept(IpcServerHandle server, IpcConnectionHandle *out_connection);

/*
 * Closes server, removes its endpoint and sets *server to NULL.
 *
 * Errors: CORE_INVALID_ARGUMENT if server or *server is NULL; errors from
 * core_error_from_errno() for close/unlink failures.
 */
ErrorCode ipc_server_close(IpcServerHandle *server);

/*
 * Creates a non-blocking client connection in Connecting state.
 *
 * Errors: CORE_INVALID_ARGUMENT if endpoint or out_connection is NULL;
 * CORE_INVALID_PATH if endpoint is too long; errors from
 * core_error_from_errno() for socket/connect failures; errors of libmem.
 */
ErrorCode ipc_connection_begin_connect(const String *endpoint, IpcConnectionHandle *out_connection);

/*
 * Checks connection status through getsockopt(SO_ERROR).
 *
 * Errors: CORE_INVALID_ARGUMENT if connection or out_state is NULL;
 * CORE_INVALID_STATE if connection is not in Connecting state; errors from
 * core_error_from_errno() for getsockopt failures.
 */
ErrorCode ipc_connection_finish_connect(IpcConnectionHandle connection, IpcConnectionState *out_state);

/*
 * Stores connection's non-owning file descriptor in *out_fd.
 *
 * Errors: CORE_INVALID_ARGUMENT if connection or out_fd is NULL.
 */
ErrorCode ipc_connection_get_fd(IpcConnectionHandle connection, i32 *out_fd);

/*
 * Copies message into connection's outgoing queue.
 *
 * Errors: CORE_INVALID_ARGUMENT if connection or message is NULL;
 * CORE_INVALID_STATE if connection is not Open; CORE_OVERFLOW if message
 * exceeds IPC_MESSAGE_MAX_SIZE or queue exceeds IPC_OUTGOING_QUEUE_MAX_SIZE;
 * errors of libmem.
 */
ErrorCode ipc_connection_send(IpcConnectionHandle connection, const IpcMessage *message);

/*
 * Copies one message and one descriptor into the outgoing queue. The library
 * duplicates descriptor with CLOEXEC, so the caller retains ownership of the
 * descriptor passed here. Existing descriptor-free clients keep using
 * ipc_connection_send() and the same IpcMessage ABI.
 */
ErrorCode ipc_connection_send_with_descriptor(IpcConnectionHandle connection,
                                              const IpcMessage *message,
                                              i32 descriptor);

/*
 * Sends as many queued bytes as possible without blocking.
 *
 * Errors: CORE_INVALID_ARGUMENT if connection or out_has_pending is NULL;
 * CORE_INVALID_STATE if connection is not Open; errors from
 * core_error_from_errno() for send failures.
 */
ErrorCode ipc_connection_flush(IpcConnectionHandle connection, bool *out_has_pending);

/*
 * Reads available bytes and assembles at most one complete frame.
 *
 * Errors: CORE_INVALID_ARGUMENT if connection or out_status is NULL;
 * CORE_INVALID_STATE if connection is not Open; errors from
 * core_error_from_errno() for recv failures; errors of libmem.
 */
ErrorCode ipc_connection_receive(IpcConnectionHandle connection, IpcReceiveStatus *out_status, IpcMessage *out_message);

/*
 * Receives one message and optionally transfers ownership of its descriptor
 * to the caller. *out_descriptor is IPC_NO_DESCRIPTOR when the frame did not
 * carry one. The caller must close a returned descriptor.
 */
ErrorCode ipc_connection_receive_with_descriptor(
    IpcConnectionHandle connection, IpcReceiveStatus *out_status,
    IpcMessage *out_message, i32 *out_descriptor);

/*
 * Releases message's payload and sets it to { 0, { NULL, 0 } }.
 *
 * Errors: CORE_INVALID_ARGUMENT if message is NULL; errors of libmem.
 */
ErrorCode ipc_message_destroy(IpcMessage *message);

/*
 * Closes connection, releases internal buffers and sets *connection to NULL.
 *
 * Errors: CORE_INVALID_ARGUMENT if connection or *connection is NULL; errors
 * from core_error_from_errno() for close failures.
 */
ErrorCode ipc_connection_close(IpcConnectionHandle *connection);

#endif
