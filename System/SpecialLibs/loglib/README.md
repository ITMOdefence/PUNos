# loglib — special/log

Implements `DevDoc/SpecialLibs/loglib.md` using libfile, libmem and libstr.
Time/process/thread libraries supply types only. Paths are copied. Each append
validates UTF-8 and lengths and writes the exact little-endian binary frame;
flush is explicit. Rotation opens the replacement before closing the original.
If closing the old file fails, libfile consumes that file handle, so the store
remains closable but subsequent I/O returns CORE_INVALID_STATE; the replacement
is never adopted.

The reader accepts EOF only at frame boundaries and rejects malformed lengths,
levels, UTF-8 and truncated input. After a read failure it cannot safely resume
at a frame boundary, so further reads return CORE_IO_ERROR until close.
Returned records own both strings.

`make check` pins the ABI and tests the actual on-disk byte format, signed IDs,
record ownership, EOF, rotation success/failure, invalid UTF-8, size limits,
truncated/malformed frames and empty strings through real files.

Cross-compilation and emulated runtime verification passed. In a scratch copy,
altering the serialized timestamp failed at check 7 as expected. See the parent
README for the verification environment and native Linux/QEMU limitations.
