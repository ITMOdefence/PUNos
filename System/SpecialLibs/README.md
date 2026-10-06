# PUNos Special Libs

Special Libs provide focused adapters used by System Services. Unlike System
Libs, they may encode a concrete subsystem policy or wire protocol.

| Directory | Public import | Purpose |
| --- | --- | --- |
| `ipclib` | `<special/ipc.h>` | Nonblocking local IPC with framed messages and one optional transferred fd |
| `displaylib` | `<special/display.h>` | Adapter from displayserv to the System graphics ABI |

Build all libraries with `make -C System/SpecialLibs`. Runtime tests require
the Linux x86_64 PUNos toolchain or the QEMU development environment.
