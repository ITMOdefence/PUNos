# uilib

Client-side GUI library for PUNos applications. `UiContextHandle` owns the
connections to compositorserv and windowserv and registers their non-blocking
descriptors in the `appframework` event loop.

Current implementation covers context initialization, connection event
dispatch, asynchronous surface/window creation, configure/lifecycle/navigation
delivery, shared-memory buffer pools, damage/present, frame completion and
deterministic shutdown. Applications receive close, touch, pointer, key and
visibility events and can publish, hide, retitle or answer close requests.
`examples/hello_ui.c` demonstrates a complete application-side first frame.

```sh
make -C System/ApplicationLayer/uilib check
```
