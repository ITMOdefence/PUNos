#include <special/ipc.h>
_Static_assert(__builtin_types_compatible_p(IpcEndpoint, String), "endpoint identity");
_Static_assert(__builtin_types_compatible_p(IpcMessageType, u32), "message type identity");
_Static_assert(sizeof(IpcServerHandle) == 8 && sizeof(IpcConnectionHandle) == 8, "handles ABI");
_Static_assert(sizeof(IpcMessage) == 24 && __builtin_offsetof(IpcMessage, payload) == 8, "message ABI");
_Static_assert(sizeof(IpcConnectionState) == 4 && sizeof(IpcReceiveStatus) == 4, "enums ABI");
_Static_assert(IPC_CONNECTION_STATE_CONNECTING == 0 && IPC_CONNECTION_STATE_OPEN == 1 && IPC_CONNECTION_STATE_PEER_CLOSED == 2, "states");
_Static_assert(IPC_RECEIVE_STATUS_NO_MESSAGE == 0 && IPC_RECEIVE_STATUS_MESSAGE == 1 && IPC_RECEIVE_STATUS_PEER_CLOSED == 2, "statuses");
_Static_assert(IPC_ENDPOINT_MAX_LENGTH == 107 && IPC_FRAME_HEADER_SIZE == 8 && IPC_MESSAGE_MAX_SIZE == 1048576 && IPC_OUTGOING_QUEUE_MAX_SIZE == 4194304, "constants");
