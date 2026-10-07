# compositorserv

GUI compositor service for PUNos.

## Implemented

- IPC endpoint and event loop;
- client connection handling;
- surface create/destroy lifecycle;
- shared-memory buffer import and validation;
- XRGB8888 and ARGB8888 buffer support;
- surface attach, damage, opaque region and frame requests;
- atomic surface commit;
- pending/current surface state;
- monotonically increasing commit serial validation;
- scene graph;
- SurfaceBindToken validation;
- scene geometry, visibility, layer/stack and output assignment;
- software composition;
- clipping and alpha blending;
- connection to displayserv;
- display presenter binding;
- final shared-memory output frame creation;
- frame presentation through GUI_DISPLAY_PRESENT;
- frame_done events after presentation;
- buffer_released events when buffers are no longer used;
- cleanup of mappings, memory and file descriptors;
- cleanup on client disconnect and error paths.

## Tests

The runtime tests cover:

- surface create/destroy;
- valid and invalid buffer import;
- attach/damage/commit;
- pending/current state;
- commit serial validation;
- scene graph operations;
- software rendering;
- XRGB8888/ARGB8888 pixel composition;
- resource cleanup;
- compositorserv/displayserv integration;
- final frame presentation;
- frame_done;
- buffer_released;
- file descriptor leak checks.

Run tests with:

```bash
make -C System/SystemServices/compositorserv check