# libthread

System Lib for native threads (`sys/thread`): create a thread, wait for it and
get the value its entry function returned.

Specification: `DevDoc/SystemLibs/libthread.md`. Depends on
[libcore](../libcore/README.md) and [libmem](../libmem/README.md).

## Build and test

From the repository root:

```bash
make -C System/SystemLibs/libthread check
```

Outputs:

```text
System/SystemLibs/libthread/build/libpunos_thread.a   # the library
System/SystemLibs/libthread/build/thread_runtime      # runtime test / QEMU self-test
```

## Usage

```c
#include <sys/thread.h>

static i32 worker(void *argument)
{
    u64 *total = argument;

    *total = 42; /* ... real work ... */
    return THREAD_EXIT_SUCCESS;
}

ErrorCode compute(u64 *out_total)
{
    ThreadHandle thread;
    ThreadExitStatus status;
    ErrorCode result = thread_create(worker, out_total, &thread);

    if (result != CORE_OK) {
        return result;
    }
    result = thread_join(&thread, &status); /* exactly once per thread */
    if (result == CORE_OK && status != THREAD_EXIT_SUCCESS) {
        return CORE_IO_ERROR;
    }
    return result;
}
```

Compile also with `-I System/NativeRuntime/include` (for
`<runtime/thread-state.h>`). Link order:

```text
crt0.o <objects> libpunos_thread.a libpunos_mem.a libpunos_core.a libpunos_crt.a libpunos_syscall.a
```

## How it works

```text
thread_create()                         new thread
  allocate handle (1 page) and stack      |
  clone(trampoline, stack top, flags,     |
        handle, &tid, &state, &tid) ----> clone's assembly trampoline
  return handle                            -> thread_trampoline(handle)
                                               result = entry(argument)
thread_join()                                  handle->result = result
  while tid != 0:                          -> thread_exit(result)
    futex(&tid, FUTEX_WAIT, tid)           kernel: tid = 0, futex wake
  read handle->result
  free stack and handle
```

- **The handle** is one libmem page holding the kernel-maintained `tid`, the
  result, the entry and argument, and the thread's `RuntimeThreadState`.
  Its layout is in `src/internal.h`.
- **TLS and errno.** `RuntimeThreadState` of the new thread is passed to
  `clone()` as the TLS base (`CLONE_SETTLS`). Its `self` field points to
  itself and `errno_value` starts at `0`, so each thread has its own `errno`.
- **The `tid` field** is passed as both `parent_tid` and `child_tid`. The
  kernel writes the thread id there on creation and, because of
  `CLONE_CHILD_CLEARTID`, sets it to `0` and wakes the futex when the thread
  has exited. Only at that point is the thread no longer using its stack, so
  only then does `thread_join()` free it (contracts 16–17).
- **The futex** is waited on with plain `FUTEX_WAIT`, not the private variant:
  the kernel's wake-up on thread exit is a shared-futex wake, and a private
  waiter would not receive it.

## Implementation decisions

- **Stack size is 256 KiB, fixed.** Pages are backed only when touched, so an
  idle thread costs little. The specification has no stack size parameter.
- **No guard page.** The contract allows only `clone()`, `gettid()` and
  `futex()` plus libmem, and libmem can only protect whole blocks. A thread
  that overflows its 256 KiB stack therefore overwrites whatever memory lies
  below it instead of crashing. Adding a guard page needs a libmem operation
  such as "allocate with guard"; until then, avoid deep recursion and large
  local arrays in threads.
- **Self-join.** `thread_join()` called from the thread it joins would wait
  forever. It detects this (`tid == gettid()`) and returns
  `CORE_INVALID_STATE`.
- **Signals do not end a join.** `EINTR` and `EAGAIN` from the futex just
  mean "check again" (contract 16); `thread_join()` returns only when the
  thread has really finished.
- **Result.** The value returned by the entry function is stored in the handle
  by the library's C trampoline and returned by `thread_join()`.

## Tests

- `tests/abi_compile.c`: type signatures, constants, and that the handle's
  `tid` is 32-bit and its `state` is the shared `RuntimeThreadState`.
- `tests/thread_runtime.c`, check groups: `1–9` current id, `10–29` create
  and join (result, argument, negative values), `30–49` per-thread state
  (errno starts at 0 and is not shared, own thread id, 16-byte stack
  alignment), `50–59` join really waits, `60–69` stack and handle released
  after join, `70–89` 16 threads at once incrementing one counter and 200
  create/join cycles, `90–99` join keeps waiting while signals interrupt it,
  `100–119` self-join and invalid arguments.
  - Checks that run inside a created thread report through the thread's
    result, because `exit()` would end the whole process.

The host test was run 30 times in a row without a failure, and PUNos was
booted 5 times in QEMU with all self-tests passing each time. The test was
checked against 11 deliberately broken versions of `thread.c`; all 11 are
caught, 9 by their check number and 2 by hanging (without
`CLONE_CHILD_CLEARTID`, or without the self-join check, `thread_join()` waits
forever — which is exactly the bug).
