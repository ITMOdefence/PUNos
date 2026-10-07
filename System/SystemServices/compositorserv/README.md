# compositorserv

Initial compositorserv service skeleton.

Implemented in this stage:
- IPC endpoint ownership;
- event loop integration;
- accepting multiple IPC clients;
- connection read/write/hangup handling;
- compositor message dispatch boundary;
- controller-token configuration plumbing.

Not implemented yet:
- SurfaceId/BufferId state;
- shared-memory import;
- pending/current commit semantics;
- scene graph;
- software composition;
- displayserv connection/present;
- frame_done/buffer_released.

Default endpoint: `/run/punos/compositorserv.sock`.
The executable currently requires `--controller-token=<64 hex characters>`.
