# appframework

Application Layer library of PUNos: the skeleton every PUNos application is
built on — one `AppHandle` with a lifecycle (`app_create` / `app_run` /
`app_destroy`), an event loop for the application's own fds and timers, a
logging client of `logserv` and an application-management client of `appserv`.

Specification: `DevDoc/ApplicationLayer/appframework.md` (including the table
of decisions taken while refining it).

## Build and test

From the repository root:

```bash
make -C System/ApplicationLayer check                       # levels 1 and 2
make -C System/ApplicationLayer/appframework check-system   # level 3
python3 System/ApplicationLayer/appframework/tests/check_mutations.py
```

Output: `System/ApplicationLayer/appframework/build/libappframework.a`.

External event-loop clients register non-owned descriptors with
`app_event_add_fd()`, change read/write interest with `app_event_update_fd()`,
and unregister them with `app_event_remove()`.

## Test levels

| Level | What | Where |
| --- | --- | --- |
| 1 | ABI (types, constants, signatures) and the state/operation matrix | `tests/abi_compile.c`, `tests/lifecycle_runtime.c` |
| 2 | The real `ipclib` and `libeventloop` against fake peers; faults injected with `--wrap` (allocation, wait, `sigaction`, send, flush) | `tests/*_runtime.c`, `tests/fake_peer.h`, `tests/fault_fake.c` |
| 3 | Two `appframework` processes against the real `logserv` and `appserv` on the host | `tests/system/` (`host_integration.py`) |
| 4 | QEMU smoke: every level-2 test runs as `appfw-*-selftest` in the smoke initramfs | `System/Boot/InitservStub`, `System/scripts/build-initramfs.sh` |

`tests/check_mutations.py` breaks one rule at a time in a temporary copy of the
tree and requires the tests to notice.

## Notes for tests

- `libappframework_internal_logserv_endpoint` and
  `libappframework_internal_appserv_endpoint` redirect the clients to a fake
  peer; applications never touch them.
- `test_pump()` (`tests/support.h`) runs the event loop by hand without
  changing the state of the handle.
