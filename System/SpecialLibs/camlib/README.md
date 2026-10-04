# camlib

Special Lib of PUNos (`special/camera`): raw camera frame capture from a
single Linux V4L2 device for `camserv`.

Specification: `DevDoc/SpecialLibs/camlib.md`.

## Build and test

From the repository root:

```bash
make -C System/SpecialLibs/camlib check
```

## Usage

```c
#include <special/camera.h>

CameraHandle camera = NULL;
CameraConfig config;
String path;

string_from_c_str("/dev/video0", &path);
config.path = &path;
camera_open(&config, &camera);

CameraStreamConfig requested;
requested.format = CAMERA_PIXEL_FORMAT_YUYV;
requested.width = 640;
requested.height = 480;
requested.frames_per_second = 30;
requested.max_frame_bytes = 0;

CameraStreamConfig actual;
camera_configure(camera, &requested, &actual);
camera_start(camera);

CameraReadStatus status;
CameraFrame frame;
camera_dequeue_frame(camera, &status, &frame);
camera_frame_release(camera, &frame);
camera_close(&camera);
```

## What the library provides

- **Camera operations.** `camera_open`, `camera_get_fd`,
  `camera_get_configurations`, `camera_configure`, `camera_start`,
  `camera_dequeue_frame`, `camera_frame_release`, `camera_stop`,
  `camera_close`.
- **Formats.** YUYV, MJPEG, NV12.
- **States.** Open → Configured → Streaming → DeviceLost.

## Implementation decisions

- **Uses libv4l2 for V4L2 access.**
- **CameraFrame is non-owning.** Data points to MMAP buffer.
- **Each frame must be released exactly once.**
- **Only discrete configurations are supported.**

## Tests

- `tests/abi_compile.c`: compile-time checks of types and constants.
- `tests/camera_runtime.c`: runtime checks of camera lifecycle, configuration
  and error paths.
