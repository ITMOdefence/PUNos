# logserv v1

Freestanding Linux x86_64 structured journal service for PUNos. Specification:
`DevDoc/SystemServices/base/logserv.md`. Build with the parent service Makefile,
then run `logserv --log-path=/var/log/punos/system.log`. The parent directory
must already exist. The endpoint is `/run/punos/logserv.sock`.

The IPC owner validates v1 little-endian LogSubmitRequest frames and copies
UTF-8 component/message strings into a FIFO. A single worker opens, appends,
flushes and closes its LogStoreHandle. File I/O never runs on the IPC thread.
The endpoint is created only after the worker confirms successful store open.
Submitted PID/TID and severity are preserved; timestamps are assigned by the
service after validation. As in ipclib v1, clients must be trusted.

Queue limits are 4096 records and 4194304 total string bytes, including the
in-flight append. Exhaustion returns CORE_WOULD_BLOCK. A successful response
acknowledges memory acceptance, not disk durability. No rotation, history IPC,
filtering, retry after store failure or periodic flush is implemented.

SIGTERM/SIGINT stop accepts, attempt pending replies, prohibit further enqueue,
drain accepted records, flush/close on the worker, join, and release IPC/sync
resources. Append/flush failure marks Failed, wakes the owner and exits with
code 1; startup failure exits with code 3 before exposing the endpoint. Failed
storage cannot durably log its own failure; initserv observes its exit status.
Technical diagnostics use the same bounded queue; overflow diagnostics are
deferred until capacity becomes available. Malformed payload contents are not
included in diagnostics.

`make check` verifies allocation failures and queue cleanup in a freestanding
unit executable. Parent `make -C System/SystemServices check` also runs real
Linux integration tests, including thread ownership of every store operation,
record ordering/persistence, both queue limits and injected append failure.
Test instrumentation is never linked into the production executable.
