# libcore

Base System Lib of PUNos (`sys/core`): common types, `ErrorCode`, overflow-safe
size arithmetic and conversion of Linux `errno` values to `ErrorCode`.

Specification: `DevDoc/SystemLibs/libcore.md`.

## Build and test

From the repository root:

```bash
make -C System/SystemLibs/libcore check
```

Outputs:

```text
System/SystemLibs/libcore/build/libpunos_core.a   # the library
System/SystemLibs/libcore/build/core_runtime      # runtime test / QEMU self-test
```

## Usage

```c
#include <sys/core.h>

ErrorCode buffer_size(usize count, usize element_size, usize *out_size)
{
    return core_size_multiply(count, element_size, out_size);
}
```

Compile with `-I System/SystemLibs/libcore/include` and
`-I System/NativeRuntime/SyscallWrappers/include`, and link
`libpunos_core.a` before `libpunos_crt.a` and `libpunos_syscall.a`.

## What the library provides

- **Types.** `usize`, `isize`, `intptr_t`, `uintptr_t`, `f64`, `bool`,
  `Bytes`, `ErrorCode` and the Linux aliases `sa_family_t`, `in_port_t`,
  `dev_t`, `ino_t`, `nlink_t`. The fixed-width integers and `size_t`,
  `ssize_t`, `off_t`, `pid_t`, `mode_t`, `socklen_t`, `time_t`, `clockid_t`
  are re-exported from `<punos/syscall/types.h>`, not redeclared.
- **Constants.** `NULL`, `true`, `false`, `USIZE_MAX`, `ISIZE_MIN`,
  `ISIZE_MAX` and the `CORE_*` error codes.
- **Operations.** `core_is_ok`, `core_size_add`, `core_size_multiply`,
  `core_error_from_errno`.

## Implementation decisions

These points are not spelled out in the specification or needed a choice; they
are fixed here and pinned by the tests.

- **ErrorCode values are ABI.** `CORE_OK` is `0`, the rest follow the order of
  the specification's table (`CORE_INVALID_ARGUMENT` = 1 … `CORE_OVERFLOW` =
  13). Error codes can cross process boundaries (IPC, logs), so existing values
  are never renumbered; new codes are appended. `sizeof(ErrorCode)` is 4.
- **`usize` is `u64`, the same type as `size_t`;** `isize` is `i64`, the same
  type as `ssize_t`. Values pass between them without casts or warnings.
- **`bool` is C11 `_Bool`.** It is an 8-bit unsigned type, as the
  specification requires, and it normalises every non-zero value to `1`, so a
  `bool` never holds anything but `false` or `true`.
- **Range of `core_error_from_errno()`.** The specification maps "any other
  positive value" to `CORE_IO_ERROR` and "negative or unknown" values to
  `CORE_INVALID_ARGUMENT`. A value counts as a Linux errno when it is in
  `1 … 4095` (the kernel's `MAX_ERRNO`, the same bound Syscall Wrappers use to
  detect errors). So: `0` → `CORE_OK`; mapped errno → its code; other errno in
  range → `CORE_IO_ERROR`; negative or above `4095` → `CORE_INVALID_ARGUMENT`.
- **`NULL` out-pointer.** `core_size_add()` and `core_size_multiply()` return
  `CORE_INVALID_ARGUMENT` when `out_value` is `NULL`, instead of crashing.
- **Nothing is written on error.** On error the implementation leaves
  `*out_value` untouched. Callers still must not rely on it: the contract says
  the value is unspecified.
- **Overflow checks** use the compiler's `__builtin_add_overflow` and
  `__builtin_mul_overflow`, which compile to inline instructions and need no
  runtime support.
- **No dependency on wrapper code.** `libpunos_core.a` has no undefined
  symbols: it uses `<punos/syscall/types.h>` and the `E*` constants from
  `<punos/syscall/constants.h>` only.

## Tests

- `tests/abi_compile.c`: compile-time checks of every type, limit, the
  `Bytes` layout and all `ErrorCode` values.
- `tests/core_runtime.c`: runtime checks of all operations, including every
  row of the `errno` table, overflow boundaries, `NULL` out-pointers and the
  guarantee that `core_error_from_errno()` does not read the global `errno`.
  Check numbers are grouped by operation: `1–4` `core_is_ok`, `10–16`
  `core_size_add`, `20–26` `core_size_multiply`, `30–74`
  `core_error_from_errno`, `80–82` global `errno`, `90–93` types.
