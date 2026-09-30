#ifndef PUNOS_SYS_FILE_H
#define PUNOS_SYS_FILE_H

/*
 * libfile (sys/file) - files and directories.
 *
 * Specification: DevDoc/SystemLibs/libfile.md
 *
 * The only syscall wrappers used are open(), close(), read(), write(),
 * lseek(), fstat(), mkdir(), unlink(), rename() and fsync(). Paths are
 * <sys/string.h> Strings (UTF-8, no NUL). All operations return ErrorCode;
 * see <sys/core.h> for the conventions.
 */

#include <sys/core.h>
#include <sys/string.h>

/*
 * Opaque handle of an open file. The underlying file descriptor is never
 * exposed to the caller (specification contract 5). Released exactly once
 * with file_close().
 */
typedef struct FileHandle *FileHandle;

/*
 * The specification lists these values by name only, without pinning
 * numbers (unlike, say, ProcessTerminationKind). This library numbers them
 * in the order the specification's table lists them, starting at 0; the
 * numbers are then part of the ABI like any other enum here.
 */
typedef enum FileOpenMode {
    FILE_OPEN_MODE_READ = 0,               /* O_RDONLY */
    FILE_OPEN_MODE_WRITE_TRUNCATE = 1,      /* O_WRONLY | O_CREAT | O_TRUNC */
    FILE_OPEN_MODE_READ_WRITE_CREATE = 2,   /* O_RDWR | O_CREAT */
    FILE_OPEN_MODE_APPEND_CREATE = 3        /* O_WRONLY | O_CREAT | O_APPEND */
} FileOpenMode;

typedef enum FileSeekOrigin {
    FILE_SEEK_ORIGIN_START = 0,
    FILE_SEEK_ORIGIN_CURRENT = 1,
    FILE_SEEK_ORIGIN_END = 2
} FileSeekOrigin;

/* Access bits of a new file or directory: 0000 .. 0777. */
typedef u32 FilePermissions;

typedef enum FileKind {
    FILE_KIND_REGULAR = 0,
    FILE_KIND_DIRECTORY = 1,
    FILE_KIND_DEVICE = 2,
    FILE_KIND_OTHER = 3
} FileKind;

typedef struct FileInfo {
    u64 size;
    FileKind kind;
} FileInfo;

#define FILE_DEFAULT_PERMISSIONS ((FilePermissions)0644)
#define DIRECTORY_DEFAULT_PERMISSIONS ((FilePermissions)0755)

_Static_assert(sizeof(FileOpenMode) == 4, "FileOpenMode ABI mismatch");
_Static_assert(sizeof(FileSeekOrigin) == 4, "FileSeekOrigin ABI mismatch");
_Static_assert(sizeof(FileKind) == 4, "FileKind ABI mismatch");
_Static_assert(sizeof(FileInfo) == 16, "FileInfo ABI mismatch");

/*
 * Opens path in the given mode, creating it with permissions if the mode
 * creates files, and stores the handle in *out_file.
 *
 * Errors: CORE_INVALID_ARGUMENT if out_file is NULL, path is not a valid
 * String, mode is not a valid FileOpenMode, or permissions has bits outside
 * 0000..0777; codes from core_error_from_errno() if open() fails
 * (CORE_NOT_FOUND if the path does not exist and mode does not create it,
 * CORE_ALREADY_EXISTS is not raised here: open() does not use O_EXCL).
 */
ErrorCode file_open(const String *path, FileOpenMode mode, FilePermissions permissions,
                    FileHandle *out_file);

/*
 * Closes file through close() and sets *file to NULL.
 *
 * Errors: CORE_INVALID_ARGUMENT if file or *file is NULL; codes from
 * core_error_from_errno() if close() fails (the handle is still released).
 */
ErrorCode file_close(FileHandle *file);

/*
 * Reads into buffer through read(); stores the number of bytes actually
 * read in *out_read. 0 means end of file. May read fewer bytes than
 * buffer.size.
 *
 * Errors: CORE_INVALID_ARGUMENT if file or out_read is NULL, or buffer has
 * data == NULL with a non-zero size; codes from core_error_from_errno() if
 * read() fails.
 */
ErrorCode file_read(FileHandle file, Bytes buffer, usize *out_read);

/*
 * Writes every byte of data through one or more write() calls.
 *
 * Errors: CORE_INVALID_ARGUMENT if file is NULL, or data has data == NULL
 * with a non-zero size; codes from core_error_from_errno() if write() fails;
 * CORE_IO_ERROR if write() repeatedly returns 0 without making progress.
 */
ErrorCode file_write_all(FileHandle file, Bytes data);

/*
 * Moves the read/write position through lseek() and stores the new,
 * absolute position in *out_position.
 *
 * Errors: CORE_INVALID_ARGUMENT if file or out_position is NULL, or origin
 * is not a valid FileSeekOrigin; codes from core_error_from_errno() if
 * lseek() fails (e.g. CORE_INVALID_ARGUMENT if the resulting position would
 * be negative).
 */
ErrorCode file_seek(FileHandle file, i64 offset, FileSeekOrigin origin, u64 *out_position);

/*
 * Stores file's size and kind, read through fstat(), in *out_info.
 *
 * Errors: CORE_INVALID_ARGUMENT if file or out_info is NULL; codes from
 * core_error_from_errno() if fstat() fails.
 */
ErrorCode file_info(FileHandle file, FileInfo *out_info);

/*
 * Flushes file's written data to storage through fsync().
 *
 * Errors: CORE_INVALID_ARGUMENT if file is NULL; codes from
 * core_error_from_errno() if fsync() fails.
 */
ErrorCode file_flush(FileHandle file);

/*
 * Creates a directory at path with permissions through mkdir().
 *
 * Errors: CORE_INVALID_ARGUMENT if path is not a valid String or
 * permissions has bits outside 0000..0777; codes from
 * core_error_from_errno() if mkdir() fails (CORE_ALREADY_EXISTS if path
 * already exists, CORE_NOT_FOUND if a parent directory is missing).
 */
ErrorCode directory_create(const String *path, FilePermissions permissions);

/*
 * Removes the file at path through unlink(). Does not remove directories:
 * the specification names no directory-removal wrapper.
 *
 * Errors: CORE_INVALID_ARGUMENT if path is not a valid String; codes from
 * core_error_from_errno() if unlink() fails.
 */
ErrorCode file_remove(const String *path);

/*
 * Renames or moves old_path to new_path through rename().
 *
 * Errors: CORE_INVALID_ARGUMENT if old_path or new_path is not a valid
 * String; codes from core_error_from_errno() if rename() fails.
 */
ErrorCode file_rename(const String *old_path, const String *new_path);

#endif
