# Special Libs

Freestanding adapters for System Services, implementing the contracts in
`DevDoc/SpecialLibs/`. No libc or host headers are used.

| Library | Public header | Archive |
| --- | --- | --- |
| [inputlib](inputlib/README.md) | `<special/input.h>` | `libpunos_input.a` |
| [ipclib](ipclib/README.md) | `<special/ipc.h>` | `libpunos_ipc.a` |
| [loglib](loglib/README.md) | `<special/log.h>` | `libpunos_log.a` |

```sh
make -C System/SpecialLibs all
make -C System/SpecialLibs check
```

Each library also supports `all`, `check`, `runtime-test`, and `clean`
individually. The shared rules in `System/SystemLibs/library.mk` build the
System Lib dependencies first. Public enums follow the System Libs convention:
type and value names in UPPER_SNAKE_CASE, numbered in specification order.

Runtime tests are installed as `/punos/bin/inputlib-selftest`,
`ipclib-selftest`, and `loglib-selftest` and run by the initserv stub.
The input test substitutes a scripted raw-device adapter; IPC and log tests
use real Unix sockets and files under PID-qualified `/tmp` paths. The caller
owns returned frames/messages/records and destroys them through the library.

Programs link the Special Lib before its System Lib dependencies, then CRT
and Syscall Wrappers, following `System/SystemLibs/README.md`.

## Verification in the Windows development environment

The three libraries, ABI checks, runtime tests, the updated raw-device source
and initserv stub were cross-compiled with Zig/Clang for Linux x86_64 using
freestanding flags and `-Wall -Wextra -Werror`. Archive undefined-symbol checks
confirmed only the specified System Libs and wrappers are referenced, with no
implicit libc calls. All three ELF runtime tests passed in Unicorn with modeled
Linux syscalls (including partial socket I/O/backpressure); this does not verify
Linux kernel integration. Native `make check` and QEMU were unavailable here.

Mutation checks in scratch copies proved the tests detect a wrongly normalized
key (input check 12), an altered IPC message type (IPC check 17), and an altered
log timestamp (log check 7). The implementation files were not modified by these
checks. Run the commands above on Linux and the boot checks in
`System/start-guide.md` for native verification.
