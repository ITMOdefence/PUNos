#include <special/ipc.h>

#include <punos/syscall/syscall.h>
#include <punos/syscall/constants.h>
#include <sys/memory.h>
#include <sys/string.h>

struct IpcServerHandle {
    i32 fd;
    char endpoint_path[IPC_ENDPOINT_MAX_LENGTH + 1];
    usize endpoint_length;
};

struct IpcConnectionHandle {
    i32 fd;
    IpcConnectionState state;
    u8 *outgoing_buffer;
    usize outgoing_size;
    usize outgoing_capacity;
    u8 *incoming_buffer;
    usize incoming_size;
    usize incoming_capacity;
};

static ErrorCode ipc_create_socket(i32 *out_fd)
{
    i32 fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        i32 err = errno;
        return core_error_from_errno(err);
    }
    *out_fd = fd;
    return CORE_OK;
}

static ErrorCode ipc_unlink_endpoint(const char *path)
{
    if (unlink(path) < 0) {
        i32 err = errno;
        if (err == ENOENT) {
            return CORE_OK;
        }
        return core_error_from_errno(err);
    }
    return CORE_OK;
}

ErrorCode ipc_server_create(const String *endpoint, IpcServerHandle **out_server)
{
    if (endpoint == NULL || out_server == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (endpoint->length > IPC_ENDPOINT_MAX_LENGTH) {
        return CORE_INVALID_PATH;
    }

    MemoryBlock server_block;
    ErrorCode result = memory_allocate(sizeof(IpcServerHandle), &server_block);
    if (result != CORE_OK) {
        return result;
    }

    IpcServerHandle *server = (IpcServerHandle *)server_block.data;

    result = ipc_create_socket(&server->fd);
    if (result != CORE_OK) {
        memory_free(&server_block);
        return result;
    }

    MemoryBlock name_block;
    result = memory_allocate(endpoint->length + 1, &name_block);
    if (result != CORE_OK) {
        close(server->fd);
        memory_free(&server_block);
        return result;
    }

    memory_copy((Bytes){ name_block.data, endpoint->length },
                 (Bytes){ (u8 *)endpoint->data, endpoint->length });
    name_block.data[endpoint->length] = '\0';

    result = ipc_unlink_endpoint((const char *)name_block.data);
    if (result != CORE_OK) {
        close(server->fd);
        memory_free(&name_block);
        memory_free(&server_block);
        return result;
    }

    struct sockaddr_un addr;
    memory_set((Bytes){ (u8 *)&addr, sizeof(addr) }, 0);
    addr.sun_family = AF_UNIX;
    memory_copy((Bytes){ (u8 *)addr.sun_path, endpoint->length },
                 (Bytes){ name_block.data, endpoint->length });

    if (bind(server->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        i32 err = errno;
        close(server->fd);
        memory_free(&name_block);
        memory_free(&server_block);
        return core_error_from_errno(err);
    }

    if (listen(server->fd, 8) < 0) {
        i32 err = errno;
        close(server->fd);
        memory_free(&name_block);
        memory_free(&server_block);
        return core_error_from_errno(err);
    }

    memory_free(&name_block);

    server->endpoint_length = endpoint->length;
    if (endpoint->length > 0) {
        memory_copy((Bytes){ (u8 *)server->endpoint_path, endpoint->length },
                     (Bytes){ (u8 *)endpoint->data, endpoint->length });
    }
    server->endpoint_path[endpoint->length] = '\0';

    *out_server = server;
    return CORE_OK;
}

ErrorCode ipc_server_get_fd(IpcServerHandle *server, i32 *out_fd)
{
    if (server == NULL || out_fd == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_fd = server->fd;
    return CORE_OK;
}

ErrorCode ipc_server_accept(IpcServerHandle *server, IpcConnectionHandle **out_connection)
{
    if (server == NULL || out_connection == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    i32 client_fd = accept4(server->fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd < 0) {
        i32 err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return CORE_WOULD_BLOCK;
        }
        return core_error_from_errno(err);
    }

    MemoryBlock conn_block;
    ErrorCode result = memory_allocate(sizeof(IpcConnectionHandle), &conn_block);
    if (result != CORE_OK) {
        close(client_fd);
        return result;
    }

    IpcConnectionHandle *conn = (IpcConnectionHandle *)conn_block.data;
    conn->fd = client_fd;
    conn->state = IPC_CONNECTION_STATE_OPEN;
    conn->outgoing_buffer = NULL;
    conn->outgoing_size = 0;
    conn->outgoing_capacity = 0;
    conn->incoming_buffer = NULL;
    conn->incoming_size = 0;
    conn->incoming_capacity = 0;

    *out_connection = conn;
    return CORE_OK;
}

ErrorCode ipc_server_close(IpcServerHandle **server)
{
    if (server == NULL || *server == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    ErrorCode result = CORE_OK;
    IpcServerHandle *s = *server;

    if (s->endpoint_length > 0) {
        if (unlink(s->endpoint_path) < 0) {
            i32 err = errno;
            if (err != ENOENT) {
                result = core_error_from_errno(err);
            }
        }
    }

    if (close(s->fd) < 0) {
        i32 err = errno;
        if (result == CORE_OK) {
            result = core_error_from_errno(err);
        }
    }

    MemoryBlock block;
    block.data = (u8 *)s;
    block.size = sizeof(IpcServerHandle);
    memory_free(&block);

    *server = NULL;
    return result;
}

ErrorCode ipc_connection_begin_connect(const String *endpoint, IpcConnectionHandle **out_connection)
{
    if (endpoint == NULL || out_connection == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (endpoint->length > IPC_ENDPOINT_MAX_LENGTH) {
        return CORE_INVALID_PATH;
    }

    MemoryBlock conn_block;
    ErrorCode result = memory_allocate(sizeof(IpcConnectionHandle), &conn_block);
    if (result != CORE_OK) {
        return result;
    }

    IpcConnectionHandle *conn = (IpcConnectionHandle *)conn_block.data;

    result = ipc_create_socket(&conn->fd);
    if (result != CORE_OK) {
        memory_free(&conn_block);
        return result;
    }

    conn->state = IPC_CONNECTION_STATE_CONNECTING;
    conn->outgoing_buffer = NULL;
    conn->outgoing_size = 0;
    conn->outgoing_capacity = 0;
    conn->incoming_buffer = NULL;
    conn->incoming_size = 0;
    conn->incoming_capacity = 0;

    MemoryBlock name_block;
    result = memory_allocate(endpoint->length + 1, &name_block);
    if (result != CORE_OK) {
        close(conn->fd);
        memory_free(&conn_block);
        return result;
    }

    memory_copy((Bytes){ name_block.data, endpoint->length },
                 (Bytes){ (u8 *)endpoint->data, endpoint->length });
    name_block.data[endpoint->length] = '\0';

    struct sockaddr_un addr;
    memory_set((Bytes){ (u8 *)&addr, sizeof(addr) }, 0);
    addr.sun_family = AF_UNIX;
    memory_copy((Bytes){ (u8 *)addr.sun_path, endpoint->length },
                 (Bytes){ name_block.data, endpoint->length });

    if (connect(conn->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        i32 err = errno;
        if (err != EINPROGRESS) {
            close(conn->fd);
            memory_free(&name_block);
            memory_free(&conn_block);
            return core_error_from_errno(err);
        }
    }

    memory_free(&name_block);
    *out_connection = conn;
    return CORE_OK;
}

ErrorCode ipc_connection_finish_connect(IpcConnectionHandle *connection, IpcConnectionState *out_state)
{
    if (connection == NULL || out_state == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (connection->state != IPC_CONNECTION_STATE_CONNECTING) {
        return CORE_INVALID_STATE;
    }

    i32 so_error = 0;
    socklen_t len = sizeof(so_error);

    if (getsockopt(connection->fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0) {
        i32 err = errno;
        return core_error_from_errno(err);
    }

    if (so_error == 0) {
        connection->state = IPC_CONNECTION_STATE_OPEN;
        *out_state = IPC_CONNECTION_STATE_OPEN;
    } else {
        *out_state = IPC_CONNECTION_STATE_CONNECTING;
    }

    return CORE_OK;
}

ErrorCode ipc_connection_get_fd(IpcConnectionHandle *connection, i32 *out_fd)
{
    if (connection == NULL || out_fd == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_fd = connection->fd;
    return CORE_OK;
}

static ErrorCode ipc_ensure_outgoing_capacity(IpcConnectionHandle *connection, usize needed)
{
    if (connection->outgoing_capacity >= needed) {
        return CORE_OK;
    }

    usize new_capacity = connection->outgoing_capacity;
    if (new_capacity == 0) {
        new_capacity = 4096;
    }
    while (new_capacity < needed) {
        if (new_capacity > USIZE_MAX / 2) {
            return CORE_OVERFLOW;
        }
        new_capacity *= 2;
    }

    MemoryBlock new_block;
    ErrorCode result = memory_allocate(new_capacity, &new_block);
    if (result != CORE_OK) {
        return result;
    }

    if (connection->outgoing_size > 0) {
        memory_copy((Bytes){ new_block.data, connection->outgoing_size },
                     (Bytes){ connection->outgoing_buffer, connection->outgoing_size });
    }

    if (connection->outgoing_buffer != NULL) {
        MemoryBlock old_block;
        old_block.data = connection->outgoing_buffer;
        old_block.size = connection->outgoing_capacity;
        memory_free(&old_block);
    }

    connection->outgoing_buffer = new_block.data;
    connection->outgoing_capacity = new_capacity;
    return CORE_OK;
}

ErrorCode ipc_connection_send(IpcConnectionHandle *connection, const IpcMessage *message)
{
    if (connection == NULL || message == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (connection->state != IPC_CONNECTION_STATE_OPEN) {
        return CORE_INVALID_STATE;
    }
    if (message->payload.size > IPC_MESSAGE_MAX_SIZE) {
        return CORE_OVERFLOW;
    }

    usize frame_size = IPC_FRAME_HEADER_SIZE + message->payload.size;
    if (connection->outgoing_size + frame_size > IPC_OUTGOING_QUEUE_MAX_SIZE) {
        return CORE_OVERFLOW;
    }

    ErrorCode result = ipc_ensure_outgoing_capacity(connection, connection->outgoing_size + frame_size);
    if (result != CORE_OK) {
        return result;
    }

    u8 *frame = connection->outgoing_buffer + connection->outgoing_size;

    frame[0] = (u8)(message->payload.size & 0xFF);
    frame[1] = (u8)((message->payload.size >> 8) & 0xFF);
    frame[2] = (u8)((message->payload.size >> 16) & 0xFF);
    frame[3] = (u8)((message->payload.size >> 24) & 0xFF);

    frame[4] = (u8)(message->type & 0xFF);
    frame[5] = (u8)((message->type >> 8) & 0xFF);
    frame[6] = (u8)((message->type >> 16) & 0xFF);
    frame[7] = (u8)((message->type >> 24) & 0xFF);

    if (message->payload.size > 0) {
        memory_copy((Bytes){ frame + IPC_FRAME_HEADER_SIZE, message->payload.size }, message->payload);
    }

    connection->outgoing_size += frame_size;
    return CORE_OK;
}

ErrorCode ipc_connection_flush(IpcConnectionHandle *connection, bool *out_has_pending)
{
    if (connection == NULL || out_has_pending == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (connection->state != IPC_CONNECTION_STATE_OPEN) {
        return CORE_INVALID_STATE;
    }

    if (connection->outgoing_size == 0) {
        *out_has_pending = false;
        return CORE_OK;
    }

    ssize_t sent = send(connection->fd, connection->outgoing_buffer, connection->outgoing_size, MSG_NOSIGNAL);
    if (sent < 0) {
        i32 err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            *out_has_pending = true;
            return CORE_OK;
        }
        if (err == EPIPE) {
            connection->state = IPC_CONNECTION_STATE_PEER_CLOSED;
            *out_has_pending = false;
            return CORE_OK;
        }
        return core_error_from_errno(err);
    }

    usize sent_size = (usize)sent;
    if (sent_size < connection->outgoing_size) {
        memory_copy((Bytes){ connection->outgoing_buffer, connection->outgoing_size - sent_size },
                     (Bytes){ connection->outgoing_buffer + sent_size, connection->outgoing_size - sent_size });
    }
    connection->outgoing_size -= sent_size;

    *out_has_pending = connection->outgoing_size > 0;
    return CORE_OK;
}

static ErrorCode ipc_ensure_incoming_capacity(IpcConnectionHandle *connection, usize needed)
{
    if (connection->incoming_capacity >= needed) {
        return CORE_OK;
    }

    usize new_capacity = connection->incoming_capacity;
    if (new_capacity == 0) {
        new_capacity = 4096;
    }
    while (new_capacity < needed) {
        if (new_capacity > USIZE_MAX / 2) {
            return CORE_OVERFLOW;
        }
        new_capacity *= 2;
    }

    MemoryBlock new_block;
    ErrorCode result = memory_allocate(new_capacity, &new_block);
    if (result != CORE_OK) {
        return result;
    }

    if (connection->incoming_size > 0) {
        memory_copy((Bytes){ new_block.data, connection->incoming_size },
                     (Bytes){ connection->incoming_buffer, connection->incoming_size });
    }

    if (connection->incoming_buffer != NULL) {
        MemoryBlock old_block;
        old_block.data = connection->incoming_buffer;
        old_block.size = connection->incoming_capacity;
        memory_free(&old_block);
    }

    connection->incoming_buffer = new_block.data;
    connection->incoming_capacity = new_capacity;
    return CORE_OK;
}

/*
 * Removes the first complete frame from the incoming buffer, if there is one.
 *
 * The frame header and the type field are read before the remaining bytes are
 * moved to the front, so out_message never points into a shifted buffer.
 */
static ErrorCode ipc_take_buffered_frame(IpcConnectionHandle *connection, bool *out_extracted,
                                         IpcMessage *out_message)
{
    *out_extracted = false;

    if (connection->incoming_size < IPC_FRAME_HEADER_SIZE) {
        return CORE_OK;
    }

    u32 payload_size = (u32)connection->incoming_buffer[0] |
                       ((u32)connection->incoming_buffer[1] << 8) |
                       ((u32)connection->incoming_buffer[2] << 16) |
                       ((u32)connection->incoming_buffer[3] << 24);

    if (payload_size > IPC_MESSAGE_MAX_SIZE) {
        return CORE_OVERFLOW;
    }

    usize total_frame_size = IPC_FRAME_HEADER_SIZE + (usize)payload_size;

    if (connection->incoming_size < total_frame_size) {
        return CORE_OK;
    }

    u32 message_type = (u32)connection->incoming_buffer[4] |
                       ((u32)connection->incoming_buffer[5] << 8) |
                       ((u32)connection->incoming_buffer[6] << 16) |
                       ((u32)connection->incoming_buffer[7] << 24);

    MemoryBlock payload_block;
    ErrorCode result = memory_allocate(payload_size, &payload_block);
    if (result != CORE_OK) {
        return result;
    }

    if (payload_size > 0) {
        memory_copy((Bytes){ payload_block.data, payload_size },
                     (Bytes){ connection->incoming_buffer + IPC_FRAME_HEADER_SIZE, payload_size });
    }

    usize remaining = connection->incoming_size - total_frame_size;
    if (remaining > 0) {
        memory_move((Bytes){ connection->incoming_buffer, remaining },
                    (Bytes){ connection->incoming_buffer + total_frame_size, remaining });
    }
    connection->incoming_size = remaining;

    out_message->type = message_type;
    out_message->payload.data = payload_block.data;
    out_message->payload.size = payload_size;

    *out_extracted = true;
    return CORE_OK;
}

ErrorCode ipc_connection_receive(IpcConnectionHandle *connection, IpcReceiveStatus *out_status, IpcMessage *out_message)
{
    if (connection == NULL || out_status == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (connection->state != IPC_CONNECTION_STATE_OPEN) {
        return CORE_INVALID_STATE;
    }

    u8 recv_buffer[4096];

    for (;;) {
        /* A frame may already be complete in the buffer from an earlier call
         * that read several frames in one recv(). */
        bool extracted = false;
        ErrorCode result = ipc_take_buffered_frame(connection, &extracted, out_message);
        if (result != CORE_OK) {
            return result;
        }
        if (extracted) {
            *out_status = IPC_RECEIVE_STATUS_MESSAGE;
            return CORE_OK;
        }

        result = ipc_ensure_incoming_capacity(connection, connection->incoming_size + sizeof(recv_buffer));
        if (result != CORE_OK) {
            return result;
        }

        ssize_t received = recv(connection->fd, recv_buffer, sizeof(recv_buffer), 0);

        if (received < 0) {
            i32 err = errno;
            if (err == EAGAIN || err == EWOULDBLOCK) {
                *out_status = IPC_RECEIVE_STATUS_NO_MESSAGE;
                return CORE_OK;
            }
            if (err == EPIPE) {
                connection->state = IPC_CONNECTION_STATE_PEER_CLOSED;
                *out_status = IPC_RECEIVE_STATUS_PEER_CLOSED;
                return CORE_OK;
            }
            return core_error_from_errno(err);
        }

        if (received == 0) {
            connection->state = IPC_CONNECTION_STATE_PEER_CLOSED;
            *out_status = IPC_RECEIVE_STATUS_PEER_CLOSED;
            return CORE_OK;
        }

        memory_copy((Bytes){ connection->incoming_buffer + connection->incoming_size, (usize)received },
                     (Bytes){ recv_buffer, (usize)received });
        connection->incoming_size += (usize)received;
    }
}

ErrorCode ipc_message_destroy(IpcMessage *message)
{
    if (message == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    if (message->payload.data != NULL && message->payload.size > 0) {
        MemoryBlock block;
        block.data = message->payload.data;
        block.size = message->payload.size;
        memory_free(&block);
    }

    message->type = 0;
    message->payload.data = NULL;
    message->payload.size = 0;
    return CORE_OK;
}

ErrorCode ipc_connection_close(IpcConnectionHandle **connection)
{
    if (connection == NULL || *connection == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    ErrorCode result = CORE_OK;
    IpcConnectionHandle *conn = *connection;

    if (close(conn->fd) < 0) {
        i32 err = errno;
        result = core_error_from_errno(err);
    }

    if (conn->outgoing_buffer != NULL) {
        MemoryBlock block;
        block.data = conn->outgoing_buffer;
        block.size = conn->outgoing_capacity;
        memory_free(&block);
    }
    if (conn->incoming_buffer != NULL) {
        MemoryBlock block;
        block.data = conn->incoming_buffer;
        block.size = conn->incoming_capacity;
        memory_free(&block);
    }

    MemoryBlock conn_block;
    conn_block.data = (u8 *)conn;
    conn_block.size = sizeof(IpcConnectionHandle);
    memory_free(&conn_block);

    *connection = NULL;
    return result;
}
