# camserv

Single-threaded camera service. Its wire contract is in
`DevDoc/SystemServices/abilities/camserv.md` and its public numeric constants
are in `include/camserv/protocol.h`. The device must be supplied explicitly:

```text
camserv --device=/dev/videoN --format=Yuyv|Mjpeg|Nv12 --width=N --height=N --fps=N
```

The default platform registry disables the service until a platform supplies
real arguments. `make check` runs an independent wire test and process tests
against a fake camera linked only into the test executable. The production ELF
links the real camlib.
