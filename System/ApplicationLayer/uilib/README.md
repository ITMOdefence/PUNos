# uilib

Client-side GUI library for PUNos applications. `UiContextHandle` owns the
connections to compositorserv and windowserv and registers their non-blocking
descriptors in the `appframework` event loop.

Current implementation covers context initialization, connection event
dispatch and deterministic shutdown. Views, surfaces and buffer pools are the
next implementation layer.

```sh
make -C System/ApplicationLayer/uilib check
```
