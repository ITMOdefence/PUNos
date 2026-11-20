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

Platform subscription v2 (`CAM_SUBSCRIBE` payload u16 little-endian = 2)
returns one snapshot through `CAM_FRAME_SHARED` (type 11). Its 20-byte metadata
is sequence u64, timestamp_ns u64, byte_count u32, all little-endian. Exactly
one SCM_RIGHTS fd carries shared bytes, up to 16 MiB. The service copies the
V4L2 frame before releasing it, queues a duplicated fd and closes its own
mapping/fd. V2 emits one snapshot per subscription, bounded independently of
client read speed; the client explicitly unsubscribes afterward.

V1 retains inline CAM_FRAME and its 1 MiB IPC payload limit. A v1 subscription
is rejected as unsupported when configured max_frame_bytes exceeds that
limit. The device configuration now permits up to 16 MiB for v2. The common
wire constants are in `include/camserv/wire.h`. No sensor or protocol runtime
tests were executed for this extension during the implementation stage.
