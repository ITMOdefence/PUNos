#ifndef PUNOS_APPFRAMEWORK_TESTS_FAKE_PEER_H
#define PUNOS_APPFRAMEWORK_TESTS_FAKE_PEER_H

/*
 * A stand-in for logserv or appserv: a real ipclib server on a socket in /tmp
 * that the test drives by hand. All operations are non-blocking; the test
 * alternates test_pump(app, ...) with the calls below.
 */

#include <special/ipc.h>

#include "support.h"

typedef struct FakePeer {
    IpcServerHandle server;
    IpcConnectionHandle connection;
} FakePeer;

static inline bool fake_peer_start(FakePeer *peer, const char *path)
{
    String endpoint = { 0 };
    ErrorCode result;

    peer->server = NULL;
    peer->connection = NULL;
    if (string_from_c_str(path, &endpoint) != CORE_OK) {
        return false;
    }
    result = ipc_server_create(&endpoint, &peer->server);
    (void)string_destroy(&endpoint);
    return result == CORE_OK;
}

static inline void fake_peer_drop(FakePeer *peer)
{
    if (peer->connection != NULL) {
        (void)ipc_connection_close(&peer->connection);
    }
}

static inline void fake_peer_stop(FakePeer *peer)
{
    fake_peer_drop(peer);
    if (peer->server != NULL) {
        (void)ipc_server_close(&peer->server);
    }
}

/* Takes the newest pending connection and drops the older ones, including the
 * current one: a client that reconnected has abandoned the earlier socket. */
static inline bool fake_peer_accept(FakePeer *peer)
{
    bool any = false;
    IpcConnectionHandle accepted = NULL;

    while (ipc_server_accept(peer->server, &accepted) == CORE_OK) {
        fake_peer_drop(peer);
        peer->connection = accepted;
        any = true;
    }
    return any;
}

/* 1: one message in *out (release it with ipc_message_destroy), 0: nothing yet,
 * -1: the application closed the connection or the stream is broken. */
static inline i32 fake_peer_receive(FakePeer *peer, IpcMessage *out)
{
    IpcReceiveStatus status = IPC_RECEIVE_STATUS_NO_MESSAGE;

    if (peer->connection == NULL) {
        return 0;
    }
    if (ipc_connection_receive(peer->connection, &status, out) != CORE_OK) {
        return -1;
    }
    if (status == IPC_RECEIVE_STATUS_MESSAGE) {
        return 1;
    }
    return status == IPC_RECEIVE_STATUS_PEER_CLOSED ? -1 : 0;
}

static inline bool fake_peer_send(FakePeer *peer, u32 type, const u8 *data, usize size)
{
    bool pending = false;
    IpcMessage message = { type, { (u8 *)data, size } };

    return peer->connection != NULL && ipc_connection_send(peer->connection, &message) == CORE_OK &&
           ipc_connection_flush(peer->connection, &pending) == CORE_OK;
}

static inline void put_le32(u8 *out, u32 value)
{
    for (u32 i = 0; i < 4; ++i) {
        out[i] = (u8)((value >> (8 * i)) & 0xff);
    }
}

static inline u32 get_le32(const u8 *in)
{
    return (u32)in[0] | ((u32)in[1] << 8) | ((u32)in[2] << 16) | ((u32)in[3] << 24);
}

static inline u16 get_le16(const u8 *in)
{
    return (u16)(in[0] | (in[1] << 8));
}

/* log.submit_result / any "ErrorCode only" reply: type + 4 bytes. */
static inline bool fake_peer_reply(FakePeer *peer, u32 type, ErrorCode code)
{
    u8 payload[4];

    put_le32(payload, (u32)code);
    return fake_peer_send(peer, type, payload, sizeof(payload));
}

/* Raw bytes on the wire, for frames ipclib itself would never produce. */
static inline bool fake_peer_write_raw(FakePeer *peer, const u8 *data, usize size)
{
    i32 fd = -1;

    return peer->connection != NULL && ipc_connection_get_fd(peer->connection, &fd) == CORE_OK &&
           write(fd, data, size) == (isize)size;
}

#endif
