# Special Libs

Native PUNos libraries that provide higher-level abstractions for System
Services. Each library implements one specification from `DevDoc/SpecialLibs/`;
the specification is the contract, and the code must match it.

| Library | Header | Archive | Status |
| --- | --- | --- | --- |
| [ipclib](ipclib/README.md) | `<special/ipc.h>` | `libpunos_special_ipc.a` | Host tests pass |
| [loglib](loglib/README.md) | `<special/log.h>` | `libpunos_special_log.a` | Host tests pass |
| [inputlib](inputlib/README.md) | `<special/input.h>` | `libpunos_special_input.a` | Host tests pass |
| [camlib](camlib/README.md) | `<special/camera.h>` | `libpunos_special_camera.a` | Host tests pass |

"Host tests pass" means `make -C System/SpecialLibs check` is green and every
fix the library claims has a check that fails when the fix is reverted. The
paths that need real hardware - a V4L2 capture device, an evdev device - are
exercised through the fakes described below rather than against a real node, so
a green run says nothing about the V4L2 and evdev ABIs themselves; that is
libv4l2's and libinputdevice's own business. The QEMU boot check in CI runs the
same self-tests inside the initramfs.

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
    ├── <name>_runtime.c       # runtime test, also used as the QEMU self-test
    └── <dep>_fake.c           # optional: stands in for one dependency
```

Build output goes to `<library>/build/`, which is ignored by Git.

### Testing without the hardware

A camera, a keyboard and a Unix socket that fails in a particular way are not
always available on the machine running the tests, and some of them cannot be
provoked on demand at all - Linux completes an `AF_UNIX` connect immediately, so
a client never sees `EINPROGRESS`. A library can therefore list fakes:

```make
TEST_FAKES := v4l2_fake            # tests/v4l2_fake.c, linked before the archives
TEST_FAKE_LDFLAGS := ...           # only needed when a fake replaces a wrapper
```

A fake provides its own definition of the dependency's public functions, so the
library under test runs unmodified against something the test can script. The
fake objects are linked ahead of the dependency archives, so a definition in one
of them satisfies the reference and the archive member is never pulled in.

Two fakes replace a syscall wrapper rather than a library: `ipclib`'s
`getsockopt` and `libv4l2`'s `open`/`ioctl`/`mmap`. `libpunos_syscall.a` keeps
every wrapper in a single object, so pulling it in for, say, `mmap()` also drags
in the `ioctl()` being replaced. Those libraries pass
`-Wl,--allow-multiple-definition`, which `library.mk` and `special_libs.mk`
apply only when a fake is actually linked in.

A fake has to behave like the thing it replaces, not just return something
convenient. Two details cost real debugging time and are easy to get wrong:

  - `mmap()` must hand out page-aligned, zero-filled memory. libmem validates a
    block by requiring page alignment and relies on the contents being zero.
  - a fake must not allocate through libmem, because libmem allocates with
    `mmap()`. Use static storage.

### Mutation checks

A test that passes proves nothing on its own: it also passes when the fix is
absent. Every fix below was therefore checked by reverting it and confirming
that one specific check fails. The check number is the one that fails, which is
what `make check` prints and what CI reads.

| # | Fix | Check that fails when reverted |
| --- | --- | --- |
| 2 | `inputlib` keeps an unterminated frame instead of handing it over | 102 |
| 3 | `inputlib` keeps events read past a returned frame | 126 |
| 4 | `camlib` refuses `configure()` and `close()` while Streaming | 122 |
| 5 | `camlib` returns to `Open` after `stop()` | 107 |
| 6 | `libv4l2` clears the outstanding count on `ENODEV` | 68 |
| 8a | `ipclib` reports a terminal connect error | 105 |
| 8b | `ipclib` repeats that error instead of reporting `Open` | 108 |
| 9a | `loglib` rejects `component_length` over the maximum | 83 |
| 9b | `loglib` rejects `message_length` over the maximum | 85 |
| 9c | `loglib` rejects a severity outside `LogSeverity` | 87 |
| 10 | `camlib` tracks issued buffers by id | 148 |
| 13a | `ipclib` refuses an empty endpoint | 80 |
| 13b | `loglib` refuses an empty path | 90 |
| 13c | `camlib` refuses a path that is not `/dev/video<digits>` | 60 |
| 14a | `libv4l2` validates the device with `VIDIOC_QUERYCAP` | 53 |
| 14b | `inputlib` tells a keyboard from a device with only buttons | 172 |

Two fixes are deliberately not in the table:

  - **#1, the outgoing queue shift**, is the one gap. `memory_copy()` rejects
    overlapping ranges, so reverting it to `memory_move()`'s forbidden twin
    makes the call fail, which the old code ignored before shrinking
    `outgoing_size`. Detecting that needs a send that the kernel accepts only in
    part - a socket buffer full enough to return a short count - and then a
    peer that reassembles the stream and finds it corrupted. Nothing in the
    current test does either, so this fix rests on reading, not on a check.
  - **#11, the configuration array** is trimmed to the count handed out so that
    `memory_free()` is given the size it was allocated with. Reverting the trim
    still passes every check, because the only consequence is a mapping that is
    never fully unmapped - a leak, which a test cannot see without allocation
    accounting.

Two more are structural rather than behavioural, and the ABI compile checks
cover them: **#12**, the pointer handle typedefs, is enforced by the signatures
having to compile, and **#7** is not a return code at all - `file_close()`
releases the handle even when `close()` fails, so no code path can leave the
previous log usable. The store is made explicitly invalid instead, and the
header no longer promises otherwise.

One more regression was found this way, in `libinputdevice`: widening the
capability mask without updating its ABI check broke the QEMU boot run while the
host run stayed green, because the host's `/dev/input/event0` reports no
`EV_KEY` at all and never sets the new bits. See commit `accd443`.

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
