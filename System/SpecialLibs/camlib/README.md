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
if (status == CAMERA_READ_STATUS_FRAME_READY) {
    camera_frame_release(camera, &frame);
}
camera_stop(camera);
camera_close(&camera);
string_destroy(&path);
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
- **Frame identity includes data, size, timestamp and sequence.** A matching
  buffer id alone does not authorise a release.
- **Stopping returns the camera to Open.** Configure again before restarting.
  A stop error makes the handle usable only for closing (DeviceLost).
- **Only discrete configurations are supported.**

## Tests

- `tests/abi_compile.c`: compile-time checks of types and constants.
- `tests/camera_runtime.c`: runtime checks of camera lifecycle, configuration
  and error paths.
- `tests/memory_fake.c`: checks that every allocation is freed with its original
  size, including a filtered configuration list spanning several pages.
- Mutation checks: foreign frame 122, stale sequence 128, Streaming query 142,
  DeviceLost fd 145, rejected close 151, configuration allocation size 156.
- The current revision still needs native Linux/GCC and QEMU validation; see
  the validation record in `../README.md`.
