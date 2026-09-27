# System Libs

Native PUNos libraries that sit between the NativeRuntime (CRT and Syscall
Wrappers) and System Services / applications. Each library implements one
specification from `DevDoc/SystemLibs/`; the specification is the contract,
and the code must match it.

| Library | Header | Archive | Status |
| --- | --- | --- | --- |
| [libcore](libcore/README.md) | `<sys/core.h>` | `libpunos_core.a` | Implemented |
| [libmath](libmath/README.md) | `<sys/math.h>` | `libpunos_math.a` | Implemented |
| [libmem](libmem/README.md) | `<sys/memory.h>` | `libpunos_mem.a` | Implemented |
| [libstr](libstr/README.md) | `<sys/string.h>` | `libpunos_str.a` | Implemented |
| [libfile](libfile/README.md) | `<sys/file.h>` | `libpunos_file.a` | Implemented |
| [libtime](libtime/README.md) | `<sys/time.h>` | `libpunos_time.a` | Implemented |
| [libprocess](libprocess/README.md) | `<sys/process.h>` | `libpunos_process.a` | Implemented |
| [libthread](libthread/README.md) | `<sys/thread.h>` | `libpunos_thread.a` | Implemented |
| [libsync](libsync/README.md) | `<sys/sync.h>` | `libpunos_sync.a` | Implemented |
| [libeventloop](libeventloop/README.md) | `<sys/eventloop.h>` | `libpunos_eventloop.a` | Implemented |
| [libinputdevice](libinputdevice/README.md) | `<sys/input-device.h>` | `libpunos_inputdevice.a` | Implemented |
| [libv4l2](libv4l2/README.md) | `<sys/v4l2.h>` | `libpunos_v4l2.a` | Implemented |

The full list of planned libraries is in `DevDoc/SystemLibs/_LibsList.md`.

## Build and test

From the repository root:

```bash
make -C System/SystemLibs check    # build and test every library
make -C System/SystemLibs all      # build the static archives only
make -C System/SystemLibs clean
```

A single library is built and tested the same way, for example
`make -C System/SystemLibs/libcore check`.

The same runtime tests also run inside PUNos: they are packaged into the
initramfs as `/punos/bin/<library>-selftest`, and the initserv smoke-test stub
executes them at boot. Build and boot as described in `System/start-guide.md`
and look for:

```text
PUNos initserv stub: libcore self-test passed
PUNos initserv stub: libmath self-test passed
PUNos initserv stub: libmem self-test passed
PUNos initserv stub: libstr self-test passed
PUNos initserv stub: libfile self-test passed
PUNos initserv stub: libtime self-test passed
PUNos initserv stub: libprocess self-test passed
PUNos initserv stub: libthread self-test passed
PUNos initserv stub: libsync self-test passed
PUNos initserv stub: libeventloop self-test passed
PUNos initserv stub: libinputdevice self-test passed
PUNos initserv stub: libv4l2 self-test passed
```

## Conventions for every library

### Layout

```text
System/SystemLibs/<library>/
├── Makefile
├── README.md
├── include/sys/<name>.h   # public header, the only thing consumers include
│                          # (the name listed in DevDoc/SystemLibs/_LibsList.md)
├── src/                   # implementation; internal.h for internals the test needs
└── tests/
    ├── abi_compile.c      # compile-time ABI checks, no runtime code
    └── <name>_runtime.c   # runtime test, also used as the QEMU self-test
```

Build output goes to `<library>/build/`, which is ignored by Git.
Code shared by the tests of all libraries lives in `System/SystemLibs/testing/`.

### Makefile

All build rules live in `System/SystemLibs/library.mk`. A library's own
Makefile only describes the library:

```make
LIBRARY_NAME := str              # -> build/libpunos_str.a
RUNTIME_TEST := string_runtime   # tests/string_runtime.c
DEPENDENCIES := libcore libmem   # other System Libs, in dependency order

include ../library.mk
```

Every `.c` file in `src/` goes into the archive. `library.mk` adds the include
directories of the dependencies, builds them first, and links the runtime test
in the right order.

Targets:

| Target | Result |
| --- | --- |
| `all` | `build/libpunos_<name>.a` |
| `runtime-test` | the runtime test binary, used by `System/Boot/Makefile` |
| `check` | builds the ABI checks, runs the runtime test on the host |
| `clean` | removes `build/` |

When a new library is added:

1. list it in `LIBS` of `System/SystemLibs/Makefile` (in dependency order) and
   in the table above;
2. add its runtime test to `SYSTEM_LIBS_SELFTESTS` in `System/Boot/Makefile`,
   to `SELF_TESTS` in `System/scripts/build-initramfs.sh` (as
   `<library>-selftest`) and to `self_tests` in
   `System/Boot/InitservStub/initserv_stub.c`;
3. add the expected boot line to `System/start-guide.md` and describe the
   implementation status in the library's page in `DevDoc/SystemLibs/`.

### Compilation

Libraries are freestanding: no host headers, no libc. They use the flags of
the NativeRuntime plus `-fno-tree-loop-distribute-patterns`, which keeps GCC
from turning byte loops into calls to `memcpy()`/`memset()` (PUNos has none):

```text
-std=c11 -ffreestanding -nostdinc -fno-builtin -fno-stack-protector -fno-pic -m64 -Wall -Wextra -Werror
-fno-tree-loop-distribute-patterns
```

Check the result with `nm --undefined-only build/libpunos_<name>.a`: it must
list only the functions the library's contract allows.

Programs link with `-nostdlib -no-pie -Wl,-e,_start` in this order:

```text
crt0.o <objects> <System Libs, dependants first> libpunos_crt.a libpunos_syscall.a
```

### API rules (from `DevDoc/SystemLibs/libcore.md`)

- Every operation that can fail returns `ErrorCode`; `CORE_OK` is the only
  success value.
- Results are written through `out_*` pointers. After an error their contents
  are unspecified and must not be used.
- A failed Syscall Wrapper's `errno` is saved immediately, before any other
  wrapper call, and converted with `core_error_from_errno()`, unless the
  library's own specification defines a more precise interpretation.
- Machine and Linux ABI types come from `<sys/core.h>`, never from local
  typedefs.

### Naming enum values

Specifications list enum values by short names (`Less`, `Equal`, `Greater`).
Several libraries use the same short names, and C enum values share one global
namespace, so in C every value gets its type name as a prefix, both in
UPPER_SNAKE_CASE:

```text
<TypeName>.<Value>   ->   <TYPE_NAME>_<VALUE>
MemoryCompareResult.Less        ->  MEMORY_COMPARE_RESULT_LESS
FileOpenMode.ReadWriteCreate    ->  FILE_OPEN_MODE_READ_WRITE_CREATE
```

Numeric values are part of the ABI and are pinned in `abi_compile.c`.

### Tests

- `abi_compile.c` pins the public ABI with `_Static_assert`: type identity,
  sizes, signedness, structure offsets and enum values.
- The runtime test is an ordinary PUNos program with `main()` that uses
  `testing/check.h`. Every check is `EXPECT(N, condition)` with a unique
  number `N` from 1 to 255; the first failed check prints
  `<library> runtime test failed: check N` and exits with status `N`. On
  success it prints `<library> runtime test passed` and returns `0`. Group
  check numbers by operation and list the groups at the top of the test.
- Cover both the success path and every error path the specification names.
  Check effects, not only return codes: that pages are really released, that
  protection really faults, that bytes outside the target range are untouched.
- When a result depends on things a test cannot control (clocks, signals,
  timing), put the pure computation into a function declared in
  `src/internal.h` and test it on exact values, as libtime does for rounding.
  Internal functions are prefixed `lib<name>_internal_` and are not part of
  the public API.
- Make sure the test can fail: break the implementation on purpose (in a
  scratch copy, never in the tree) and confirm that the expected check number
  fails. Record the result in the library's README.
- Some bugs show up as a hang, not as a failed check (a join that waits
  forever). A test that never finishes is a failed test.
- Code with threads or signals is run many times in a row (the libthread test
  was run 30 times on the host and in 5 QEMU boots) to catch races that only
  show up sometimes.
