# libv4l2

System Lib for Linux V4L2 video capture with streaming I/O and MMAP
buffers (`sys/v4l2`).

Specification: `DevDoc/SystemLibs/libv4l2.md`. Depends on
[libcore](../libcore/README.md), [libmem](../libmem/README.md) (the handle
and its buffer/config-list bookkeeping), [libstr](../libstr/README.md) (the
device path) and [libtime](../libtime/README.md) (`MonotonicTime` for
buffer timestamps). `libpunos_v4l2.a` calls only `open()`, `close()`,
`ioctl()`, `mmap()` and `munmap()`, as contract 1 requires.

## Build and test

From the repository root:

```bash
make -C System/SystemLibs/libv4l2 check
```

Like `libinputdevice`, this library is inherently hardware-bound; see
"Tests" and "Verifying the hardware-backed path" below for what an
unprivileged `make check` does and does not reach.

Outputs:

```text
System/SystemLibs/libv4l2/build/libpunos_v4l2.a   # the library
System/SystemLibs/libv4l2/build/v4l2_runtime      # runtime test / QEMU self-test
```

## Usage

```c
#include <sys/v4l2.h>

ErrorCode grab_one_frame(const String *path, Bytes *out_frame)
{
    V4l2DeviceConfig config = { path };
    V4l2DeviceHandle device;
    V4l2StreamConfigList configs;
    V4l2StreamConfig actual;
    ErrorCode result = v4l2_open(&config, &device);

    if (result != CORE_OK) return result;
    result = v4l2_get_capture_configurations(device, &configs);
    if (result != CORE_OK || configs.count == 0) { (void)v4l2_close(&device); return result; }
    configs.items[0].max_frame_bytes = 0; /* a request, not an actual config */
    result = v4l2_configure_capture(device, &configs.items[0], &actual);
    (void)v4l2_stream_config_list_destroy(&configs);
    if (result != CORE_OK) { (void)v4l2_close(&device); return result; }
    result = v4l2_start_capture(device);
    /* ... v4l2_dequeue_buffer() in an event loop, via v4l2_get_fd() ... */
    (void)v4l2_close(&device);
    return result;
}
```

Link order:

```text
crt0.o <objects> libpunos_v4l2.a libpunos_time.a libpunos_str.a libpunos_mem.a libpunos_core.a libpunos_crt.a libpunos_syscall.a
```

## Implementation decisions

- **The V4L2 ABI is hand-written**, not included from a host header: PUNos
  is freestanding (`-nostdinc`) and has no `<linux/videodev2.h>`. Every
  `Raw*` struct in `src/v4l2.c` (`RawV4l2Capability`, `RawV4l2Fmtdesc`,
  `RawV4l2Frmsizeenum`, `RawV4l2Frmivalenum`, `RawV4l2PixFormat`,
  `RawV4l2Format`, `RawV4l2RequestBuffers`, `RawV4l2Buffer`,
  `RawV4l2StreamParm`) was checked field-by-field against
  `/usr/include/linux/videodev2.h` on the build host, using a small
  host-side C program that printed `sizeof()` and `offsetof()` for the real
  kernel structs (not just read from comments in the header) — because the
  encoded `VIDIOC_*` ioctl request numbers bake the struct size in, a wrong
  size means the *wrong ioctl number entirely*, not just a misread field.
  Two mismatches this caught: `struct v4l2_buffer` needs 4 bytes of padding
  before its `timestamp` (a `struct timeval`, 8-byte aligned) that a naive
  field-by-field translation would miss, giving 84 bytes instead of the
  real 88; and `struct v4l2_format` is 208 bytes, not the 204 a same-looking
  union would suggest, because one of its *other*, unused members
  (`v4l2_window`, which this library never touches) contains a pointer that
  forces 8-byte alignment on the whole union. `RawV4l2Format` reproduces
  that with an explicit padding field so its union still starts at the
  right offset and the struct still totals 208 bytes, even though only
  `fmt.pix` is ever read or written.
- **Every ioctl struct is zeroed before use**, not just the fields this
  library sets. Real V4L2 drivers document "must be set to 0 by
  applications" for reserved fields throughout the header, and some fields
  this library never sets (e.g. `v4l2_buffer.flags`) are read as input by
  `VIDIOC_QBUF` for less common paths (request-fd handling); leaving them
  as uninitialised stack contents would be non-deterministic.
- **`VIDIOC_TRY_FMT`, not `VIDIOC_S_FMT`, negotiates `sizeimage` while
  enumerating configurations.** `TRY_FMT` and `S_FMT` are both just
  `ioctl()` calls (contract 1 allows any of them), but `TRY_FMT` does not
  change the device's actual format, so
  `v4l2_get_capture_configurations()` can probe every combination's
  `sizeimage` without disturbing whatever format is currently configured -
  matching the specification's "determined without starting capture"
  wording for contract 12.
- **`v4l2_get_capture_configurations()` fails entirely, not partially, on a
  non-discrete range.** Contract 12 says the library "returns
  `CORE_UNSUPPORTED`" (singular result) if the driver reports a continuous
  or stepwise size or frame-interval type anywhere; this library stops
  enumerating and frees the partial list rather than silently omitting the
  offending entries.
- **Frame rate uses `VIDIOC_S_PARM`/`VIDIOC_G_PARM`**, a separate ioctl pair
  from the pixel format (`VIDIOC_S_FMT`). `v4l2_configure_capture()` reads
  the rate back via `G_PARM` after setting it, since a driver can adjust a
  requested rate to the nearest one its hardware actually supports; this is
  the same "read back what was actually applied" idea contract 11 already
  requires for pixel format, width and height.
- **Re-configuring an already-configured device tears down its old MMAP
  buffers first** (unmap each one, `VIDIOC_REQBUFS` with `count = 0`) before
  requesting new ones, so calling `v4l2_configure_capture()` a second time
  on the same handle does not leak the first buffer set.
- **A partially-mapped buffer set is unmapped safely.** If `mmap()` fails
  partway through `v4l2_configure_capture()`'s buffer loop, the buffers
  that were never reached are still zero-filled (`{ NULL, 0 }`, from
  `memory_allocate()`'s zero-fill guarantee) and are skipped during
  cleanup — calling `munmap(NULL, 0)` unconditionally would itself fail
  with `EINVAL` and mask the real error.
- **`ENODEV` from `VIDIOC_DQBUF` means `DeviceLost`**, the same convention
  `libinputdevice` uses for `read()`; any other `VIDIOC_DQBUF` failure is a
  genuine `ErrorCode` error, not a status.
- **The growable `V4l2StreamConfigList`** is trimmed to its exact final
  size with one last `memory_reallocate()` before being returned, instead
  of leaving whatever spare capacity the doubling growth strategy allocated
  — `v4l2_stream_config_list_destroy()` then only ever needs
  `configurations->count`, not a separately tracked capacity, to free it
  correctly.

## Tests

Capture lifecycle checks cover a partial QBUF failure followed by a start
retry, drivers allocating extra buffers, stale/double buffer returns, and
STREAMOFF failure. Configuring during capture and starting twice are rejected.
Closing consumes the descriptor before unmapping so DMA cannot target unmapped
memory. Timestamps are sampled immediately before DQBUF; a clock error therefore
cannot consume a frame. After DeviceLost, only closing is allowed.

- `tests/abi_compile.c`: `V4L2_BUFFER_COUNT`/`V4L2_MIN_*`, the ABI numbering
  this library chose for the unpinned `V4l2DequeueStatus`, and the layout
  of `V4l2StreamConfig` and `V4l2Buffer`.
- `tests/v4l2_runtime.c` adapts to the machine it runs on:
  - `1–15`: always run — `v4l2_open()` against a path guaranteed not to
    exist (`CORE_NOT_FOUND`), every `NULL`-argument check for every
    operation, and that a request with `max_frame_bytes != 0` is rejected
    before a device is even needed.
  - `20–45`: only if a real, read/write-openable `/dev/video*` exists (a
    real capture device, or a virtual one such as `v4l2loopback`) — the
    full `open`/`get_fd`/`get_capture_configurations`/`configure_capture`/
    `start_capture`/`dequeue_buffer`/`queue_buffer`/`stop_capture`/
    `stream_config_list_destroy`/`close` success path, checked against it.
    A non-blocking dequeue accepts both `NoBuffer` (nothing captured yet,
    the expected case for an idle virtual device) and `BufferReady`.
  - If no usable `/dev/video*` exists at all, only `1–15` run.

### Verifying the hardware-backed path

This machine has no `/dev/video*` device at all (checked with
`ls /dev/video*`), so an unmodified `make check` here only reaches checks
`1–15`. Unlike `libinputdevice`'s `/dev/input/event*` (present but
permission-restricted, so `sudo` was enough), there is no real or virtual
V4L2 device on this host to open at all, with or without extra privileges.

Checks `20–45` were instead verified by construction: every `Raw*` struct's
size and the field offsets the code actually reads were cross-checked
against the real kernel header (see "Implementation decisions" above), and
the ioctl sequencing follows the standard V4L2 streaming I/O flow
(`VIDIOC_S_FMT` → `VIDIOC_S_PARM` → `VIDIOC_REQBUFS` → `VIDIOC_QUERYBUF` +
`mmap()` per buffer → `VIDIOC_QBUF` × N → `VIDIOC_STREAMON` →
`VIDIOC_DQBUF`/`VIDIOC_QBUF` in a loop → `VIDIOC_STREAMOFF` →
`VIDIOC_REQBUFS` with `count = 0`) documented in the V4L2 API. Running
`tests/v4l2_runtime.c`'s `20–45` block against a real capture device (a
webcam) or a virtual one (the `v4l2loopback` kernel module, installable
with `sudo apt-get install v4l2loopback-dkms` and `sudo modprobe
v4l2loopback`) would exercise this path directly; that was not done here
without the repository owner's go-ahead, since it changes the host
machine's installed software and loaded kernel modules, not just files
inside this repository.

Checks `1–15` were checked against a deliberately broken `v4l2_open()`
(the `open()` failure path replaced with `return CORE_OK`, leaving
`*out_device` untouched instead of reporting the error): check `3`
(`CORE_NOT_FOUND` for a guaranteed-missing path) failed as expected, then
passed again once reverted.
