# windowserv

Mobile window policy service for PUNos.

## Implemented

- IPC endpoint and event loop;
- application and shell client connections;
- `window.create`, `destroy`, `show`, `hide`, `set_title`, `ack_configure`,
  `set_modal`, `surface_rebind` and close response handling;
- shell token binding, activation, navigation and safe inset updates;
- `WindowId` ownership checks and cleanup on disconnect;
- lifecycle states `HIDDEN`, `BACKGROUND`, `FOREGROUND`, `SUSPENDED` and
  `CLOSING`;
- viewport configure events with orientation, scale and safe insets;
- IOserv subscription for keyboard, pointer and touch frames;
- touch, pointer and key routing from normalized IOserv frames;
- scene-controller connection to compositorserv and best-effort scene bind,
  geometry, stack, visibility and remove commands.

## Tests

The runtime test covers window creation, configure delivery, activation,
orientation/safe-area changes, touch routing, foreign-window access denial and
cleanup after client disconnect. It also uses a fake IOserv endpoint to verify
that `io.input_frame` touch data is routed to the foreground window.

Run:

```bash
make -C System/SystemServices/windowserv check
```
