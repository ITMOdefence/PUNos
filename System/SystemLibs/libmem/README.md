# libmem

System Lib for memory (`sys/memory`): allocation, resizing, release and
protection of memory blocks, plus copying, moving, filling and comparing byte
ranges.

Specification: `DevDoc/SystemLibs/libmem.md`. Depends on
[libcore](../libcore/README.md).

## Build and test

From the repository root:

```bash
make -C System/SystemLibs/libmem check
```

Outputs:

```text
System/SystemLibs/libmem/build/libpunos_mem.a     # the library
System/SystemLibs/libmem/build/memory_runtime     # runtime test / QEMU self-test
```

## Usage

```c
#include <sys/memory.h>

ErrorCode make_table(usize count, MemoryBlock *out_table)
{
    usize size;
    ErrorCode result = core_size_multiply(count, sizeof(u64), &size);

    if (result != CORE_OK) {
        return result;
    }
    return memory_allocate(size, out_table); /* zero-filled */
}

/* ... later, exactly once: */
(void)memory_free(&table);
```

Compile with `-I System/SystemLibs/libmem/include`,
`-I System/SystemLibs/libcore/include` and
`-I System/NativeRuntime/SyscallWrappers/include`. Link order:

```text
crt0.o <objects> libpunos_mem.a libpunos_core.a libpunos_crt.a libpunos_syscall.a
```

## How it works

There is no heap. Every non-empty `MemoryBlock` is its own anonymous private
`mmap()` mapping, rounded up to whole 4 KiB pages; `MemoryBlock.size` keeps the
size the caller asked for. The contract allows only `mmap()`, `munmap()` and
`mprotect()`, and `memory_protect()` works on whole blocks, so a block cannot
share pages with another one: otherwise protecting one would also protect its
neighbour.

Consequences:

- every allocation costs at least one page (4 KiB) and a syscall; code that
  needs many small objects should put them into one block;
- new memory is always zero, because the kernel zero-fills anonymous pages;
- a non-empty block always starts on a page boundary. The library uses this to
  reject things that are not blocks (a `Bytes` view, a pointer into the middle
  of a block) with `CORE_INVALID_ARGUMENT`.

`memory_reallocate()` has three paths, chosen by the number of pages:

| New size needs | What happens | `data` |
| --- | --- | --- |
| the same number of pages | size changes in place; bytes gained by growing are zeroed | unchanged |
| fewer pages | the tail pages are released with `munmap()` | unchanged |
| more pages | a new mapping is created, the data copied, then the old mapping released | changes |

There is no `mremap()` in the contract, so growing past the current pages
always copies.

## Implementation decisions

The specification leaves these points open; they are fixed here and pinned by
the tests.

- **Header name.** The header is `<sys/memory.h>`, as in
  `DevDoc/SystemLibs/_LibsList.md` and in every consumer's specification (the
  library page used to say `sys/mem`).
- **Enum value names.** `MemoryCompareResult` values are
  `MEMORY_COMPARE_RESULT_LESS` (`-1`), `..._EQUAL` (`0`) and `..._GREATER`
  (`1`), following the naming rule in [System Libs](../README.md). The values
  follow the sign of `left - right`.
- **Bytes gained by growing are zero** in every path of
  `memory_reallocate()`, not only in a new mapping. Without this, bytes cut off
  by a shrink would reappear after the next grow.
- **`memory_reallocate()` resets protection to `MEM_READ | MEM_WRITE`.** It
  must read the old data and may have to zero bytes, so it first makes the
  block readable and writable. `MemoryBlock` has no room to remember the
  previous protection. If it fails after this step, the block keeps its data
  but may have become writable.
- **Errors leave the block unchanged.** In the "more pages" path the old
  mapping is released only after the copy; if that release fails, the new
  mapping is released and the block stays as it was.
- **`memory_free()` sets the block to `{ NULL, 0 }`**, so freeing the same
  variable twice is harmless. Freeing a *copy* of an already freed block is
  still a bug the library cannot detect.
- **Overlap in `memory_copy()`** is checked for the bytes actually written:
  `source` against the first `source.size` bytes of `destination`.
- **Invalid views.** A `Bytes` view with `data == NULL` and a non-zero size,
  or one that wraps around the end of the address space, is rejected with
  `CORE_INVALID_ARGUMENT`. Empty views are never dereferenced.
- **`NULL` pointers** (`out_block`, `block`, `out_result`) return
  `CORE_INVALID_ARGUMENT`.
- **Sizes that cannot be mapped** (for example `USIZE_MAX`, which does not
  round up to whole pages, or `2^62`, beyond the x86_64 address space) return
  `CORE_OUT_OF_MEMORY`.
- **Byte loops, no `memcpy()`.** Copy, move, fill and compare are simple byte
  loops. PUNos has no `memcpy()`/`memset()`, so `../library.mk` passes
  `-fno-tree-loop-distribute-patterns` to every System Lib to stop GCC from
  generating calls to them. Speeding the loops up (word-sized copies) is possible later without
  changing the API.

## Tests

- `tests/abi_compile.c`: layout of `MemoryBlock`, type and values of
  `MemoryProtection` and the `MEM_*` flags, values of `MemoryCompareResult`.
- `tests/memory_runtime.c`: every operation, success and error paths. Check
  numbers are grouped: `1–19` allocate, `20–29` free, `30–49` protect,
  `50–89` reallocate, `100–119` copy, `120–139` move, `140–149` set,
  `150–169` compare.
  - Whether pages are really mapped or released is checked with `mprotect()`,
    which fails with `ENOMEM` on an unmapped range.
  - Whether protection really works is checked in a child process: writing to
    a read-only block must kill it with `SIGSEGV`.

The test was checked against 12 deliberately broken versions of `memory.c`
(skipped zeroing, pages not released, overlap not rejected, signed byte
comparison and others). 11 were caught, each by its own check number. The
remaining one, removing the page-alignment check, does not change behaviour:
the kernel rejects an unaligned address itself with `EINVAL`, which also
becomes `CORE_INVALID_ARGUMENT`.
