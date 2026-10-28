# GUI Protocols

Wire codecs shared by GUI clients and services. The implementation writes and
reads every little-endian field explicitly; native C structure layout is never
sent over IPC.

Implemented display-side slices cover:

- `buffer.import`;
- `surface.damage`;
- `display.bind_presenter`;
- `display.configure`;
- `display.present` and its basic result;
- `display.list`, `display.get_state` and `display.subscribe` requests;
- variable-size `DisplayInfo`, list/state/configure results and
  added/changed/removed events;
- `display.presented`, `display.present_failed` and `display.backend_lost`.

Run `make -C GUI/Protocols compile-check` with the Linux x86_64 toolchain.
