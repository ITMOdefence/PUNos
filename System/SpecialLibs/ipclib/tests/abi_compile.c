#include <special/ipc.h>

_Static_assert(IPC_FRAME_HEADER_SIZE == 12, "frame header size");
_Static_assert(IPC_NO_DESCRIPTOR == -1, "descriptor sentinel");
_Static_assert(sizeof(IpcMessage) == 32, "IpcMessage ABI mismatch");
_Static_assert(sizeof(IpcConnectionState) == 4, "state ABI mismatch");
_Static_assert(sizeof(IpcReceiveStatus) == 4, "receive status ABI mismatch");
_Static_assert(sizeof(IpcServerHandle) == sizeof(void *), "server handle ABI");
_Static_assert(sizeof(IpcConnectionHandle) == sizeof(void *), "connection handle ABI");

int ipc_abi_compile_test(void)
{
    return 0;
}
