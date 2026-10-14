# IOserv

Single-threaded input service. Its wire contract is in
`DevDoc/SystemServices/base/IOserv.md` and public numeric constants are in
`include/ioserv/protocol.h`. Every device is an explicit argument:

```text
IOserv --device=/dev/input/eventN [--device=/dev/input/eventN ...]
```

The default platform registry disables the service until a platform supplies
real arguments. `make check` runs independent wire and per-device state tests,
plus process tests against fake input devices linked only into test executables.
The production ELF links the real inputlib. The intended GUI client is windowserv in its
separate branch; no GUI client code is included here.
