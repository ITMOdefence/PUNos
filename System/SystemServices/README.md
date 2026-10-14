# System Services

This tree contains appserv, logserv, initserv, camserv and IOserv built on the same PUNos CRT,
Syscall Wrappers, System Libs and Special Libs. No network access is needed:

```sh
make -C System/SystemServices all
make -C System/SystemServices check
./System/scripts/build-initramfs.sh
```

`platform_registry.json` is the sole build-time registry owned by initserv.
It contains enabled/critical/restart policy and fixed ProcessLaunch data for
services, and a separate native_apps allowlist. `applications.json` supplies
appserv metadata and fixed application arguments/environment. Native apps are
never launched by initserv. Both generators derive the identical SHA-256
PlatformRegistryId from the whole canonical registry and reject native app
ID/path mismatches. Files are not loaded at runtime.

The default registry enables real logserv and appserv. The device services have
disabled entries with no guessed hardware paths or camera parameters. An
explicit platform registry can enable them with `/dev/videoN` and
`/dev/input/eventN` arguments. `PUNOS_REGISTRY=/absolute/path.json`
`System/scripts/build-initramfs.sh` compiles that registry and installs only
enabled device services. The default image is production mode and excludes
all self-tests. `PUNOS_IMAGE_MODE=smoke` adds the existing boot tests for QEMU.
Boot installs logserv and appserv and
the genuine initserv at `/punos/bin/`; normal `/init` starts the supervisor,
which confirms logserv readiness before launching appserv. No fake logging peer
is present in production. Desktop/appframework can use appserv's v1 endpoint
when implemented; the default native app catalogue is empty.

CI uses `/init-smoke`: it runs freestanding library/service self-tests in the
explicit `initserv-smoke` executable, then starts the same real initserv and
uses a test IPC probe to confirm appserv actually serves requests. Normal
`/init` does not execute those test programs. Both paths mount the same filesystems.
Tests inject failures only in separately linked binaries and cover real native
threads, futexes, IPC, journal storage and supervised children.

The activation PR is based on the prior appserv integration branch until that
PR is merged. Service source snapshots are also distributed as private
ITMOdefence/initserv and ITMOdefence/logserv repositories through their own PRs.
