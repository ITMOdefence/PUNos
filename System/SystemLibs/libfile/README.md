# libfile

System Lib for files and directories (`sys/file`).

Specification: `DevDoc/SystemLibs/libfile.md`. Depends on
[libcore](../libcore/README.md), [libmem](../libmem/README.md) (handle
storage) and [libstr](../libstr/README.md) (paths).

## Build and test

From the repository root:

```bash
make -C System/SystemLibs/libfile check
```

The test creates and removes a handful of files and one directory under
`/tmp` and opens `/dev/null` to exercise `FileKind.Device`. Outputs:

```text
System/SystemLibs/libfile/build/libpunos_file.a   # the library
System/SystemLibs/libfile/build/file_runtime      # runtime test / QEMU self-test
```

## Usage

```c
#include <sys/file.h>

ErrorCode read_all(const String *path, Bytes buffer, usize *out_read)
{
    FileHandle file;
    ErrorCode result = file_open(path, FILE_OPEN_MODE_READ, FILE_DEFAULT_PERMISSIONS, &file);

    if (result != CORE_OK) {
        return result;
    }
    result = file_read(file, buffer, out_read);
    (void)file_close(&file);
    return result;
}
```

Link order:

```text
crt0.o <objects> libpunos_file.a libpunos_str.a libpunos_mem.a libpunos_core.a libpunos_crt.a libpunos_syscall.a
```

## Implementation decisions

- **`FileOpenMode`/`FileSeekOrigin`/`FileKind` numbering.** The specification
  lists these values by name only, unlike, say, `ProcessTerminationKind`.
  This library numbers them in the order the specification's table lists
  them, starting at `0`, and pins the numbers in `tests/abi_compile.c`.
  `FileSeekOrigin` happens to line up with `SEEK_SET`/`SEEK_CUR`/`SEEK_END`;
  this is a mnemonic coincidence, not something callers should rely on.
- **The underlying file descriptor is never exposed.** Unlike `libv4l2` and
  `libinputdevice`, `FileHandle` has no `get_fd()`: regular files are always
  "ready", so there is no reason to poll them from an event loop, and the
  specification's contract 5 says the descriptor is not handed to the
  caller.
- **No directory removal.** The specification's allowed Syscall Wrappers do
  not include `rmdir()`, and the operations table has no
  `directory_remove()`. A directory created by `directory_create()` can
  currently not be removed by this library; the runtime test accepts this by
  reusing the same fixed path across runs (`directory_create()` on an
  already-existing directory returns `CORE_ALREADY_EXISTS`, which the test
  checks for directly) instead of creating a fresh one every time.
  A `directory_remove()` operation following the `unlink()`/`rmdir()`
  split would need a specification update.
- **`file_write_all()` on a `write()` that returns `0`.** Linux `write()` is
  not documented to return `0` for a non-empty request on a regular,
  blocking file descriptor, but nothing rules it out for every future kind
  of descriptor this library might see. Treating it as forward progress
  could spin forever, so `file_write_all()` reports `CORE_IO_ERROR` instead
  of retrying indefinitely.
- **`file_close()` still releases the handle when `close()` fails.** POSIX
  says the file descriptor is unspecified-state after a failed `close()` on
  Linux (it is always deallocated); keeping the `FileHandle` alive after
  that would let a caller retry `close()` on a descriptor number the kernel
  may have already reused for something else. The error from `close()` is
  still returned.
- **Permission bits.** `file_open()`/`directory_create()` reject
  `permissions` with any bit outside `0000..0777` as `CORE_INVALID_ARGUMENT`
  before making a syscall.
- **No dependency on wrapper code beyond the contract.** `libpunos_file.a`
  calls only `open()`, `close()`, `read()`, `write()`, `lseek()`, `fstat()`,
  `mkdir()`, `unlink()`, `rename()` and `fsync()`, as contract 1 requires.

## Tests

- `tests/abi_compile.c`: `FilePermissions` is `u32`; the default permission
  constants; the ABI numbering of `FileOpenMode`, `FileSeekOrigin` and
  `FileKind`; the layout of `FileInfo`.
- `tests/file_runtime.c`, check groups: `1–7` `file_open`/`file_close`
  error paths (including opening a missing file), `10–22` write then read
  back, including end of file and a zero-size read, `30–37` `file_seek` in
  all three origins and a seek that would go negative, `40–50` `file_info`
  on a regular file and on `/dev/null` (`FileKind.Device`), `55–58`
  `file_flush`, `60–67` `FileOpenMode.AppendCreate`, `70–76`
  `directory_create()` and that a directory's `FileHandle` reports
  `FileKind.Directory`, `80–89` `file_rename()` and `file_remove()`.
