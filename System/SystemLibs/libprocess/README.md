# libprocess

System Lib for child processes (`sys/process`): start a native component,
check or wait for its end, send it `SIGTERM`. It is the low-level API that
initserv will use to run System Services and the VM.

Specification: `DevDoc/SystemLibs/libprocess.md`. Depends on
[libcore](../libcore/README.md), [libmem](../libmem/README.md) and
[libstr](../libstr/README.md).

## Build and test

From the repository root:

```bash
make -C System/SystemLibs/libprocess check
```

Outputs:

```text
System/SystemLibs/libprocess/build/libpunos_process.a   # the library
System/SystemLibs/libprocess/build/process_runtime      # runtime test / QEMU self-test
```

## Usage

```c
#include <sys/process.h>

/* Runs /punos/bin/logserv --verbose and waits for it. */
ErrorCode run_logserv(ProcessExitStatus *out_status)
{
    String executable;
    String argument;
    ProcessLaunch launch = { &executable, &argument, 1, NULL, 0 };
    ProcessHandle process;
    ErrorCode result;

    if ((result = string_from_c_str("/punos/bin/logserv", &executable)) != CORE_OK) {
        return result;
    }
    if ((result = string_from_c_str("--verbose", &argument)) == CORE_OK) {
        result = process_start(&launch, &process); /* strings may go now */
        (void)string_destroy(&argument);
    }
    (void)string_destroy(&executable);

    while (result == CORE_OK) {
        result = process_wait(&process, out_status);
        if (result != CORE_INTERRUPTED) {
            break;
        }
        result = CORE_OK; /* a signal handler ran; keep waiting */
    }
    return result;
}
```

Link order:

```text
crt0.o <objects> libpunos_process.a libpunos_str.a libpunos_mem.a libpunos_core.a libpunos_crt.a libpunos_syscall.a
```

## How a process is started

1. **Before `fork()`**, everything that can fail is prepared: the `argv` and
   `envp` arrays (one libmem block with pointers to the caller's strings) and
   the handle. If anything fails here, no process has been created.
2. **`fork()`.**
3. **In the child**, only `execve()` is called, and `exit(127)` if it fails
   (contract 5). The child never allocates memory, never returns into the
   caller's code and never touches libmem, so a half-copied parent state
   cannot hurt it.
4. **In the parent**, the arrays are freed and the handle is returned.

The child's `argv` is `executable, arguments..., NULL`, so `argv[0]` is the
executable path as usual. Its environment is exactly
`environment[0 .. environment_count)`; nothing is inherited implicitly. It does
inherit the caller's signal mask and ignored signals, as every `execve()` does.

## Implementation decisions

- **Header name.** The header is `<sys/process.h>`, as in
  `DevDoc/SystemLibs/_LibsList.md` and the consumers' specifications (the
  library page used to say `sys/proc`).
- **`execve()` failure.** The parent cannot see it: `fork()` has already
  succeeded and `process_start()` returns `CORE_OK`. The child then exits with
  the new constant **`PROCESS_EXIT_EXEC_FAILED` (127)**, the usual Unix code
  for this case, so the caller learns about it from `process_wait()`.
  Reporting it from `process_start()` itself would need a pipe or shared
  memory, which the contract does not allow.
- **Interrupted wait.** If a signal handler interrupts `process_wait()`, it
  returns `CORE_INTERRUPTED` and keeps the handle, like `time_sleep()` in
  libtime. The caller decides whether to wait again.
- **Environment format.** Every `environment` entry must have the form
  `NAME=value`: it must contain `=`, and the name before the first `=` must
  not be empty. `NAME=` and `NAME=a=b` are valid; `""`, `NAME` and `=value`
  give `CORE_INVALID_ARGUMENT` (contract 10). The name's characters are not
  checked. The check runs before `fork()`, so a rejected launch creates no
  child and releases everything it allocated.
- **Errors keep the handle.** `process_wait()` and `process_poll()` release
  the handle and set it to `NULL` only when the child's status has been
  collected.
- **Exit status.** `PROCESS_TERMINATION_KIND_EXITED` with the exit code
  `0 … 255`, or `PROCESS_TERMINATION_KIND_SIGNALED` with the signal number.
- **Handles** are a small structure in their own libmem block (one page).
  A handle is opaque: the library cannot tell a released or forged handle from
  a valid one, so using a handle after it was released is a bug.
- **Memory.** libprocess itself calls only `getpid()`, `fork()`, `execve()`,
  `waitpid()`, `exit()` and `kill()`. Memory for handles and arrays comes from
  libmem, which is how every System Lib gets memory.

## Tests

- `tests/abi_compile.c`: layouts of `ProcessLaunch` and `ProcessExitStatus`,
  enum values, the `PROCESS_EXIT_*` codes.
- `tests/process_runtime.c`: the test starts **itself** (`/proc/self/exe`)
  with a mode argument, so it needs no other programs. Check groups: `1–9`
  current id, `10–39` start and wait (exit codes, `execve()` failure, several
  children), `40–59` argv and environment (spaces, empty and non-ASCII
  arguments; the child checks them and reports through its exit code),
  `60–79` poll and terminate, `80–99` wait interrupted by signals, `100–119`
  invalid arguments and "no stray children left", `120–139` environment
  format (valid `NAME=value` entries reach the child unchanged; `""`,
  `NAME` and `=value`, alone or among valid entries, are rejected before
  `fork()` without leaking pages).

The test was checked against 13 deliberately broken versions of
`process.c`. 12 are caught: 10 by their check number, 2 by hanging (a test
that never ends is a failure too; `make check` would never finish). The
remaining one, removing the early `NULL`-array check, does not change
behaviour: the first element of a `NULL` array is a `NULL` `String *`, which
the string check rejects anyway.
