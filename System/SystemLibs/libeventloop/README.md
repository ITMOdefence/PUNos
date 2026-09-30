# libeventloop

System Lib for waiting on file descriptors, timers and other threads in one
loop (`sys/eventloop`).

Specification: `DevDoc/SystemLibs/libeventloop.md`. Depends on
[libcore](../libcore/README.md), [libmem](../libmem/README.md) (the growable
source table and the reusable `epoll_wait()` scratch buffer) and
[libtime](../libtime/README.md) (`DurationMs`). `libpunos_eventloop.a` itself
calls only `epoll_create1()`, `epoll_ctl()`, `epoll_wait()`, `eventfd()`,
`timerfd_create()`, `timerfd_settime()`, `read()`, `write()` and `close()`,
as contract 1 requires; [libthread](../libthread/README.md) is a Makefile
dependency only so `tests/eventloop_runtime.c` can call `eventloop_wake()`
from a real second thread (contract 9) — `src/eventloop.c` never calls a
libthread function.

## Build and test

From the repository root:

```bash
make -C System/SystemLibs/libeventloop check
```

The test has no `pipe()` Syscall Wrapper to build a caller-owned readable/
writable fd, so it uses `eventfd()` directly (through the Syscall Wrappers
the test is free to call) to stand in for one. Outputs:

```text
System/SystemLibs/libeventloop/build/libpunos_eventloop.a   # the library
System/SystemLibs/libeventloop/build/eventloop_runtime      # runtime test / QEMU self-test
```

## Usage

```c
#include <sys/eventloop.h>

ErrorCode watch_and_wait(EventLoopHandle loop, i32 fd)
{
    EventSourceId source;
    Event events[8];
    usize count;
    ErrorCode result = eventloop_add_fd(loop, fd, EVENT_READABLE, &source);

    if (result != CORE_OK) {
        return result;
    }
    return eventloop_wait(loop, events, 8, EVENTLOOP_WAIT_FOREVER, &count);
}
```

Link order:

```text
crt0.o <objects> libpunos_eventloop.a libpunos_thread.a libpunos_time.a libpunos_mem.a libpunos_core.a libpunos_crt.a libpunos_syscall.a
```

(drop `libpunos_thread.a` for a program that does not also use libthread
directly).

## Implementation decisions

- **`EventMask` bits equal the corresponding `EPOLL*` bits** (`EVENT_READABLE`
  = `EPOLLIN` = `1`, `EVENT_WRITABLE` = `EPOLLOUT` = `4`, `EVENT_ERROR` =
  `EPOLLERR` = `8`, `EVENT_HANGUP` = `EPOLLHUP` = `16`), so translating
  `struct epoll_event.events` to and from `EventMask` is a plain bitwise
  `AND` with no lookup table. The specification does not pin these numbers.
- **`EventSourceKind` numbering** (`FileDescriptor` = `0`, `Timer` = `1`,
  `Wake` = `2`) follows the order the specification's table lists them, the
  same convention `libfile` uses for its unpinned enums.
- **One source table, keyed by `EventSourceId`.** Every registered source
  (an external fd, an owned timerfd, or the internal wake eventfd) is one
  record `{ id, fd, kind }` in a flat array grown through `libmem`
  (`memory_reallocate()`, doubling); `EventSourceId` is simply the `data.u64`
  stashed in `epoll_ctl()`'s `epoll_data_t`, so `eventloop_wait()` reads it
  straight back from the kernel without guessing. A linear scan is enough:
  an event loop is expected to hold a handful of sources, not thousands.
- **A reusable scratch buffer for `epoll_wait()`.** Allocating a temporary
  `struct epoll_event` buffer on every `eventloop_wait()` call would mean an
  `mmap()`/`munmap()` pair (libmem contract 10) on every single wait of what
  is meant to be a hot loop. Instead `EventLoopHandle` keeps one scratch
  block that only ever grows (`memory_reallocate()`, never shrinks) to the
  largest `events_capacity` seen so far.
- **`eventloop_wait()` drains a ready Timer or Wake source's 8-byte counter
  itself** before reporting it, since both `timerfd` and `eventfd` are
  level-triggered here: without draining, epoll would keep reporting the
  same expiration or wake forever. This discards the counter's numeric
  value (how many periods elapsed, or the wake count) without acting on it,
  which is not the same as "interpreting the source's data" (contract 10) —
  that contract is about a `FileDescriptor` source's actual payload, which
  this library never reads at all; the caller reads it themselves once
  `eventloop_wait()` reports it ready.
- **`eventloop_create_timer(loop, 0, ...)` still fires.** `timerfd_settime()`
  treats an all-zero `it_value` as "disarm the timer", but the
  specification's `delay_ms == 0` means "fire as soon as possible". This
  library detects that case and substitutes 1 nanosecond instead of 0.
- **The internal Wake source can never be removed through this API**: its
  `EventSourceId` is never handed to the caller by `eventloop_create()`
  (only through an `Event` from `eventloop_wait()`, which the caller cannot
  feed back into `eventloop_remove_source()` meaningfully, since nothing
  in the specification says they should).
- **`eventloop_wake()`'s `EAGAIN`** (the eventfd counter would overflow
  because a wake is already pending) is not an error: the pending wake
  still delivers, so there is nothing left to signal.
- **No dependency on wrapper code beyond the contract.** `libpunos_eventloop.a`
  calls only the nine wrappers contract 1 names.

## Tests

- `tests/abi_compile.c`: the `EVENT_*` bit values, `EVENTLOOP_WAIT_FOREVER`,
  the ABI numbering of `EventSourceKind`, and the layout of `Event`.
- `tests/eventloop_runtime.c`, check groups: `1–6` create/close and argument
  checks, `10–30` `eventloop_add_fd()`/`eventloop_update_fd()`/
  `eventloop_remove_source()` against a real eventfd standing in for a
  caller-owned descriptor (including that the library never reads its data:
  the test drains its own counter after the fact), `35–49` that
  `events_capacity` is respected with two always-ready sources and one slot,
  plus the timeout-too-large and null-argument checks, `55–63` a one-shot
  timer, including `delay_ms == 0` firing instead of disarming, `68–72` a
  periodic timer firing at least three times, `80–86` `eventloop_wake()`
  called from a second, real thread unblocking a `EVENTLOOP_WAIT_FOREVER`
  wait — a bug here hangs the test instead of merely failing a check.
- The test was checked against the zero-delay timer fix removed (a
  deliberately broken `eventloop_create_timer()` that let `timerfd_settime()`
  disarm the timer for `delay_ms == 0`): check `60` failed, as expected —
  the one-shot timer never fired and the wait timed out with `count == 0`.
