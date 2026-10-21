# Special Libs

Native PUNos libraries that provide higher-level abstractions for System
Services. Each library implements one specification from `DevDoc/SpecialLibs/`;
the specification is the contract, and the code must match it.

| Library | Header | Archive | Status |
| --- | --- | --- | --- |
| [ipclib](ipclib/README.md) | `<special/ipc.h>` | `libpunos_special_ipc.a` | Linux/QEMU validation pending |
| [loglib](loglib/README.md) | `<special/log.h>` | `libpunos_special_log.a` | Linux/QEMU validation pending |
| [inputlib](inputlib/README.md) | `<special/input.h>` | `libpunos_special_input.a` | Linux/QEMU validation pending |
| [camlib](camlib/README.md) | `<special/camera.h>` | `libpunos_special_camera.a` | Linux/QEMU validation pending |
| [displaylib](displaylib/README.md) | `<special/display.h>` | `libpunos_special_display.a` | Linux/QEMU validation pending |

Acceptance requires `make -C System/SpecialLibs check`, mutation checks and a
full QEMU boot check on the current revision. The
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
python3 System/SpecialLibs/testing/check_mutations.py
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

A mutation check reintroduces a defect in a disposable source copy and requires
the corresponding test to fail. `testing/check_mutations.py` builds a baseline,
checks each mutant, restores the source and verifies the restored library.
It is also run by CI. The original working tree is never changed.

Historical results recorded by earlier commits are retained below. Their native
GCC/QEMU runs have not been repeated for this revision in this Windows session.

| # | Fix | Check that fails when reverted |
| --- | --- | --- |
| 2 | `inputlib` keeps an unterminated frame instead of handing it over | 32 |
| 3 | `inputlib` keeps events read past a returned frame | 49 |
| 4 | `camlib` refuses `configure()` and `close()` while Streaming | 66 |
| 5 | `camlib` returns to `Open` after `stop()` | 54 |
| 6 | `libv4l2` clears the outstanding count on `ENODEV` | 59 |
| 8a | `ipclib` reports a terminal connect error | 92 |
| 8b | `ipclib` repeats that error instead of reporting `Open` | 95 |
| 9a | `loglib` rejects `component_length` over the maximum | 72 |
| 9b | `loglib` rejects `message_length` over the maximum | 74 |
| 9c | `loglib` rejects a severity outside `LogSeverity` | 76 |
| 10 | `camlib` tracks issued buffers by id | 83 |
| 13a | `ipclib` refuses an empty endpoint | 77 |
| 13b | `loglib` refuses an empty path | 83 |
| 13c | `camlib` refuses a path that is not `/dev/video<digits>` | 42 |
| 14a | `libv4l2` validates the device with `VIDIOC_QUERYCAP` | 45 |
| 14b | `inputlib` tells a keyboard from a device with only buttons | 78 |

Every test file numbers its checks sequentially from 1. They used to reuse
numbers across functions - `ipc_runtime.c` had eighteen collisions - and since
`check_fail()` exits with the check number, a failure in one of them could not
be told apart from a failure in another.

The former gaps in **#1** and **#11** are now covered: the send fake forwards a
short prefix to the peer, and an allocation wrapper compares the exact allocated
and freed sizes for a configuration list with 300 entries, most filtered out.
camlib counts supported configurations before allocating, so destruction never
has to reconstruct a larger allocation from a smaller count.

**#12** now has explicit pointer-typedef and function-signature ABI assertions
in all four libraries. **#7** has injected open/close failures and checks of both
files: an open failure preserves the old journal, while a close failure makes
the store invalid because libfile consumes the handle. The header, DevDoc and
README describe that distinction consistently.

### Validation record, 2026-10-06

The available host is Windows without WSL, GNU make/GCC or QEMU. Clang 18 (via
Zig 0.13) cross-compiled all 12 System Lib implementations and their ABI tests,
plus all four Special Lib implementations, ABI tests and runtime suites.
Six x86_64 ELF runtime suites passed in Unicorn 2.1.4 with modelled Linux
syscalls: ipclib, loglib, inputlib, camlib, libv4l2 and libinputdevice. For this
supplementary run, symbol substitution was applied during compilation because
Zig's link driver does not support GNU `--wrap`. The checked-in Linux Makefiles
use GNU `--wrap` to inject the same fakes.

All 23 mutants below failed at the expected check under that supplementary
execution; their restored suites passed. This validates the regression logic,
not the real Linux syscall/device ABI or boot integration. Native
`make -C System/SpecialLibs check`, the mutation runner with GCC, and the full
QEMU boot check remain pending. No readiness claim is made for this revision.

| Mutant | Library | Failing check |
| --- | --- | --- |
| Overlapping queue copy with ignored error | ipclib | 117 |
| Forgotten terminal connect error | ipclib | 95 |
| Missing completed-frame retry after allocation failure | inputlib | 90 |
| Incorrect resync after an overflow ending at SYN_REPORT | inputlib | 106 |
| Reading after DeviceLost | inputlib | 110 |
| Foreign frame accepted by matching buffer id | camlib | 122 |
| Missing stale sequence check | camlib | 128 |
| Allocation sized by unfiltered configuration count | camlib | 156 |
| Configuration enumeration during Streaming | camlib | 142 |
| File descriptor access after DeviceLost | camlib | 145 |
| Destroying outer handle after rejected lower close | camlib | 151 |
| Appending after failed rotation close | loglib | 93 |
| Wrong V4L2_CAP_DEVICE_CAPS ABI bit | libv4l2 | 16 |
| Missing bytesused bounds check | libv4l2 | 88 |
| Reading errno after cleanup instead of using saved errno | libv4l2 | 91 |
| Classifying extended KEY_* codes as buttons | libinputdevice | 38 |
| Streaming retained after failed destructive stop | camlib | 166 |
| Partial input frame discarded on read failure | inputlib | 116 |
| NULL IPC payload accepted | ipclib | 126 |
| Invalid String accepted for log append | loglib | 106 |
| Previously queued prefix queued twice on start retry | libv4l2 | 96 |
| Extra driver buffers exceed camera ownership table | libv4l2 | 96 |
| Stale V4L2 buffer accepted | libv4l2 | 103 |

The follow-up audit also cross-compiled libmem's runtime suite and executed its
focused malformed-MemoryBlock checks 161–166 in the supplementary emulator.
The full libmem runtime suite requires real Linux fork/signals and remains
pending. See [AUDIT.md](AUDIT.md) for findings and verification limits.

The V4L2 capability bit and the interleaved KEY_*/BTN_* ranges were verified
against Linux's [videodev2.h](https://github.com/torvalds/linux/blob/master/include/uapi/linux/videodev2.h)
and [input-event-codes.h](https://github.com/torvalds/linux/blob/master/include/uapi/linux/input-event-codes.h).

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
