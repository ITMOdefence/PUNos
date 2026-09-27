/*
 * Compile-time checks of the libfile ABI against DevDoc/SystemLibs/libfile.md.
 * This file only has to compile; it produces no runnable program.
 */
#include <sys/file.h>

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)

/* FilePermissions is an unsigned integer, as the specification requires. */
_Static_assert(SAME_TYPE((FilePermissions)0, u32), "FilePermissions must be u32");
_Static_assert(FILE_DEFAULT_PERMISSIONS == 0644, "FILE_DEFAULT_PERMISSIONS");
_Static_assert(DIRECTORY_DEFAULT_PERMISSIONS == 0755, "DIRECTORY_DEFAULT_PERMISSIONS");

/*
 * FileOpenMode/FileSeekOrigin/FileKind values are not pinned by the
 * specification; this library numbers them in the order the specification's
 * table lists them, and that numbering is then ABI (see file.h).
 */
_Static_assert(FILE_OPEN_MODE_READ == 0, "FILE_OPEN_MODE_READ");
_Static_assert(FILE_OPEN_MODE_WRITE_TRUNCATE == 1, "FILE_OPEN_MODE_WRITE_TRUNCATE");
_Static_assert(FILE_OPEN_MODE_READ_WRITE_CREATE == 2, "FILE_OPEN_MODE_READ_WRITE_CREATE");
_Static_assert(FILE_OPEN_MODE_APPEND_CREATE == 3, "FILE_OPEN_MODE_APPEND_CREATE");

_Static_assert(FILE_SEEK_ORIGIN_START == 0, "FILE_SEEK_ORIGIN_START");
_Static_assert(FILE_SEEK_ORIGIN_CURRENT == 1, "FILE_SEEK_ORIGIN_CURRENT");
_Static_assert(FILE_SEEK_ORIGIN_END == 2, "FILE_SEEK_ORIGIN_END");

_Static_assert(FILE_KIND_REGULAR == 0, "FILE_KIND_REGULAR");
_Static_assert(FILE_KIND_DIRECTORY == 1, "FILE_KIND_DIRECTORY");
_Static_assert(FILE_KIND_DEVICE == 2, "FILE_KIND_DEVICE");
_Static_assert(FILE_KIND_OTHER == 3, "FILE_KIND_OTHER");

/* FileInfo layout: size, then kind. */
_Static_assert(__builtin_offsetof(FileInfo, size) == 0, "FileInfo.size offset");
_Static_assert(__builtin_offsetof(FileInfo, kind) == 8, "FileInfo.kind offset");
_Static_assert(SAME_TYPE(((FileInfo *)0)->size, u64), "FileInfo.size type");
