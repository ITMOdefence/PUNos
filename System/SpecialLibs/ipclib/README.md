# ipclib

Special Lib of PUNos (`special/ipc`): local inter-process communication
between components through non-blocking Unix domain sockets.

Specification: `DevDoc/SpecialLibs/ipclib.md`.

## Build and test

From the repository root:

```bash
make -C System/SpecialLibs/ipclib check
```

Outputs:

```text
System/SpecialLibs/ipclib/build/libpunos_special_ipc.a   # the library
System/SpecialLibs/ipclib/build/ipc_runtime              # runtime test
```

## Usage

```c
#include <special/ipc.h>

IpcServerHandle *server = NULL;
IpcConnectionHandle *conn = NULL;
String endpoint;

string_from_c_str("/tmp/test.sock", &endpoint);
ipc_server_create(&endpoint, &server);
ipc_server_accept(server, &conn);
ipc_connection_close(&conn);
ipc_server_close(&server);
string_destroy(&endpoint);
```

## What the library provides

- **Server operations.** `ipc_server_create`, `ipc_server_get_fd`,
  `ipc_server_accept`, `ipc_server_close`.
- **Connection operations.** `ipc_connection_begin_connect`,
  `ipc_connection_finish_connect`, `ipc_connection_get_fd`,
  `ipc_connection_send`, `ipc_connection_flush`, `ipc_connection_receive`,
  `ipc_message_destroy`, `ipc_connection_close`.
- **Frame format.** 8-byte header: `payload_size` (u32), `message_type` (u32),
  followed by payload bytes.
- **States.** Connecting → Open → PeerClosed.

## Implementation decisions

- **Frame header is 8 bytes.** `payload_size` (u32) + `message_type` (u32).
- **Outgoing queue is a simple byte buffer.** Messages are copied into a
  contiguous buffer and flushed with send().
- **Incoming buffer grows as needed.** Received bytes are accumulated until
  a complete frame is available.
- **All sockets are non-blocking.** SOCK_NONBLOCK | SOCK_CLOEXEC.
- **Server unconditionally unlinks endpoint before binding.**

## Tests

- `tests/abi_compile.c`: compile-time checks of types, constants and enum values.
- `tests/ipc_runtime.c`: runtime checks of server/connection lifecycle,
  message send/receive, error paths and state transitions.
