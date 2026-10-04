# ipclib — special/ipc

Implements `DevDoc/SpecialLibs/ipclib.md`: AF_UNIX/SOCK_STREAM sockets with
NONBLOCK/CLOEXEC, explicit Connecting/Open/PeerClosed state, little-endian
8-byte headers and bounded, owning FIFO queues. `send` only copies; `flush`
handles short writes and backpressure with MSG_NOSIGNAL. `receive` reads only
the bytes required for one frame, retains partial input and rejects oversized
or truncated frames. Empty messages are legal. Allocation failure during
receive preserves the header for a retry. EAGAIN during Unix connect requires
retrying connect rather than treating SO_ERROR=0 as an established connection.

`make check` pins the ABI and uses real sockets to test state restrictions,
copy ownership, empty/multiple messages, fragmented headers and payloads,
queue limits, short writes/backpressure, orderly closure and malformed input.
Close discards pending buffers. Server creation unlinks the endpoint as the
v1 specification requires, and server close removes it.

Cross-compilation and emulated runtime verification passed. In a scratch copy,
altering the serialized message type failed at check 17 as expected. See the
parent README for the verification environment and native Linux/QEMU limitations.
