# System Services integration

The appserv snapshot is built against the CRT, Syscall Wrappers, System Libs
and ipclib in this PUNos checkout. Its upstream dependency snapshot and CI are
not duplicated here. No network access or cross-repository token is required
to build a system image.

```sh
make -C System/SystemServices all
make -C System/SystemServices check
./System/scripts/build-initramfs.sh
```

The image includes `/punos/bin/appserv` and `/punos/bin/appserv-selftest`.
The existing initserv smoke-test stub runs the self-test at boot. It does not
start the production service: genuine initserv and logserv are not implemented
in this platform revision. In particular, there is no fake journal server in
the production image. Host integration tests use a test logserv peer only.

## Registry and catalogue

`platform_registry.json` is the platform-owned **build-time** registry input.
The appserv service record describes its installed path and logserv dependency;
`enabled: false` explicitly records that runtime activation is pending. The
current smoke-test stub does not interpret service records. When genuine
initserv is implemented, it must consume this same input, enforce readiness of
logserv and honor service enablement before launching appserv.

`applications.json` contains appserv's public metadata and fixed launch data.
Both native application lists are empty by default. To add an application,
register its exact ID/executable under `native_apps` in the platform registry,
add the matching metadata record, and include its executable in the image.
Every registered native application must terminate on SIGTERM. These files are
not runtime manifests and are not read by appserv after startup.

The shared header is generated at
`appserv/build/generated/platform_registry.h`. It exports the common SHA-256
`PLATFORM_REGISTRY_ID` and native launch specs for future initserv integration.
Build-time generation checks exact ID/path correspondence. appserv also checks
the embedded catalogue before creating `/run/punos/appserv.sock`.

## Activation boundary

Packaging and boot self-tests do not imply an active application service.
Actual activation additionally requires genuine logserv at
`/run/punos/logserv.sock`, initserv startup/ownership of appserv after logserv
readiness, and Desktop/appframework clients using its v1 IPC. Enabling the
record alone does not make the current boot stub start services. Keep the
record disabled until those dependencies exist.
