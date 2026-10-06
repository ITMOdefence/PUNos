#include <special/ipc.h>

#include <sys/memory.h>

#include <punos/syscall/syscall.h>

#define IPC_LISTEN_BACKLOG 16
#define IPC_DESCRIPTOR_COUNT_NONE 0U
#define IPC_DESCRIPTOR_COUNT_ONE 1U
#define CMSG_ONE_FD_LENGTH (sizeof(struct cmsghdr) + sizeof(i32))
#define CMSG_ONE_FD_SPACE ((usize)24)

typedef struct OutgoingFrame {
    MemoryBlock block;
    struct OutgoingFrame *next;
    usize frame_size;
    usize sent;
    i32 descriptor;
} OutgoingFrame;

struct IpcServerHandle {
    MemoryBlock handle_block;
    i32 fd;
    String endpoint;
};

struct IpcConnectionHandle {
    MemoryBlock handle_block;
    i32 fd;
    IpcConnectionState state;
    OutgoingFrame *outgoing_head;
    OutgoingFrame *outgoing_tail;
    usize outgoing_size;
    MemoryBlock incoming;
    usize incoming_used;
    usize incoming_target;
    i32 incoming_descriptor;
};

static ErrorCode last_error(void)
{
    return core_error_from_errno(errno);
}

static void put_u32(u8 *destination, u32 value)
{
    destination[0] = (u8)value;
    destination[1] = (u8)(value >> 8);
    destination[2] = (u8)(value >> 16);
    destination[3] = (u8)(value >> 24);
}

static u32 get_u32(const u8 *source)
{
    return (u32)source[0] | ((u32)source[1] << 8) |
           ((u32)source[2] << 16) | ((u32)source[3] << 24);
}

static ErrorCode endpoint_address(const IpcEndpoint *endpoint,
                                  struct sockaddr_un *out_address,
                                  socklen_t *out_length)
{
    const char *path;
    usize index;
    ErrorCode result;

    if (endpoint == NULL || out_address == NULL || out_length == NULL ||
        endpoint->length == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    if (endpoint->length > IPC_ENDPOINT_MAX_LENGTH) {
        return CORE_INVALID_PATH;
    }
    result = string_c_str(endpoint, &path);
    if (result != CORE_OK) {
        return result;
    }
    out_address->sun_family = AF_UNIX;
    for (index = 0; index <= endpoint->length; ++index) {
        out_address->sun_path[index] = path[index];
    }
    *out_length = (socklen_t)(sizeof(out_address->sun_family) +
                              endpoint->length + 1);
    return CORE_OK;
}

static ErrorCode connection_create(i32 fd, IpcConnectionState state,
                                   IpcConnectionHandle *out_connection)
{
    MemoryBlock block;
    struct IpcConnectionHandle *connection;
    ErrorCode result = memory_allocate(sizeof(*connection), &block);

    if (result != CORE_OK) {
        return result;
    }
    connection = (struct IpcConnectionHandle *)block.data;
    connection->handle_block = block;
    connection->fd = fd;
    connection->state = state;
    connection->outgoing_head = NULL;
    connection->outgoing_tail = NULL;
    connection->outgoing_size = 0;
    connection->incoming.data = NULL;
    connection->incoming.size = 0;
    connection->incoming_used = 0;
    connection->incoming_target = IPC_FRAME_HEADER_SIZE;
    connection->incoming_descriptor = IPC_NO_DESCRIPTOR;
    *out_connection = connection;
    return CORE_OK;
}

ErrorCode ipc_server_create(const IpcEndpoint *endpoint,
                            IpcServerHandle *out_server)
{
    struct sockaddr_un address;
    socklen_t address_length;
    MemoryBlock block;
    struct IpcServerHandle *server;
    const char *path;
    i32 fd;
    ErrorCode result;

    if (out_server == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = endpoint_address(endpoint, &address, &address_length);
    if (result != CORE_OK) {
        return result;
    }
    (void)string_c_str(endpoint, &path);
    (void)unlink(path);
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return last_error();
    }
    if (bind(fd, (const struct sockaddr *)&address, address_length) != 0 ||
        listen(fd, IPC_LISTEN_BACKLOG) != 0) {
        result = last_error();
        (void)close(fd);
        (void)unlink(path);
        return result;
    }
    result = memory_allocate(sizeof(*server), &block);
    if (result != CORE_OK) {
        (void)close(fd);
        (void)unlink(path);
        return result;
    }
    server = (struct IpcServerHandle *)block.data;
    result = string_copy(endpoint, &server->endpoint);
    if (result != CORE_OK) {
        (void)memory_free(&block);
        (void)close(fd);
        (void)unlink(path);
        return result;
    }
    server->handle_block = block;
    server->fd = fd;
    *out_server = server;
    return CORE_OK;
}

ErrorCode ipc_server_get_fd(IpcServerHandle server, i32 *out_fd)
{
    if (server == NULL || out_fd == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_fd = server->fd;
    return CORE_OK;
}

ErrorCode ipc_server_accept(IpcServerHandle server,
                            IpcConnectionHandle *out_connection)
{
    i32 fd;
    ErrorCode result;

    if (server == NULL || out_connection == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    fd = accept4(server->fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) {
        return last_error();
    }
    result = connection_create(fd, IPC_CONNECTION_OPEN, out_connection);
    if (result != CORE_OK) {
        (void)close(fd);
    }
    return result;
}

ErrorCode ipc_server_close(IpcServerHandle *server)
{
    struct IpcServerHandle *handle;
    MemoryBlock block;
    const char *path;
    ErrorCode result = CORE_OK;

    if (server == NULL || *server == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    handle = *server;
    block = handle->handle_block;
    (void)string_c_str(&handle->endpoint, &path);
    if (close(handle->fd) != 0) {
        result = last_error();
    }
    if (unlink(path) != 0 && errno != ENOENT && result == CORE_OK) {
        result = last_error();
    }
    if (string_destroy(&handle->endpoint) != CORE_OK && result == CORE_OK) {
        result = CORE_IO_ERROR;
    }
    if (memory_free(&block) != CORE_OK && result == CORE_OK) {
        result = CORE_IO_ERROR;
    }
    *server = NULL;
    return result;
}

ErrorCode ipc_connection_begin_connect(const IpcEndpoint *endpoint,
                                       IpcConnectionHandle *out_connection)
{
    struct sockaddr_un address;
    socklen_t address_length;
    IpcConnectionState state = IPC_CONNECTION_OPEN;
    i32 fd;
    ErrorCode result;

    if (out_connection == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = endpoint_address(endpoint, &address, &address_length);
    if (result != CORE_OK) {
        return result;
    }
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return last_error();
    }
    if (connect(fd, (const struct sockaddr *)&address, address_length) != 0) {
        if (errno != EINPROGRESS) {
            result = last_error();
            (void)close(fd);
            return result;
        }
        state = IPC_CONNECTION_CONNECTING;
    }
    result = connection_create(fd, state, out_connection);
    if (result != CORE_OK) {
        (void)close(fd);
    }
    return result;
}

ErrorCode ipc_connection_finish_connect(IpcConnectionHandle connection,
                                        IpcConnectionState *out_state)
{
    i32 socket_error = 0;
    socklen_t length = sizeof(socket_error);

    if (connection == NULL || out_state == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (connection->state == IPC_CONNECTION_OPEN) {
        *out_state = connection->state;
        return CORE_OK;
    }
    if (connection->state != IPC_CONNECTION_CONNECTING) {
        return CORE_INVALID_STATE;
    }
    if (getsockopt(connection->fd, SOL_SOCKET, SO_ERROR, &socket_error,
                   &length) != 0) {
        return last_error();
    }
    if (socket_error == EINPROGRESS) {
        *out_state = connection->state;
        return CORE_OK;
    }
    if (socket_error != 0) {
        return core_error_from_errno(socket_error);
    }
    connection->state = IPC_CONNECTION_OPEN;
    *out_state = connection->state;
    return CORE_OK;
}

ErrorCode ipc_connection_get_fd(IpcConnectionHandle connection, i32 *out_fd)
{
    if (connection == NULL || out_fd == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_fd = connection->fd;
    return CORE_OK;
}

ErrorCode ipc_connection_send(IpcConnectionHandle connection,
                              const IpcMessage *message)
{
    usize frame_size;
    usize allocation_size;
    MemoryBlock block;
    OutgoingFrame *frame;
    u8 *bytes;
    i32 descriptor = IPC_NO_DESCRIPTOR;
    ErrorCode result;

    if (connection == NULL || message == NULL ||
        (message->payload.size != 0 && message->payload.data == NULL) ||
        message->payload.size > IPC_MESSAGE_MAX_SIZE ||
        message->descriptor < IPC_NO_DESCRIPTOR) {
        return CORE_INVALID_ARGUMENT;
    }
    if (connection->state != IPC_CONNECTION_OPEN) {
        return CORE_INVALID_STATE;
    }
    result = core_size_add(IPC_FRAME_HEADER_SIZE, message->payload.size,
                           &frame_size);
    if (result != CORE_OK || frame_size > IPC_OUTGOING_QUEUE_MAX_SIZE ||
        connection->outgoing_size > IPC_OUTGOING_QUEUE_MAX_SIZE - frame_size) {
        return CORE_OVERFLOW;
    }
    if (message->descriptor != IPC_NO_DESCRIPTOR) {
        descriptor = fcntl(message->descriptor, F_DUPFD_CLOEXEC, 0);
        if (descriptor < 0) {
            return last_error();
        }
    }
    result = core_size_add(sizeof(OutgoingFrame), frame_size, &allocation_size);
    if (result != CORE_OK) {
        result = CORE_OVERFLOW;
        goto fail_descriptor;
    }
    result = memory_allocate(allocation_size, &block);
    if (result != CORE_OK) {
        goto fail_descriptor;
    }
    frame = (OutgoingFrame *)block.data;
    bytes = block.data + sizeof(OutgoingFrame);
    frame->block = block;
    frame->next = NULL;
    frame->frame_size = frame_size;
    frame->sent = 0;
    frame->descriptor = descriptor;
    put_u32(bytes, (u32)message->payload.size);
    put_u32(bytes + 4, message->type);
    put_u32(bytes + 8, descriptor == IPC_NO_DESCRIPTOR ?
            IPC_DESCRIPTOR_COUNT_NONE : IPC_DESCRIPTOR_COUNT_ONE);
    if (message->payload.size != 0) {
        Bytes destination = { bytes + IPC_FRAME_HEADER_SIZE,
                              message->payload.size };
        result = memory_copy(destination, message->payload);
        if (result != CORE_OK) {
            (void)memory_free(&block);
            goto fail_descriptor;
        }
    }
    if (connection->outgoing_tail == NULL) {
        connection->outgoing_head = frame;
    } else {
        connection->outgoing_tail->next = frame;
    }
    connection->outgoing_tail = frame;
    connection->outgoing_size += frame_size;
    return CORE_OK;

fail_descriptor:
    if (descriptor != IPC_NO_DESCRIPTOR) {
        (void)close(descriptor);
    }
    return result;
}

static ErrorCode flush_frame(IpcConnectionHandle connection,
                             OutgoingFrame *frame, ssize_t *out_sent)
{
    u8 *bytes = frame->block.data + sizeof(OutgoingFrame) + frame->sent;
    struct iovec vector = { bytes, frame->frame_size - frame->sent };
    union ControlBuffer {
        struct cmsghdr alignment;
        u8 bytes[CMSG_ONE_FD_SPACE];
    } control = { { 0, 0, 0 } };
    struct msghdr message = { NULL, 0, &vector, 1, NULL, 0, 0 };

    if (frame->descriptor != IPC_NO_DESCRIPTOR) {
        struct cmsghdr *header = (struct cmsghdr *)control.bytes;
        header->cmsg_len = CMSG_ONE_FD_LENGTH;
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        *(i32 *)(control.bytes + sizeof(struct cmsghdr)) = frame->descriptor;
        message.msg_control = control.bytes;
        message.msg_controllen = sizeof(control);
    }
    *out_sent = sendmsg(connection->fd, &message, MSG_NOSIGNAL);
    if (*out_sent < 0) {
        if (errno == EAGAIN || errno == EINTR) {
            return CORE_WOULD_BLOCK;
        }
        return last_error();
    }
    if (*out_sent == 0) {
        return CORE_WOULD_BLOCK;
    }
    if (frame->descriptor != IPC_NO_DESCRIPTOR) {
        (void)close(frame->descriptor);
        frame->descriptor = IPC_NO_DESCRIPTOR;
    }
    return CORE_OK;
}

ErrorCode ipc_connection_flush(IpcConnectionHandle connection,
                               bool *out_has_pending)
{
    if (connection == NULL || out_has_pending == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (connection->state != IPC_CONNECTION_OPEN) {
        return CORE_INVALID_STATE;
    }
    while (connection->outgoing_head != NULL) {
        OutgoingFrame *frame = connection->outgoing_head;
        ssize_t sent;
        ErrorCode result = flush_frame(connection, frame, &sent);

        if (result == CORE_WOULD_BLOCK) {
            *out_has_pending = true;
            return CORE_OK;
        }
        if (result != CORE_OK) {
            return result;
        }
        frame->sent += (usize)sent;
        connection->outgoing_size -= (usize)sent;
        if (frame->sent == frame->frame_size) {
            MemoryBlock block = frame->block;
            connection->outgoing_head = frame->next;
            if (connection->outgoing_head == NULL) {
                connection->outgoing_tail = NULL;
            }
            (void)memory_free(&block);
        }
    }
    *out_has_pending = false;
    return CORE_OK;
}

static ErrorCode receive_chunk(IpcConnectionHandle connection,
                               usize maximum, ssize_t *out_received)
{
    struct iovec vector = {
        connection->incoming.data + connection->incoming_used, maximum
    };
    union ControlBuffer {
        struct cmsghdr alignment;
        u8 bytes[CMSG_ONE_FD_SPACE];
    } control = { { 0, 0, 0 } };
    struct msghdr message = {
        NULL, 0, &vector, 1, control.bytes, sizeof(control), 0
    };

    *out_received = recvmsg(connection->fd, &message, MSG_CMSG_CLOEXEC);
    if (*out_received < 0) {
        if (errno == EAGAIN || errno == EINTR) {
            return CORE_WOULD_BLOCK;
        }
        return last_error();
    }
    if ((message.msg_flags & MSG_CTRUNC) != 0) {
        return CORE_IO_ERROR;
    }
    if (message.msg_controllen != 0) {
        struct cmsghdr *header = (struct cmsghdr *)control.bytes;
        i32 descriptor;

        if (connection->incoming_descriptor != IPC_NO_DESCRIPTOR ||
            header->cmsg_len != CMSG_ONE_FD_LENGTH ||
            header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS) {
            return CORE_IO_ERROR;
        }
        descriptor = *(i32 *)(control.bytes + sizeof(struct cmsghdr));
        if (descriptor < 0) {
            return CORE_IO_ERROR;
        }
        connection->incoming_descriptor = descriptor;
    }
    return CORE_OK;
}

ErrorCode ipc_connection_receive(IpcConnectionHandle connection,
                                 IpcReceiveStatus *out_status,
                                 IpcMessage *out_message)
{
    ErrorCode result;

    if (connection == NULL || out_status == NULL || out_message == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (connection->state != IPC_CONNECTION_OPEN) {
        return CORE_INVALID_STATE;
    }
    if (connection->incoming.data == NULL) {
        result = memory_allocate(IPC_FRAME_HEADER_SIZE, &connection->incoming);
        if (result != CORE_OK) {
            return result;
        }
    }
    while (connection->incoming_used < connection->incoming_target) {
        ssize_t received;
        usize remaining = connection->incoming_target - connection->incoming_used;

        result = receive_chunk(connection, remaining, &received);
        if (result == CORE_WOULD_BLOCK) {
            *out_status = IPC_RECEIVE_NO_MESSAGE;
            return CORE_OK;
        }
        if (result != CORE_OK) {
            return result;
        }
        if (received == 0) {
            if (connection->incoming_used != 0) {
                return CORE_IO_ERROR;
            }
            connection->state = IPC_CONNECTION_PEER_CLOSED;
            *out_status = IPC_RECEIVE_PEER_CLOSED;
            return CORE_OK;
        }
        connection->incoming_used += (usize)received;
        if (connection->incoming_used == IPC_FRAME_HEADER_SIZE &&
            connection->incoming_target == IPC_FRAME_HEADER_SIZE) {
            u32 payload_size = get_u32(connection->incoming.data);
            u32 descriptor_count = get_u32(connection->incoming.data + 8);
            usize target;

            if (payload_size > IPC_MESSAGE_MAX_SIZE ||
                descriptor_count > IPC_DESCRIPTOR_COUNT_ONE ||
                (descriptor_count == 0) !=
                    (connection->incoming_descriptor == IPC_NO_DESCRIPTOR) ||
                core_size_add(IPC_FRAME_HEADER_SIZE, payload_size, &target) !=
                    CORE_OK) {
                return CORE_IO_ERROR;
            }
            connection->incoming_target = target;
            if (target != IPC_FRAME_HEADER_SIZE) {
                result = memory_reallocate(&connection->incoming, target);
                if (result != CORE_OK) {
                    return result;
                }
            }
        }
    }
    {
        u32 payload_size = get_u32(connection->incoming.data);
        MemoryBlock payload = { NULL, 0 };
        i32 descriptor = connection->incoming_descriptor;

        result = memory_allocate(payload_size, &payload);
        if (result != CORE_OK) {
            return result;
        }
        if (payload_size != 0) {
            Bytes destination = { payload.data, payload.size };
            Bytes source = { connection->incoming.data + IPC_FRAME_HEADER_SIZE,
                             payload_size };
            result = memory_copy(destination, source);
            if (result != CORE_OK) {
                (void)memory_free(&payload);
                return result;
            }
        }
        out_message->type = get_u32(connection->incoming.data + 4);
        out_message->reserved = 0;
        out_message->payload.data = payload.data;
        out_message->payload.size = payload.size;
        out_message->descriptor = descriptor;
        out_message->descriptor_reserved = 0;
        connection->incoming_descriptor = IPC_NO_DESCRIPTOR;
        connection->incoming_used = 0;
        connection->incoming_target = IPC_FRAME_HEADER_SIZE;
        if (connection->incoming.size != IPC_FRAME_HEADER_SIZE) {
            (void)memory_reallocate(&connection->incoming,
                                    IPC_FRAME_HEADER_SIZE);
        }
        *out_status = IPC_RECEIVE_MESSAGE;
        return CORE_OK;
    }
}

ErrorCode ipc_message_destroy(IpcMessage *message)
{
    MemoryBlock payload;
    ErrorCode result = CORE_OK;

    if (message == NULL || message->descriptor < IPC_NO_DESCRIPTOR ||
        (message->payload.size != 0 && message->payload.data == NULL)) {
        return CORE_INVALID_ARGUMENT;
    }
    payload.data = message->payload.data;
    payload.size = message->payload.size;
    if (message->descriptor != IPC_NO_DESCRIPTOR &&
        close(message->descriptor) != 0) {
        result = last_error();
    }
    if (memory_free(&payload) != CORE_OK && result == CORE_OK) {
        result = CORE_IO_ERROR;
    }
    message->type = 0;
    message->reserved = 0;
    message->payload.data = NULL;
    message->payload.size = 0;
    message->descriptor = IPC_NO_DESCRIPTOR;
    message->descriptor_reserved = 0;
    return result;
}

ErrorCode ipc_connection_close(IpcConnectionHandle *connection)
{
    struct IpcConnectionHandle *handle;
    MemoryBlock handle_block;
    ErrorCode result = CORE_OK;

    if (connection == NULL || *connection == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    handle = *connection;
    while (handle->outgoing_head != NULL) {
        OutgoingFrame *frame = handle->outgoing_head;
        MemoryBlock block = frame->block;
        handle->outgoing_head = frame->next;
        if (frame->descriptor != IPC_NO_DESCRIPTOR) {
            (void)close(frame->descriptor);
        }
        (void)memory_free(&block);
    }
    if (handle->incoming_descriptor != IPC_NO_DESCRIPTOR) {
        (void)close(handle->incoming_descriptor);
    }
    (void)memory_free(&handle->incoming);
    if (close(handle->fd) != 0) {
        result = last_error();
    }
    handle_block = handle->handle_block;
    if (memory_free(&handle_block) != CORE_OK && result == CORE_OK) {
        result = CORE_IO_ERROR;
    }
    *connection = NULL;
    return result;
}
