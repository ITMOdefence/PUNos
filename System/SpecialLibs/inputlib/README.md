# inputlib

Special Lib of PUNos (`special/input`): access to individual Linux input
subsystem devices through `/dev/input/event*`.

Specification: `DevDoc/SpecialLibs/inputlib.md`.

## Build and test

From the repository root:

```bash
make -C System/SpecialLibs/inputlib check
```

## Usage

```c
#include <special/input.h>

InputDeviceHandle *device = NULL;
InputDeviceConfig config;
String path;

string_from_c_str("/dev/input/event0", &path);
config.path = &path;
input_device_open(&config, &device);

InputReadStatus status;
InputFrame frame;
input_device_read_frame(device, &status, &frame);
input_frame_destroy(&frame);
input_device_close(&device);
```

## What the library provides

- **Device operations.** `input_device_open`, `input_device_get_fd`,
  `input_device_get_info`, `input_device_read_frame`, `input_frame_destroy`,
  `input_device_close`.
- **Event classification.** Key, RelativeAxis, AbsoluteAxis, SyncReport,
  SyncLost, Raw.
- **Frame grouping.** Events grouped by SYN_REPORT.

## Implementation decisions

- **Uses libinputdevice for raw evdev access.**
- **Classifies events by type/code into InputEventKind.**
- **Groups events into frames terminated by SYN_REPORT.**
- **SYN_DROPPED produces a single SyncLost event.**

## Tests

- `tests/abi_compile.c`: compile-time checks of types and constants.
- `tests/input_runtime.c`: runtime checks of device lifecycle, frame reading
  and error paths.
