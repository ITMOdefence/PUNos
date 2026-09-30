# libtime

System Lib for time (`sys/time`): monotonic and wall-clock time, deadlines
and sleeping, all in whole milliseconds.

Specification: `DevDoc/SystemLibs/libtime.md`. Depends on
[libcore](../libcore/README.md).

## Build and test

From the repository root:

```bash
make -C System/SystemLibs/libtime check
```

The test sleeps for about 1.5 seconds in total. Outputs:

```text
System/SystemLibs/libtime/build/libpunos_time.a   # the library
System/SystemLibs/libtime/build/time_runtime      # runtime test / QEMU self-test
```

## Usage

```c
#include <sys/time.h>

/* Poll until ready() returns true or 500 ms pass. */
ErrorCode wait_ready(bool (*ready)(void))
{
    Deadline deadline;
    DurationMs left;
    bool expired = false;
    ErrorCode result = time_deadline_after(500, &deadline);

    while (result == CORE_OK && !ready()) {
        result = time_deadline_expired(deadline, &expired);
        if (result == CORE_OK && expired) {
            return CORE_TIMEOUT;
        }
        if (result == CORE_OK) {
            result = time_sleep(10, &left);
            if (result == CORE_INTERRUPTED) {
                result = CORE_OK; /* the loop sleeps again */
            }
        }
    }
    return result;
}
```

Link order:

```text
crt0.o <objects> libpunos_time.a libpunos_core.a libpunos_crt.a libpunos_syscall.a
```

## Which clock to use

| Type | Linux clock | Use for | Never use for |
| --- | --- | --- | --- |
| `MonotonicTime`, `Deadline` | `CLOCK_MONOTONIC` | timeouts, deadlines, measuring intervals | dates: its zero point is boot |
| `WallClockTime` | `CLOCK_REALTIME` | labelling events (logs) | deadlines: the system time can be changed |

## Implementation decisions

- **Rounding.** Clock readings are truncated to whole milliseconds. The
  remaining time of an interrupted sleep is rounded **up**, so it is never `0`
  while time is left and a loop that sleeps for the remaining time never wakes
  up early.
- **Interrupted sleep.** If a signal handler interrupts `time_sleep()`, it
  returns `CORE_INTERRUPTED` with the time left in `out_remaining`, as the
  specification requires. It does not retry by itself: the caller decides.
  A signal without a handler (default action "ignore") does not interrupt it.
- **`out_*` pointers are required.** `NULL` for `out_remaining`,
  `out_deadline`, `out_expired` or `out_time` returns `CORE_INVALID_ARGUMENT`.
- **Overflow.** `time_deadline_after()` returns `CORE_OVERFLOW` when
  now + duration does not fit in `u64`. Callers that want "wait forever" use
  the `*_WAIT_FOREVER` constants of libeventloop and libsync, not a huge
  deadline.
- **Wall clock before 1970** returns `CORE_OVERFLOW`: it cannot be represented
  in the unsigned `WallClockTime`.
- **Expiry.** A deadline has expired when `now >= deadline`, so
  `time_deadline_after(0, ...)` gives a deadline that has already expired.
- **Only two wrappers.** `libpunos_time.a` calls only `clock_gettime()` and
  `nanosleep()`, as the contract requires. It does not use vDSO, so every
  reading is a real syscall.

## Tests

- `tests/abi_compile.c`: the time types are `u64`, `Deadline` is
  `MonotonicTime`, `TIME_MILLISECONDS_PER_SECOND` is `1000`.
- `tests/time_runtime.c`, check groups: `1–19` clocks, `20–39` sleep,
  `40–59` deadlines, `60–79` millisecond conversions.
  - Sleeps are checked to last *at least* as requested; upper bounds are
    generous (5 s) because QEMU without KVM is slow.
  - The interrupted sleep uses a child process that sends `SIGCHLD` to the
    parent every 100 ms, so it works even if the first signal arrives before
    the sleep starts.
  - The monotonic clock is checked to be below any wall-clock time since 2020,
    which tells the two clocks apart.
  - Rounding is checked on exact values through the internal conversion
    functions declared in `src/internal.h`: a real interrupted sleep cannot
    produce a chosen number of nanoseconds on demand.

The test was checked against 9 deliberately broken versions of `time.c`. All
9 are caught. The first run caught 7; the misses (monotonic clock replaced by
the wall clock, remaining time rounded down) led to check 7 and to the exact
conversion checks 60–75.
