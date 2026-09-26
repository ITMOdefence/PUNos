/*
 * Compile-time checks of the libcore ABI against DevDoc/SystemLibs/libcore.md.
 * This file only has to compile; it produces no runnable program.
 */
#include <sys/core.h>

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)
#define IS_UNSIGNED(type) ((type)-1 > (type)0)

/* Syscall Wrappers types are re-exported, not redeclared. */
_Static_assert(SAME_TYPE((size_t)0, u64), "size_t must be u64");
_Static_assert(SAME_TYPE((ssize_t)0, i64), "ssize_t must be i64");
_Static_assert(SAME_TYPE((off_t)0, i64), "off_t must be i64");
_Static_assert(SAME_TYPE((pid_t)0, i32), "pid_t must be i32");
_Static_assert(SAME_TYPE((mode_t)0, u32), "mode_t must be u32");
_Static_assert(SAME_TYPE((socklen_t)0, u32), "socklen_t must be u32");
_Static_assert(SAME_TYPE((time_t)0, i64), "time_t must be i64");
_Static_assert(SAME_TYPE((clockid_t)0, i32), "clockid_t must be i32");

/* Fixed-width integers. */
_Static_assert(sizeof(u8) == 1 && IS_UNSIGNED(u8), "u8");
_Static_assert(sizeof(u16) == 2 && IS_UNSIGNED(u16), "u16");
_Static_assert(sizeof(u32) == 4 && IS_UNSIGNED(u32), "u32");
_Static_assert(sizeof(u64) == 8 && IS_UNSIGNED(u64), "u64");
_Static_assert(sizeof(i8) == 1 && !IS_UNSIGNED(i8), "i8");
_Static_assert(sizeof(i16) == 2 && !IS_UNSIGNED(i16), "i16");
_Static_assert(sizeof(i32) == 4 && !IS_UNSIGNED(i32), "i32");
_Static_assert(sizeof(i64) == 8 && !IS_UNSIGNED(i64), "i64");

/* Pointer-sized integers and their aliases. */
_Static_assert(SAME_TYPE((usize)0, size_t), "usize must be the same type as size_t");
_Static_assert(SAME_TYPE((isize)0, ssize_t), "isize must be the same type as ssize_t");
_Static_assert(IS_UNSIGNED(usize) && !IS_UNSIGNED(isize), "usize/isize signedness");
_Static_assert(SAME_TYPE((intptr_t)0, isize), "intptr_t must alias isize");
_Static_assert(SAME_TYPE((uintptr_t)0, usize), "uintptr_t must alias usize");

/* Linux ABI aliases declared by libcore. */
_Static_assert(SAME_TYPE((sa_family_t)0, u16), "sa_family_t must alias u16");
_Static_assert(SAME_TYPE((in_port_t)0, u16), "in_port_t must alias u16");
_Static_assert(SAME_TYPE((dev_t)0, u64), "dev_t must alias u64");
_Static_assert(SAME_TYPE((ino_t)0, u64), "ino_t must alias u64");
_Static_assert(SAME_TYPE((nlink_t)0, u64), "nlink_t must alias u64");
_Static_assert(sizeof(((struct stat *)0)->st_dev) == sizeof(dev_t), "dev_t vs struct stat");
_Static_assert(sizeof(((struct stat *)0)->st_ino) == sizeof(ino_t), "ino_t vs struct stat");
_Static_assert(sizeof(((struct stat *)0)->st_nlink) == sizeof(nlink_t), "nlink_t vs struct stat");

/* Limits. */
_Static_assert(USIZE_MAX == (usize)-1, "USIZE_MAX");
_Static_assert(ISIZE_MAX == (isize)(USIZE_MAX >> 1), "ISIZE_MAX");
_Static_assert(ISIZE_MIN == -ISIZE_MAX - 1, "ISIZE_MIN");
_Static_assert(SAME_TYPE(USIZE_MAX, usize), "USIZE_MAX type");
_Static_assert(SAME_TYPE(ISIZE_MIN, isize), "ISIZE_MIN type");
_Static_assert(SAME_TYPE(ISIZE_MAX, isize), "ISIZE_MAX type");

/* bool and f64. */
_Static_assert(IS_UNSIGNED(bool), "bool must be unsigned");
_Static_assert(true == 1 && false == 0, "true/false values");
_Static_assert(SAME_TYPE(true, bool) && SAME_TYPE(false, bool), "true/false type");
_Static_assert(sizeof(f64) == sizeof(double), "f64");

/* Bytes layout. */
_Static_assert(__builtin_offsetof(Bytes, data) == 0, "Bytes.data offset");
_Static_assert(__builtin_offsetof(Bytes, size) == 8, "Bytes.size offset");
_Static_assert(SAME_TYPE(((Bytes *)0)->size, usize), "Bytes.size type");

/* ErrorCode values are ABI and must never be renumbered. */
_Static_assert(CORE_OK == 0, "CORE_OK");
_Static_assert(CORE_INVALID_ARGUMENT == 1, "CORE_INVALID_ARGUMENT");
_Static_assert(CORE_INVALID_PATH == 2, "CORE_INVALID_PATH");
_Static_assert(CORE_INVALID_STATE == 3, "CORE_INVALID_STATE");
_Static_assert(CORE_OUT_OF_MEMORY == 4, "CORE_OUT_OF_MEMORY");
_Static_assert(CORE_NOT_FOUND == 5, "CORE_NOT_FOUND");
_Static_assert(CORE_ALREADY_EXISTS == 6, "CORE_ALREADY_EXISTS");
_Static_assert(CORE_PERMISSION_DENIED == 7, "CORE_PERMISSION_DENIED");
_Static_assert(CORE_IO_ERROR == 8, "CORE_IO_ERROR");
_Static_assert(CORE_TIMEOUT == 9, "CORE_TIMEOUT");
_Static_assert(CORE_INTERRUPTED == 10, "CORE_INTERRUPTED");
_Static_assert(CORE_WOULD_BLOCK == 11, "CORE_WOULD_BLOCK");
_Static_assert(CORE_UNSUPPORTED == 12, "CORE_UNSUPPORTED");
_Static_assert(CORE_OVERFLOW == 13, "CORE_OVERFLOW");

/* NULL is a null pointer constant. */
_Static_assert(SAME_TYPE(NULL, void *), "NULL type");
