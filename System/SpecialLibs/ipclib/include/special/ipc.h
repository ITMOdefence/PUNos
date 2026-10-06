#ifndef PUNOS_SPECIAL_IPC_H
#define PUNOS_SPECIAL_IPC_H

#include <sys/core.h>
#include <sys/string.h>

#define IPC_ENDPOINT_MAX_LENGTH ((usize)107)
#define IPC_FRAME_HEADER_SIZE ((usize)12)
#define IPC_MESSAGE_MAX_SIZE ((usize)1048576)
#define IPC_OUTGOING_QUEUE_MAX_SIZE ((usize)4194304)
#define IPC_NO_DESCRIPTOR (-1)

typedef String IpcEndpoint;
typedef struct IpcServerHandle *IpcServerHandle;
typedef struct IpcConnectionHandle *IpcConnectionHandle;
typedef u32 IpcMessageType;

typedef struct IpcMessage {
    IpcMessageType type;
    u32 reserved;
    Bytes payload;
    i32 descriptor;
    u32 descriptor_reserved;
} IpcMessage;

typedef enum IpcConnectionState {
    IPC_CONNECTION_CONNECTING = 0,
    IPC_CONNECTION_OPEN = 1,
    IPC_CONNECTION_PEER_CLOSED = 2
} IpcConnectionState;

typedef enum IpcReceiveStatus {
    IPC_RECEIVE_NO_MESSAGE = 0,
    IPC_RECEIVE_MESSAGE = 1,
    IPC_RECEIVE_PEER_CLOSED = 2
} IpcReceiveStatus;

ErrorCode ipc_server_create(const IpcEndpoint *endpoint,
                            IpcServerHandle *out_server);
ErrorCode ipc_server_get_fd(IpcServerHandle server, i32 *out_fd);
ErrorCode ipc_server_accept(IpcServerHandle server,
                            IpcConnectionHandle *out_connection);
ErrorCode ipc_server_close(IpcServerHandle *server);

ErrorCode ipc_connection_begin_connect(const IpcEndpoint *endpoint,
                                       IpcConnectionHandle *out_connection);
ErrorCode ipc_connection_finish_connect(IpcConnectionHandle connection,
                                        IpcConnectionState *out_state);
ErrorCode ipc_connection_get_fd(IpcConnectionHandle connection, i32 *out_fd);
ErrorCode ipc_connection_send(IpcConnectionHandle connection,
                              const IpcMessage *message);
ErrorCode ipc_connection_flush(IpcConnectionHandle connection,
                               bool *out_has_pending);
ErrorCode ipc_connection_receive(IpcConnectionHandle connection,
                                 IpcReceiveStatus *out_status,
                                 IpcMessage *out_message);
ErrorCode ipc_message_destroy(IpcMessage *message);
ErrorCode ipc_connection_close(IpcConnectionHandle *connection);

#endif
