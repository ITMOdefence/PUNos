# Special Libs

Native PUNos libraries that provide higher-level abstractions for System
Services. Each library implements one specification from `DevDoc/SpecialLibs/`;
the specification is the contract, and the code must match it.

| Library | Header | Archive | Status |
| --- | --- | --- | --- |
| [ipclib](ipclib/README.md) | `<special/ipc.h>` | `libpunos_special_ipc.a` | Implemented |
| [loglib](loglib/README.md) | `<special/log.h>` | `libpunos_special_log.a` | Implemented |
| [inputlib](inputlib/README.md) | `<special/input.h>` | `libpunos_special_input.a` | Implemented |
| [camlib](camlib/README.md) | `<special/camera.h>` | `libpunos_special_camera.a` | Implemented |

The full list of planned libraries is in `DevDoc/SpecialLibs/_LibsList.md`.

## Build and test

From the repository root:

```bash
make -C System/SpecialLibs check    # build and test every library
make -C System/SpecialLibs all      # build the static archives only
make -C System/SpecialLibs clean
```

A single library is built and tested the same way, for example
`make -C System/SpecialLibs/ipclib check`.

## Conventions for every library

### Layout

```text
System/SpecialLibs/<library>/
├── Makefile
├── README.md
├── include/special/<name>.h   # public header, the only thing consumers include
├── src/                       # implementation; internal.h for internals the test needs
└── tests/
    ├── abi_compile.c          # compile-time ABI checks, no runtime code
    └── <name>_runtime.c       # runtime test, also used as the QEMU self-test
```

Build output goes to `<library>/build/`, which is ignored by Git.

### Makefile

All build rules live in `System/SpecialLibs/special_libs.mk`. A library's own
Makefile only describes the library:

```make
LIBRARY_NAME := ipc              # -> build/libpunos_special_ipc.a
RUNTIME_TEST := ipc_runtime      # tests/ipc_runtime.c
DEPENDENCIES := libcore libmem libstr libeventloop

include ../special_libs.mk
```

Every `.c` file in `src/` goes into the archive. `special_libs.mk` adds the
include directories of the dependencies, builds them first, and links the
runtime test in the right order.

Targets:

| Target | Result |
| --- | --- |
| `all` | `build/libpunos_special_<name>.a` |
| `runtime-test` | the runtime test binary |
| `check` | builds the ABI checks, runs the runtime test on the host |
| `clean` | removes `build/` |

### Compilation

Libraries are freestanding: no host headers, no libc. They use the flags of
the NativeRuntime plus `-fno-tree-loop-distribute-patterns`, which keeps GCC
from turning byte loops into calls to `memcpy()`/`memset()` (PUNos has none):

```text
-std=c11 -ffreestanding -nostdinc -fno-builtin -fno-stack-protector -fno-pic -m64 -Wall -Wextra -Werror
-fno-tree-loop-distribute-patterns
```

Programs link with `-nostdlib -no-pie -Wl,-e,_start` in this order:

```text
crt0.o <objects> <System Libs, dependants first> libpunos_crt.a libpunos_syscall.a
```

### API rules

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
  Check effects, not only return codes.
- Make sure the test can fail: break the implementation on purpose (in a
  scratch copy, never in the tree) and confirm that the expected check number
  fails. Record the result in the library's README.
