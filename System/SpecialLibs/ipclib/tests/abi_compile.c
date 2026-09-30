/*
 * Compile-time checks of the ipclib ABI against DevDoc/SpecialLibs/ipclib.md.
 * This file only has to compile; it produces no runnable program.
 */
#include <special/ipc.h>

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)

_Static_assert(SAME_TYPE((IpcMessageType)0, u32), "IpcMessageType must be u32");
_Static_assert(sizeof(IpcMessage) == 24, "IpcMessage ABI");
_Static_assert(sizeof(IpcConnectionState) == 4, "IpcConnectionState ABI");
_Static_assert(sizeof(IpcReceiveStatus) == 4, "IpcReceiveStatus ABI");

_Static_assert(IPC_ENDPOINT_MAX_LENGTH == 107, "IPC_ENDPOINT_MAX_LENGTH");
_Static_assert(IPC_FRAME_HEADER_SIZE == 8, "IPC_FRAME_HEADER_SIZE");
_Static_assert(IPC_MESSAGE_MAX_SIZE == 1048576, "IPC_MESSAGE_MAX_SIZE");
_Static_assert(IPC_OUTGOING_QUEUE_MAX_SIZE == 4194304, "IPC_OUTGOING_QUEUE_MAX_SIZE");

_Static_assert(IPC_CONNECTION_STATE_CONNECTING == 0, "IPC_CONNECTION_STATE_CONNECTING");
_Static_assert(IPC_CONNECTION_STATE_OPEN == 1, "IPC_CONNECTION_STATE_OPEN");
_Static_assert(IPC_CONNECTION_STATE_PEER_CLOSED == 2, "IPC_CONNECTION_STATE_PEER_CLOSED");

_Static_assert(IPC_RECEIVE_STATUS_NO_MESSAGE == 0, "IPC_RECEIVE_STATUS_NO_MESSAGE");
_Static_assert(IPC_RECEIVE_STATUS_MESSAGE == 1, "IPC_RECEIVE_STATUS_MESSAGE");
_Static_assert(IPC_RECEIVE_STATUS_PEER_CLOSED == 2, "IPC_RECEIVE_STATUS_PEER_CLOSED");

int main(void)
{
    return 0;
}
