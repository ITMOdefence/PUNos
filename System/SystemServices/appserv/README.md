# appserv platform snapshot

Source snapshot of [ITMOdefence/appserv](https://github.com/ITMOdefence/appserv),
integrated into PUNos. The source commit and import exclusions are recorded in
`upstream.json`. This copy intentionally excludes the upstream vendored PUNos
dependencies; all builds use the current platform's libraries.

From the PUNos repository root:

```sh
make -C System/SystemServices all
make -C System/SystemServices check
```

The platform-owned registry and metadata live one directory above this module.
See `../README.md` for image packaging, registry ownership and the production
runtime activation. The default catalogue contains no applications.

appserv is a static freestanding Linux x86_64 ELF. One event loop owns Unix
socket clients, outgoing queues and child ProcessHandles. It supports v1 list,
get_info, launch, stop and get_status at `/run/punos/appserv.sock`, with strict
little-endian payloads, UTF-8 validation and ErrorCode results. Executable paths,
arguments and environment always come from the build-time catalogue, never IPC.
Instance IDs are not PIDs and are never reused during one service run.

Monitoring polls children every 1000 ms. Exited states remain available until
service shutdown, and applications are never restarted automatically. SIGTERM
shutdown sends SIGTERM to all live children before waiting for any. Production
code never uses SIGKILL or child stop timeouts. The production initserv starts logserv before this service. Startup requires logserv and a
valid generated catalogue before exposing the endpoint. The pinned libprocess
reports successful fork; an execve failure is subsequently observed as exit 127.

Host tests cover the generator, memory/process failures and real child ELF/IPC
integration. The freestanding `build/service_test` is also packaged as the
platform's `appserv-selftest`; its process fakes exist only in that test binary.
