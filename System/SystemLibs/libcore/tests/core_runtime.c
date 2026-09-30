/*
 * Runtime test of the libcore operations against DevDoc/SystemLibs/libcore.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 */
#define CHECK_TEST_NAME "libcore"
#include "../../testing/check.h"

static void test_is_ok(void)
{
    EXPECT(1, core_is_ok(CORE_OK) == true);
    EXPECT(2, core_is_ok(CORE_INVALID_ARGUMENT) == false);
    EXPECT(3, core_is_ok(CORE_OVERFLOW) == false);
    EXPECT(4, core_is_ok((ErrorCode)-1) == false);
}

static void test_size_add(void)
{
    usize value = 0;

    EXPECT(10, core_size_add(0, 0, &value) == CORE_OK && value == 0);
    EXPECT(11, core_size_add(40, 2, &value) == CORE_OK && value == 42);
    EXPECT(12, core_size_add(USIZE_MAX - 1, 1, &value) == CORE_OK &&
                   value == USIZE_MAX);
    EXPECT(13, core_size_add(USIZE_MAX, 0, &value) == CORE_OK &&
                   value == USIZE_MAX);
    EXPECT(14, core_size_add(USIZE_MAX, 1, &value) == CORE_OVERFLOW);
    EXPECT(15, core_size_add(USIZE_MAX, USIZE_MAX, &value) == CORE_OVERFLOW);
    EXPECT(16, core_size_add(1, 2, NULL) == CORE_INVALID_ARGUMENT);
}

static void test_size_multiply(void)
{
    usize value = 0;
    const usize half = (usize)1 << 32;

    EXPECT(20, core_size_multiply(0, USIZE_MAX, &value) == CORE_OK &&
                   value == 0);
    EXPECT(21, core_size_multiply(6, 7, &value) == CORE_OK && value == 42);
    EXPECT(22, core_size_multiply(USIZE_MAX, 1, &value) == CORE_OK &&
                   value == USIZE_MAX);
    EXPECT(23, core_size_multiply(half - 1, half + 1, &value) == CORE_OK &&
                   value == USIZE_MAX);
    EXPECT(24, core_size_multiply(half, half, &value) == CORE_OVERFLOW);
    EXPECT(25, core_size_multiply(USIZE_MAX, 2, &value) == CORE_OVERFLOW);
    EXPECT(26, core_size_multiply(2, 3, NULL) == CORE_INVALID_ARGUMENT);
}

static void test_error_from_errno(void)
{
    /* Every row of the conversion table in the specification. */
    EXPECT(30, core_error_from_errno(0) == CORE_OK);
    EXPECT(31, core_error_from_errno(EINVAL) == CORE_INVALID_ARGUMENT);
    EXPECT(32, core_error_from_errno(EISDIR) == CORE_INVALID_PATH);
    EXPECT(33, core_error_from_errno(ENOTDIR) == CORE_INVALID_PATH);
    EXPECT(34, core_error_from_errno(ENOENT) == CORE_NOT_FOUND);
    EXPECT(35, core_error_from_errno(ESRCH) == CORE_NOT_FOUND);
    EXPECT(36, core_error_from_errno(ECHILD) == CORE_NOT_FOUND);
    EXPECT(37, core_error_from_errno(ENODEV) == CORE_NOT_FOUND);
    EXPECT(38, core_error_from_errno(EEXIST) == CORE_ALREADY_EXISTS);
    EXPECT(39, core_error_from_errno(EADDRINUSE) == CORE_ALREADY_EXISTS);
    EXPECT(40, core_error_from_errno(EPERM) == CORE_PERMISSION_DENIED);
    EXPECT(41, core_error_from_errno(EACCES) == CORE_PERMISSION_DENIED);
    EXPECT(42, core_error_from_errno(ENOMEM) == CORE_OUT_OF_MEMORY);
    EXPECT(43, core_error_from_errno(ETIMEDOUT) == CORE_TIMEOUT);
    EXPECT(44, core_error_from_errno(EINTR) == CORE_INTERRUPTED);
    EXPECT(45, core_error_from_errno(EAGAIN) == CORE_WOULD_BLOCK);
    EXPECT(46, core_error_from_errno(EWOULDBLOCK) == CORE_WOULD_BLOCK);
    EXPECT(47, core_error_from_errno(EINPROGRESS) == CORE_WOULD_BLOCK);
    EXPECT(48, core_error_from_errno(ENOSYS) == CORE_UNSUPPORTED);
    EXPECT(49, core_error_from_errno(ENOTTY) == CORE_UNSUPPORTED);
    EXPECT(50, core_error_from_errno(EOPNOTSUPP) == CORE_UNSUPPORTED);
    EXPECT(51, core_error_from_errno(EOVERFLOW) == CORE_OVERFLOW);

    /* Positive errno values without a dedicated row. */
    EXPECT(60, core_error_from_errno(EIO) == CORE_IO_ERROR);
    EXPECT(61, core_error_from_errno(EBADF) == CORE_IO_ERROR);
    EXPECT(62, core_error_from_errno(EPIPE) == CORE_IO_ERROR);
    EXPECT(63, core_error_from_errno(ENOSPC) == CORE_IO_ERROR);
    EXPECT(64, core_error_from_errno(4095) == CORE_IO_ERROR);

    /* Values that are not Linux errno codes. */
    EXPECT(70, core_error_from_errno(-1) == CORE_INVALID_ARGUMENT);
    EXPECT(71, core_error_from_errno(-EINVAL) == CORE_INVALID_ARGUMENT);
    EXPECT(72, core_error_from_errno((i32)0x80000000u) == CORE_INVALID_ARGUMENT);
    EXPECT(73, core_error_from_errno(4096) == CORE_INVALID_ARGUMENT);
    EXPECT(74, core_error_from_errno(0x7fffffff) == CORE_INVALID_ARGUMENT);
}

static void test_errno_is_not_read(void)
{
    /* The conversion uses only its argument, never the global errno. */
    errno = ENOMEM;
    EXPECT(80, core_error_from_errno(0) == CORE_OK);
    EXPECT(81, core_error_from_errno(EACCES) == CORE_PERMISSION_DENIED);
    EXPECT(82, errno == ENOMEM);
}

static void test_types(void)
{
    u8 buffer[3] = { 1, 2, 3 };
    Bytes bytes = { .data = buffer, .size = sizeof(buffer) };
    Bytes empty = { .data = NULL, .size = 0 };
    bool flag = (bool)256;

    EXPECT(90, bytes.data[2] == 3 && bytes.size == 3);
    EXPECT(91, empty.data == NULL && empty.size == 0);
    /* bool normalises any non-zero value to true. */
    EXPECT(92, flag == true);
    EXPECT(93, (usize)(uintptr_t)buffer == (usize)buffer);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    test_is_ok();
    test_size_add();
    test_size_multiply();
    test_error_from_errno();
    test_errno_is_not_read();
    test_types();

    check_passed();
    return 0;
}
