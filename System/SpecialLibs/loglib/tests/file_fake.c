#include <sys/file.h>

static bool fail_open;
static bool fail_close;

ErrorCode __real_file_open(const String *path, FileOpenMode mode, u32 permissions,
                           FileHandle *out_file);
ErrorCode __real_file_close(FileHandle *file);

void fake_file_fail_open(void) { fail_open = true; }
void fake_file_fail_close(void) { fail_close = true; }

ErrorCode __wrap_file_open(const String *path, FileOpenMode mode, u32 permissions,
                           FileHandle *out_file)
{
    if (fail_open) {
        fail_open = false;
        return CORE_IO_ERROR;
    }
    return __real_file_open(path, mode, permissions, out_file);
}

ErrorCode __wrap_file_close(FileHandle *file)
{
    ErrorCode result = __real_file_close(file);
    /* Preserve libfile's destructive-close contract even on the injected
     * error. Leaving the handle alive here would conceal the rotation bug. */
    if (fail_close) {
        fail_close = false;
        return CORE_IO_ERROR;
    }
    return result;
}
