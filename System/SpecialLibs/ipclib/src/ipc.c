#include <special/ipc.h>
#include <sys/memory.h>
#include <punos/syscall/syscall.h>

struct OutgoingFrame {
    MemoryBlock block;
    struct OutgoingFrame *next;
    usize size, sent;
    u8 bytes[];
};
struct IpcServerHandle { MemoryBlock block; i32 fd; String endpoint; };
struct IpcConnectionHandle {
    MemoryBlock block;
    i32 fd;
    IpcConnectionState state;
    struct sockaddr_un address;
    bool retry_connect;
    struct OutgoingFrame *head, *tail;
    usize queued;
    u8 header[IPC_FRAME_HEADER_SIZE];
    usize header_used, payload_used;
    MemoryBlock payload;
};

static void put32(u8 *p, u32 value)
{
    for (usize i = 0; i < 4; ++i) p[i] = (u8)(value >> (i * 8));
}
static u32 get32(const u8 *p)
{
    u32 value = 0;
    for (usize i = 0; i < 4; ++i) value |= (u32)p[i] << (i * 8);
    return value;
}
static ErrorCode address_from(const IpcEndpoint *endpoint, struct sockaddr_un *address)
{
    const char *text;
    String validated;
    ErrorCode result;
    if (endpoint == NULL || endpoint->data == NULL || endpoint->length == 0)
        return CORE_INVALID_ARGUMENT;
    if (endpoint->length > IPC_ENDPOINT_MAX_LENGTH) return CORE_INVALID_PATH;
    result = string_c_str(endpoint, &text);
    if (result != CORE_OK) return CORE_INVALID_ARGUMENT;
    result = string_from_utf8((Bytes){(u8 *)text, endpoint->length}, &validated);
    if (result != CORE_OK) return result == CORE_OUT_OF_MEMORY ? result : CORE_INVALID_PATH;
    (void)string_destroy(&validated);
    (void)memory_set((Bytes){(u8 *)address, sizeof(*address)}, 0);
    address->sun_family = AF_UNIX;
    for (usize i = 0; i < endpoint->length; ++i) address->sun_path[i] = text[i];
    return CORE_OK;
}
static ErrorCode new_connection(i32 fd, IpcConnectionState state, IpcConnectionHandle *out)
{
    MemoryBlock block;
    ErrorCode result = memory_allocate(sizeof(struct IpcConnectionHandle), &block);
    if (result != CORE_OK) { (void)close(fd); return result; }
    IpcConnectionHandle connection = (IpcConnectionHandle)block.data;
    connection->block = block;
    connection->fd = fd;
    connection->state = state;
    *out = connection;
    return CORE_OK;
}
static bool peer_error(i32 error)
{
    return error == EPIPE || error == ECONNRESET || error == ENOTCONN;
}

ErrorCode ipc_server_create(const IpcEndpoint *endpoint, IpcServerHandle *out_server)
{
    struct sockaddr_un address;
    MemoryBlock block;
    String copy;
    i32 fd;
    ErrorCode result;
    if (out_server == NULL) return CORE_INVALID_ARGUMENT;
    result = address_from(endpoint, &address);
    if (result != CORE_OK) return result;
    result = string_copy(endpoint, &copy);
    if (result != CORE_OK) return result;
    result = memory_allocate(sizeof(struct IpcServerHandle), &block);
    if (result != CORE_OK) { (void)string_destroy(&copy); return result; }
    if (unlink(address.sun_path) < 0 && errno != ENOENT) {
        result = core_error_from_errno(errno);
        goto fail;
    }
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) { result = core_error_from_errno(errno); goto fail; }
    if (bind(fd, (const struct sockaddr *)&address, sizeof(address)) < 0) {
        result = core_error_from_errno(errno);
        (void)close(fd);
        goto fail;
    }
    if (listen(fd, 128) < 0) {
        result = core_error_from_errno(errno);
        (void)close(fd);
        (void)unlink(address.sun_path);
        goto fail;
    }
    IpcServerHandle server = (IpcServerHandle)block.data;
    server->block = block;
    server->fd = fd;
    server->endpoint = copy;
    *out_server = server;
    return CORE_OK;
fail:
    (void)memory_free(&block);
    (void)string_destroy(&copy);
    return result;
}
ErrorCode ipc_server_get_fd(IpcServerHandle server, i32 *out_fd)
{
    if (out_fd == NULL) return CORE_INVALID_ARGUMENT;
    if (server == NULL) return CORE_INVALID_STATE;
    *out_fd = server->fd;
    return CORE_OK;
}
ErrorCode ipc_server_accept(IpcServerHandle server, IpcConnectionHandle *out_connection)
{
    i32 fd;
    if (out_connection == NULL) return CORE_INVALID_ARGUMENT;
    if (server == NULL) return CORE_INVALID_STATE;
    fd = accept4(server->fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) return core_error_from_errno(errno);
    return new_connection(fd, IPC_CONNECTION_STATE_OPEN, out_connection);
}
ErrorCode ipc_server_close(IpcServerHandle *server)
{
    ErrorCode result = CORE_OK;
    MemoryBlock block;
    if (server == NULL) return CORE_INVALID_ARGUMENT;
    if (*server == NULL) return CORE_INVALID_STATE;
    block = (*server)->block;
    if (close((*server)->fd) < 0) result = core_error_from_errno(errno);
    if (unlink((*server)->endpoint.data) < 0 && errno != ENOENT && result == CORE_OK)
        result = core_error_from_errno(errno);
    (void)string_destroy(&(*server)->endpoint);
    (void)memory_free(&block);
    *server = NULL;
    return result;
}
ErrorCode ipc_connection_begin_connect(const IpcEndpoint *endpoint, IpcConnectionHandle *out_connection)
{
    struct sockaddr_un address;
    i32 fd, error = 0;
    IpcConnectionHandle connection;
    ErrorCode result;
    if (out_connection == NULL) return CORE_INVALID_ARGUMENT;
    result = address_from(endpoint, &address);
    if (result != CORE_OK) return result;
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return core_error_from_errno(errno);
    if (connect(fd, (const struct sockaddr *)&address, sizeof(address)) < 0) {
        error = errno;
        if (error != EINPROGRESS && error != EAGAIN) {
            result = core_error_from_errno(error);
            (void)close(fd);
            return result;
        }
    }
    result = new_connection(fd, IPC_CONNECTION_STATE_CONNECTING, &connection);
    if (result != CORE_OK) return result;
    (void)memory_copy((Bytes){(u8 *)&connection->address, sizeof(address)},
                      (Bytes){(u8 *)&address, sizeof(address)});
    connection->retry_connect = error == EAGAIN;
    *out_connection = connection;
    return CORE_OK;
}
ErrorCode ipc_connection_finish_connect(IpcConnectionHandle connection, IpcConnectionState *out_state)
{
    i32 error = 0;
    socklen_t length = sizeof(error);
    if (out_state == NULL) return CORE_INVALID_ARGUMENT;
    if (connection == NULL || connection->state != IPC_CONNECTION_STATE_CONNECTING)
        return CORE_INVALID_STATE;
    if (connection->retry_connect) {
        if (connect(connection->fd, (const struct sockaddr *)&connection->address,
                    sizeof(connection->address)) < 0) {
            error = errno;
            if (error == EAGAIN || error == EINPROGRESS || error == EALREADY) {
                connection->retry_connect = error == EAGAIN;
                *out_state = IPC_CONNECTION_STATE_CONNECTING;
                return CORE_OK;
            }
            if (error != EISCONN) {
                if (error != EINTR) connection->state = IPC_CONNECTION_STATE_PEER_CLOSED;
                return core_error_from_errno(error);
            }
        }
        connection->retry_connect = false;
    }
    if (getsockopt(connection->fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0)
        return core_error_from_errno(errno);
    if (error == EINPROGRESS || error == EALREADY || error == EAGAIN) {
        *out_state = IPC_CONNECTION_STATE_CONNECTING;
        return CORE_OK;
    }
    if (error != 0) {
        connection->state = IPC_CONNECTION_STATE_PEER_CLOSED;
        return core_error_from_errno(error);
    }
    connection->state = IPC_CONNECTION_STATE_OPEN;
    *out_state = connection->state;
    return CORE_OK;
}
ErrorCode ipc_connection_get_fd(IpcConnectionHandle connection, i32 *out_fd)
{
    if (out_fd == NULL) return CORE_INVALID_ARGUMENT;
    if (connection == NULL || connection->state == IPC_CONNECTION_STATE_PEER_CLOSED)
        return CORE_INVALID_STATE;
    *out_fd = connection->fd;
    return CORE_OK;
}
ErrorCode ipc_connection_send(IpcConnectionHandle connection, const IpcMessage *message)
{
    MemoryBlock block;
    usize size;
    ErrorCode result;
    if (message == NULL || (message->payload.size != 0 && message->payload.data == NULL))
        return CORE_INVALID_ARGUMENT;
    if (connection == NULL || connection->state != IPC_CONNECTION_STATE_OPEN)
        return CORE_INVALID_STATE;
    if (message->payload.size > IPC_MESSAGE_MAX_SIZE) return CORE_OVERFLOW;
    size = IPC_FRAME_HEADER_SIZE + message->payload.size;
    if (size > IPC_OUTGOING_QUEUE_MAX_SIZE - connection->queued) return CORE_OVERFLOW;
    result = memory_allocate(sizeof(struct OutgoingFrame) + size, &block);
    if (result != CORE_OK) return result;
    struct OutgoingFrame *frame = (struct OutgoingFrame *)block.data;
    frame->block = block;
    frame->size = size;
    put32(frame->bytes, (u32)message->payload.size);
    put32(frame->bytes + 4, message->type);
    (void)memory_copy((Bytes){frame->bytes + IPC_FRAME_HEADER_SIZE, message->payload.size}, message->payload);
    if (connection->tail != NULL) connection->tail->next = frame;
    else connection->head = frame;
    connection->tail = frame;
    connection->queued += size;
    return CORE_OK;
}
ErrorCode ipc_connection_flush(IpcConnectionHandle connection, bool *out_has_pending)
{
    if (out_has_pending == NULL) return CORE_INVALID_ARGUMENT;
    if (connection == NULL || connection->state != IPC_CONNECTION_STATE_OPEN)
        return CORE_INVALID_STATE;
    while (connection->head != NULL) {
        struct OutgoingFrame *frame = connection->head;
        ssize_t sent = send(connection->fd, frame->bytes + frame->sent,
                            frame->size - frame->sent, MSG_NOSIGNAL);
        if (sent < 0) {
            i32 error = errno;
            if (error == EAGAIN) break;
            if (peer_error(error)) connection->state = IPC_CONNECTION_STATE_PEER_CLOSED;
            return core_error_from_errno(error);
        }
        if (sent == 0) return CORE_IO_ERROR;
        frame->sent += (usize)sent;
        connection->queued -= (usize)sent;
        if (frame->sent == frame->size) {
            MemoryBlock block = frame->block;
            connection->head = frame->next;
            if (connection->head == NULL) connection->tail = NULL;
            (void)memory_free(&block);
        }
    }
    *out_has_pending = connection->head != NULL;
    return CORE_OK;
}
ErrorCode ipc_connection_receive(IpcConnectionHandle connection, IpcReceiveStatus *out_status,
                                 IpcMessage *out_message)
{
    if (out_status == NULL || out_message == NULL) return CORE_INVALID_ARGUMENT;
    if (connection == NULL || connection->state != IPC_CONNECTION_STATE_OPEN)
        return CORE_INVALID_STATE;
    for (;;) {
        u8 *destination;
        usize remaining;
        if (connection->header_used < IPC_FRAME_HEADER_SIZE) {
            destination = connection->header + connection->header_used;
            remaining = IPC_FRAME_HEADER_SIZE - connection->header_used;
        } else {
            usize size = get32(connection->header);
            if (size > IPC_MESSAGE_MAX_SIZE) {
                connection->state = IPC_CONNECTION_STATE_PEER_CLOSED;
                return CORE_IO_ERROR;
            }
            if (size != 0 && connection->payload.data == NULL) {
                ErrorCode result = memory_allocate(size, &connection->payload);
                if (result != CORE_OK) return result;
            }
            if (connection->payload_used == size) {
                *out_message = (IpcMessage){get32(connection->header + 4),
                                            {connection->payload.data, size}};
                connection->payload = (MemoryBlock){NULL, 0};
                connection->header_used = connection->payload_used = 0;
                *out_status = IPC_RECEIVE_STATUS_MESSAGE;
                return CORE_OK;
            }
            destination = connection->payload.data + connection->payload_used;
            remaining = size - connection->payload_used;
        }
        ssize_t received = recv(connection->fd, destination, remaining, 0);
        if (received < 0) {
            i32 error = errno;
            if (error == EAGAIN) {
                *out_status = IPC_RECEIVE_STATUS_NO_MESSAGE;
                return CORE_OK;
            }
            if (peer_error(error)) connection->state = IPC_CONNECTION_STATE_PEER_CLOSED;
            return core_error_from_errno(error);
        }
        if (received == 0) {
            connection->state = IPC_CONNECTION_STATE_PEER_CLOSED;
            if (connection->header_used != 0) return CORE_IO_ERROR;
            *out_status = IPC_RECEIVE_STATUS_PEER_CLOSED;
            return CORE_OK;
        }
        if (connection->header_used < IPC_FRAME_HEADER_SIZE) connection->header_used += (usize)received;
        else connection->payload_used += (usize)received;
    }
}
ErrorCode ipc_message_destroy(IpcMessage *message)
{
    MemoryBlock block;
    ErrorCode result;
    if (message == NULL || message->payload.size > IPC_MESSAGE_MAX_SIZE ||
        (message->payload.data == NULL && message->payload.size != 0)) return CORE_INVALID_ARGUMENT;
    block = (MemoryBlock){message->payload.data, message->payload.size};
    result = memory_free(&block);
    if (result == CORE_OK) {
        message->type = 0;
        message->payload.data = NULL;
        message->payload.size = 0;
    }
    return result;
}
ErrorCode ipc_connection_close(IpcConnectionHandle *connection)
{
    MemoryBlock block;
    ErrorCode result = CORE_OK;
    if (connection == NULL) return CORE_INVALID_ARGUMENT;
    if (*connection == NULL) return CORE_INVALID_STATE;
    block = (*connection)->block;
    if (close((*connection)->fd) < 0) result = core_error_from_errno(errno);
    while ((*connection)->head != NULL) {
        struct OutgoingFrame *frame = (*connection)->head;
        MemoryBlock allocation = frame->block;
        (*connection)->head = frame->next;
        (void)memory_free(&allocation);
    }
    (void)memory_free(&(*connection)->payload);
    (void)memory_free(&block);
    *connection = NULL;
    return result;
}
