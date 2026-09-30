#include <sys/file.h>

#include <sys/memory.h>

#include <punos/syscall/syscall.h>

/*
 * A handle is a small structure in its own libmem block, the same trick
 * libprocess and libthread use for their handles.
 */
struct FileHandle {
    MemoryBlock block;
    i32 fd;
};

static ErrorCode last_error(void)
{
    return core_error_from_errno(errno);
}

static bool bytes_is_valid(Bytes bytes)
{
    return bytes.size == 0 || bytes.data != NULL;
}

static bool permissions_is_valid(FilePermissions permissions)
{
    return (permissions & ~(FilePermissions)0777) == 0;
}

static i32 mode_flags(FileOpenMode mode)
{
    switch (mode) {
    case FILE_OPEN_MODE_READ:
        return O_RDONLY;
    case FILE_OPEN_MODE_WRITE_TRUNCATE:
        return O_WRONLY | O_CREAT | O_TRUNC;
    case FILE_OPEN_MODE_READ_WRITE_CREATE:
        return O_RDWR | O_CREAT;
    case FILE_OPEN_MODE_APPEND_CREATE:
        return O_WRONLY | O_CREAT | O_APPEND;
    default:
        return -1;
    }
}

static i32 seek_whence(FileSeekOrigin origin)
{
    switch (origin) {
    case FILE_SEEK_ORIGIN_START:
        return SEEK_SET;
    case FILE_SEEK_ORIGIN_CURRENT:
        return SEEK_CUR;
    case FILE_SEEK_ORIGIN_END:
        return SEEK_END;
    default:
        return -1;
    }
}

static FileKind kind_from_mode(mode_t mode)
{
    switch (mode & S_IFMT) {
    case S_IFREG:
        return FILE_KIND_REGULAR;
    case S_IFDIR:
        return FILE_KIND_DIRECTORY;
    case S_IFCHR:
    case S_IFBLK:
        return FILE_KIND_DEVICE;
    default:
        return FILE_KIND_OTHER;
    }
}

ErrorCode file_open(const String *path, FileOpenMode mode, FilePermissions permissions,
                    FileHandle *out_file)
{
    const char *path_data;
    i32 flags;
    i32 fd;
    MemoryBlock handle_block;
    FileHandle handle;
    ErrorCode result;

    if (out_file == NULL || string_c_str(path, &path_data) != CORE_OK) {
        return CORE_INVALID_ARGUMENT;
    }
    flags = mode_flags(mode);
    if (flags < 0 || !permissions_is_valid(permissions)) {
        return CORE_INVALID_ARGUMENT;
    }

    fd = open(path_data, flags, permissions);
    if (fd < 0) {
        return last_error();
    }

    result = memory_allocate(sizeof(struct FileHandle), &handle_block);
    if (result != CORE_OK) {
        (void)close(fd);
        return result;
    }
    handle = (FileHandle)handle_block.data;
    handle->block = handle_block;
    handle->fd = fd;
    *out_file = handle;
    return CORE_OK;
}

ErrorCode file_close(FileHandle *file)
{
    FileHandle handle;
    MemoryBlock block;
    ErrorCode result = CORE_OK;

    if (file == NULL || *file == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    handle = *file;
    if (close(handle->fd) != 0) {
        result = last_error();
    }
    block = handle->block;
    (void)memory_free(&block);
    *file = NULL;
    return result;
}

ErrorCode file_read(FileHandle file, Bytes buffer, usize *out_read)
{
    ssize_t result;

    if (file == NULL || out_read == NULL || !bytes_is_valid(buffer)) {
        return CORE_INVALID_ARGUMENT;
    }
    if (buffer.size == 0) {
        *out_read = 0;
        return CORE_OK;
    }
    result = read(file->fd, buffer.data, buffer.size);
    if (result < 0) {
        return last_error();
    }
    *out_read = (usize)result;
    return CORE_OK;
}

ErrorCode file_write_all(FileHandle file, Bytes data)
{
    usize written = 0;

    if (file == NULL || !bytes_is_valid(data)) {
        return CORE_INVALID_ARGUMENT;
    }
    while (written < data.size) {
        ssize_t result = write(file->fd, data.data + written, data.size - written);

        if (result < 0) {
            return last_error();
        }
        if (result == 0) {
            return CORE_IO_ERROR; /* no progress: never spin forever */
        }
        written += (usize)result;
    }
    return CORE_OK;
}

ErrorCode file_seek(FileHandle file, i64 offset, FileSeekOrigin origin, u64 *out_position)
{
    i32 whence;
    i64 result;

    if (file == NULL || out_position == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    whence = seek_whence(origin);
    if (whence < 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = lseek(file->fd, offset, whence);
    if (result < 0) {
        return last_error();
    }
    *out_position = (u64)result;
    return CORE_OK;
}

ErrorCode file_info(FileHandle file, FileInfo *out_info)
{
    struct stat status;

    if (file == NULL || out_info == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (fstat(file->fd, &status) != 0) {
        return last_error();
    }
    out_info->size = (u64)status.st_size;
    out_info->kind = kind_from_mode(status.st_mode);
    return CORE_OK;
}

ErrorCode file_flush(FileHandle file)
{
    if (file == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (fsync(file->fd) != 0) {
        return last_error();
    }
    return CORE_OK;
}

ErrorCode directory_create(const String *path, FilePermissions permissions)
{
    const char *path_data;

    if (string_c_str(path, &path_data) != CORE_OK || !permissions_is_valid(permissions)) {
        return CORE_INVALID_ARGUMENT;
    }
    if (mkdir(path_data, permissions) != 0) {
        return last_error();
    }
    return CORE_OK;
}

ErrorCode file_remove(const String *path)
{
    const char *path_data;

    if (string_c_str(path, &path_data) != CORE_OK) {
        return CORE_INVALID_ARGUMENT;
    }
    if (unlink(path_data) != 0) {
        return last_error();
    }
    return CORE_OK;
}

ErrorCode file_rename(const String *old_path, const String *new_path)
{
    const char *old_data;
    const char *new_data;

    if (string_c_str(old_path, &old_data) != CORE_OK ||
        string_c_str(new_path, &new_data) != CORE_OK) {
        return CORE_INVALID_ARGUMENT;
    }
    if (rename(old_data, new_data) != 0) {
        return last_error();
    }
    return CORE_OK;
}
