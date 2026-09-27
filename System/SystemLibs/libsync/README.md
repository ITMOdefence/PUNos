# libsync

System Lib for thread synchronization (`sys/sync`): mutex, manual-reset
event and condition variable.

Specification: `DevDoc/SystemLibs/libsync.md`. Depends on
[libcore](../libcore/README.md), [libmem](../libmem/README.md) (handle
storage) and [libtime](../libtime/README.md) (`DurationMs`/`Deadline`).
`libpunos_sync.a` itself calls only the `futex()` Syscall Wrapper, as
contract 1 requires; [libthread](../libthread/README.md) is listed as a
Makefile dependency only so `tests/sync_runtime.c` can spawn real threads to
create contention — `src/sync.c` never calls a libthread function.

## Build and test

From the repository root:

```bash
make -C System/SystemLibs/libsync check
```

The test spawns real threads and deliberately contends a mutex, an event and
a condition variable. Run it many times in a row, as with libthread and
libprocess (`System/SystemLibs/README.md`): futex races do not show up every
time.

```bash
for i in $(seq 30); do System/SystemLibs/libsync/build/sync_runtime >/dev/null || echo "fail $i"; done
```

Outputs:

```text
System/SystemLibs/libsync/build/libpunos_sync.a   # the library
System/SystemLibs/libsync/build/sync_runtime      # runtime test / QEMU self-test
```

## Usage

```c
#include <sys/sync.h>

Mutex counter_lock;
i32 shared_counter;

ErrorCode increment(void)
{
    ErrorCode result = mutex_lock(counter_lock, SYNC_WAIT_FOREVER);

    if (result != CORE_OK) {
        return result;
    }
    shared_counter++;
    return mutex_unlock(counter_lock);
}
```

Link order:

```text
crt0.o <objects> libpunos_sync.a libpunos_thread.a libpunos_time.a libpunos_mem.a libpunos_core.a libpunos_crt.a libpunos_syscall.a
```

(drop `libpunos_thread.a` from that line for a program that does not also use
libthread directly).

## Implementation decisions

- **Mutex** is the classic three-state futex mutex from Ulrich Drepper's
  "Futexes Are Tricky": `0` unlocked, `1` locked with no waiters, `2` locked
  with at least one waiter. The uncontended path is a single
  compare-and-swap with no syscall; only a thread that finds the mutex
  already held calls `futex(FUTEX_WAIT)`, and only `unlock()` on a mutex
  that was ever contended calls `futex(FUTEX_WAKE)`.
- **No owner tracking.** Contracts 4 and 5 ("the mutex is not recursive",
  "must be unlocked by the thread that locked it") are caller
  preconditions, not runtime-checked errors, the same way an ordinary futex
  mutex without priority inheritance works: re-locking from the same thread
  deadlocks instead of returning an error, and unlocking from the wrong
  thread is undefined instead of detected. `mutex_destroy()` likewise does
  not check contract 6 (unlocked, no waiters); it just releases the memory.
- **SyncEvent** is a single `u32` flag: `0` not signalled, `1` signalled.
  `event_signal()` only calls `futex(FUTEX_WAKE)` on the `0 -> 1`
  transition, since nothing can still be validly blocked on "value is 0"
  once it is already `1`. Because the flag stays `1` until `event_reset()`
  (contract 7), a thread that calls `event_wait()` after the signal already
  happened simply never blocks — there is no lost-wakeup window between
  "decide to wait" and "start waiting", unlike an auto-reset event.
- **Condition** is a `u32` generation counter, not a wait queue. A waiter
  reads the counter before releasing the mutex; every `condition_signal_one`
  /`condition_signal_all()` increments it. This closes the same lost-wakeup
  window: any signal from the moment the waiter reads the counter onward is
  visible, either as an immediate `EAGAIN` from `futex(FUTEX_WAIT)` (the
  counter already moved) or as a real wake once the waiter is asleep.
- **`condition_wait()` always relocks the mutex before returning** —
  whether it was woken, timed out, or `futex()` failed unexpectedly — the
  same guarantee POSIX gives `pthread_cond_timedwait()`. The relock itself
  waits forever (`SYNC_WAIT_FOREVER`); the specification does not say what
  a *second* timeout during that relock would even mean.
- **Timeouts share one `wait_once()` helper** used by `mutex_lock()`,
  `event_wait()` and `condition_wait()`'s inner wait. It converts the
  remaining time on a `Deadline` (from `libtime`) to a relative
  `struct timespec` for `futex()`'s timeout argument on every retry, so a
  spurious wake or `EINTR` do not restart the clock. `SYNC_WAIT_FOREVER`
  skips all deadline bookkeeping and passes a `NULL` timeout to `futex()`.
- **Wake count.** Waking "everyone" passes `0x7fffffff`, not `UINT32_MAX`,
  to `futex(FUTEX_WAKE)`'s count argument, to stay clear of the kernel's
  internal (signed) wake counter.
- **No dependency on wrapper code beyond the contract.** `libpunos_sync.a`
  calls only `futex()`, as contract 1 requires.

## Tests

- `tests/abi_compile.c`: `SYNC_WAIT_FOREVER` is a `DurationMs` with the
  `u64` maximum value; every handle is pointer-sized.
- `tests/sync_runtime.c`, check groups: `1–10` mutex creation and
  argument checks, `20–24` four threads doing 20000 protected increments
  each on a shared counter (an undercount means the mutex let two threads in
  at once), `30–42` a timed `mutex_lock()` that must time out while another
  thread holds the mutex and must then succeed once it is released, `50–62`
  event basics including that an already-signalled event returns
  immediately even with a `0` timeout, `65–70` one `event_signal()` waking
  four waiting threads, `75–90` a single `condition_signal_one()` waking one
  waiter through a mutex-protected flag, `95–104` `condition_signal_all()`
  waking four waiters, `110–118` that `condition_wait()` relocks the mutex
  even after `CORE_TIMEOUT`.
- The test ran clean 30 times in a row on the host. It was also checked
  against a deliberately broken `mutex_lock()` (the uncontended fast path
  replaced with an unconditional `return CORE_OK`, so no thread ever really
  held the mutex): check `23` (the four-thread increment count) failed on
  the very first run, as expected — four threads doing non-atomic
  read-modify-write increments with no real exclusion reliably lose updates.
