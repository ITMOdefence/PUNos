# libinputdevice

System Lib for raw access to one Linux input subsystem device through
`/dev/input/event*` (`sys/input-device`).

Specification: `DevDoc/SystemLibs/libinputdevice.md`. Depends on
[libcore](../libcore/README.md), [libmem](../libmem/README.md) (the handle
and its reusable read buffer), [libstr](../libstr/README.md) (the device
path) and [libtime](../libtime/README.md) (`MonotonicTime` for event
timestamps). `libpunos_inputdevice.a` calls only `open()`, `close()`,
`read()` and `ioctl()`, as contract 1 requires.

The additive `input_raw_device_get_key_codes()` operation returns the 96-byte
raw EV_KEY code bitmap through EVIOCGBIT(EV_KEY). Special inputlib uses it to
distinguish keyboard keys from mouse buttons without accessing evdev itself.

## Build and test

From the repository root:

```bash
make -C System/SystemLibs/libinputdevice check
```

This library is inherently hardware-bound, so the test adapts to whatever
`/dev/input/event*` it can find on the machine it runs on — see
"Tests" below and, for the parts an unprivileged run cannot reach,
"Verifying the hardware-backed path".

Outputs:

```text
System/SystemLibs/libinputdevice/build/libpunos_inputdevice.a   # the library
System/SystemLibs/libinputdevice/build/input_device_runtime     # runtime test / QEMU self-test
```

## Usage

```c
#include <sys/input-device.h>

ErrorCode print_capabilities(const String *path)
{
    InputRawDeviceConfig config = { path };
    InputRawDeviceHandle device;
    InputRawDeviceInfo info;
    ErrorCode result = input_raw_device_open(&config, &device);

    if (result != CORE_OK) {
        return result;
    }
    result = input_raw_device_get_info(device, &info);
    (void)input_raw_device_close(&device);
    return result;
}
```

Link order:

```text
crt0.o <objects> libpunos_inputdevice.a libpunos_time.a libpunos_str.a libpunos_mem.a libpunos_core.a libpunos_crt.a libpunos_syscall.a
```

## Implementation decisions

- **The Linux evdev ABI is hand-written**, not included from a host header:
  PUNos is freestanding (`-nostdinc`) and has no `<linux/input.h>`.
  `struct RawInputEvent` mirrors the kernel's `struct input_event` on
  64-bit Linux exactly (a 16-byte `struct timeval` — two 8-byte `long`s —
  followed by `type`/`code`/`value`, 24 bytes total) and `struct RawInputId`
  mirrors `struct input_id` (4 `u16`s, 8 bytes). Both were checked against
  `/usr/include/linux/input.h` on the build host. `EVIOCGID` and
  `EVIOCGBIT` are re-derived from Linux's generic ioctl request encoding
  (`asm-generic/ioctl.h`'s dir/type/nr/size packing) rather than copied as
  magic numbers, and `EVIOCGID`'s re-derived value (`0x80084502`) matches
  the well-known constant, which is a useful cross-check that the encoding
  macro itself is right.
- **The kernel's own event timestamp is discarded.** Contract 9 requires
  `InputRawEvent.timestamp` to come from `time_monotonic_now()`, not from
  the kernel — so `struct RawInputEvent`'s `tv_sec`/`tv_usec` fields exist
  only to give the struct the right size and offsets for parsing; nothing
  ever reads them.
- **One `time_monotonic_now()` call per `read()`, not per event.** Multiple
  queued events can arrive in a single `read()` (the kernel batches them),
  and `libtime` truncates readings to whole milliseconds, so calling the
  clock once per batch instead of once per event gives the same
  observable timestamps in the near-totality of cases while avoiding
  redundant syscalls. `libv4l2`'s specification uses the same "immediately
  after extracting the buffer" phrasing for a single buffer, which is the
  same idea applied to a batch here.
- **Capabilities come from one `EVIOCGBIT(0, ...)` call**, which asks for
  the bitmask of event *types* (`EV_SYN`, `EV_KEY`, `EV_REL`, `EV_ABS`, ...)
  the device can produce at all, rather than querying each type's
  individual code bitmap (`EVIOCGBIT(EV_KEY, ...)`, `EVIOCGBIT(EV_REL,
  ...)`, ...). The specification's `InputRawCapabilityMask` only asks
  "does this device support any key/relative/absolute event", which the
  type bitmask already answers in one ioctl instead of three.
- **Bus/vendor/product/version and capabilities are captured once, at
  `input_raw_device_open()` time**, and cached in the handle —
  `input_raw_device_get_info()` never makes a syscall. This is not a
  shortcut: the specification's own wording is "determined at opening"
  ("определённые при открытии").
- **A reusable scratch buffer for `read()`**, grown through
  `memory_reallocate()` and never shrunk, the same pattern
  `libeventloop` uses for its `epoll_wait()` buffer — so a caller that
  polls a device in a loop with a stable `events_capacity` does not pay
  for a fresh `mmap()`/`munmap()` (libmem contract 10) on every call.
- **`ENODEV` from `read()` means `DeviceLost`**; any other `read()` failure
  is passed through `core_error_from_errno()` as a genuine `ErrorCode`
  error, not a status.

## Tests

- `tests/abi_compile.c`: every specification-pinned constant
  (`INPUT_RAW_EVENT_*`, `INPUT_RAW_SYN_*`, `INPUT_RAW_CAP_*`), the ABI
  numbering this library chose for the unpinned `InputRawReadStatus`, and
  the layout of `InputRawDeviceInfo` and `InputRawEvent`.
- `tests/input_device_runtime.c` adapts to the machine it runs on:
  - `1–9`: `input_raw_device_open()` against a path guaranteed not to
    exist (`CORE_NOT_FOUND`) plus every `NULL`-argument check that needs no
    device at all. These always run, on every machine.
  - `20`: if a real `/dev/input/event*` exists but this process cannot
    read it (the common case for an unprivileged `make check` — the nodes
    are `root:input`, mode `0660`), checks that `input_raw_device_open()`
    reports `CORE_PERMISSION_DENIED`. Nothing further runs in this case.
  - `21–35`: if a real device was opened successfully (running as root —
    inside the PUNos initramfs, or through `sudo` on the host — or a
    machine where the caller already has the `input` group), the full
    `input_raw_device_get_fd()`/`get_info()`/`read()`/`close()` success
    path is checked against it, including that the reported capabilities
    only ever contain the three known bits and that a non-blocking read on
    an idle device returns promptly with `NoEvents` (or, tolerated,
    `EventsReady`, since real hardware can genuinely have a pending event).
  - If no `/dev/input/event*` exists at all, only `1–9` run.

### Verifying the hardware-backed path

`make check` alone does not prove checks `21–35` work, since an
unprivileged run only reaches the `20` branch. This was checked directly
against this machine's real input devices:

```bash
sudo System/SystemLibs/libinputdevice/build/input_device_runtime
```

which ran as root and printed `libinputdevice runtime test passed` after
exercising the full success path against `/dev/input/event0`, and inside
the PUNos initramfs during boot (`initserv` and its self-tests run as PID 1
/ root), where the same self-test passes against whatever `/dev/input/
event*` QEMU's PS/2 keyboard/mouse controller (`CONFIG_INPUT_EVDEV`,
`CONFIG_KEYBOARD_ATKBD`, `CONFIG_MOUSE_PS2`, all enabled in
`System/kernel/configs/x86_64.config`) exposes, if any.

No synthetic input-event injection is available to this test (no
`uinput` Syscall Wrapper, and none in the specification's allowed list),
so a genuine "pressing a key produces this exact `InputRawEvent`" scenario
is not exercised; the struct layout is instead pinned at compile time and
the real `ioctl()`/`read()` calls are exercised against real hardware.

Two deliberately broken versions confirmed the `sudo` run actually reaches
the success path and that it depends on the hand-derived ioctl encoding
being right: forcing check `21` to fail made the unprivileged run's result
unaffected (it never reaches `21`) while the `sudo` run failed at exactly
`21`, as expected; separately, encoding `EVIOCGID` with the wrong ioctl
direction bit made the real `ioctl()` call fail against actual hardware —
again caught at check `21`, this time from the unprivileged run passing
(permission denied before the bad ioctl is ever issued) alongside the
`sudo` run failing.
