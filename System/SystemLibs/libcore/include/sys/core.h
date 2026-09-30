#ifndef PUNOS_SYS_CORE_H
#define PUNOS_SYS_CORE_H

/*
 * libcore (sys/core) - base types, error codes and size checks shared by
 * every native PUNos library, System Service and application.
 *
 * Specification: DevDoc/SystemLibs/libcore.md
 *
 * libcore does not depend on libc or on other System Libs. Machine and Linux
 * ABI types (u8-u64, i8-i64, size_t, ssize_t, off_t, pid_t, mode_t,
 * socklen_t, time_t, clockid_t) come from the Syscall Wrappers type header
 * and are re-exported here, never redeclared. This is a dependency on the
 * type contract only: libcore does not call or link any syscall wrapper.
 *
 * Conventions for every operation that can fail:
 *   - it returns ErrorCode, and CORE_OK is the only success value;
 *   - results are written through out_* pointers;
 *   - on error the contents of out_* are unspecified and must not be used.
 */

#include <punos/syscall/types.h>

/* ------------------------------------------------------------------------ */
/* Platform types                                                           */
/* ------------------------------------------------------------------------ */

/*
 * Pointer-sized integers for Linux x86_64. usize is the same type as size_t
 * (both are u64), so values move between them without conversions.
 */
typedef u64 usize;
typedef i64 isize;

typedef isize intptr_t;
typedef usize uintptr_t;

/* Linux ABI aliases that are not declared by Syscall Wrappers. */
typedef u16 sa_family_t; /* AF_* socket address family */
typedef u16 in_port_t;   /* TCP/IP port, network byte order */
typedef u64 dev_t;       /* device id, matches struct stat st_dev/st_rdev */
typedef u64 ino_t;       /* inode number, matches struct stat st_ino */
typedef u64 nlink_t;     /* hard link count, matches struct stat st_nlink */

/* IEEE 754 binary64. */
typedef double f64;

/*
 * Boolean stored in one byte. C11 _Bool is an 8-bit unsigned integer type on
 * Linux x86_64 and normalises every non-zero value to 1, so a bool always
 * holds exactly false or true.
 */
typedef _Bool bool;

/*
 * Non-owning view of a byte sequence. data may be NULL only when size is 0.
 * Whoever produced the view owns the memory and keeps it alive while the view
 * is in use.
 */
typedef struct Bytes {
    u8 *data;
    usize size;
} Bytes;

/*
 * Result of an operation that can fail.
 *
 * The numeric values are part of the PUNos ABI: they may cross process
 * boundaries (IPC, logs), so existing values must never be renumbered.
 * New codes are appended at the end.
 */
typedef enum ErrorCode {
    CORE_OK = 0,                 /* operation succeeded */
    CORE_INVALID_ARGUMENT = 1,   /* an argument is not acceptable */
    CORE_INVALID_PATH = 2,       /* a path has an invalid form */
    CORE_INVALID_STATE = 3,      /* not allowed in the object's current state */
    CORE_OUT_OF_MEMORY = 4,      /* not enough memory */
    CORE_NOT_FOUND = 5,          /* the requested object does not exist */
    CORE_ALREADY_EXISTS = 6,     /* an object with this name already exists */
    CORE_PERMISSION_DENIED = 7,  /* insufficient permissions */
    CORE_IO_ERROR = 8,           /* input/output error, or any other failure */
    CORE_TIMEOUT = 9,            /* the operation's timeout expired */
    CORE_INTERRUPTED = 10,       /* interrupted by a signal */
    CORE_WOULD_BLOCK = 11,       /* the operation would have to block */
    CORE_UNSUPPORTED = 12,       /* the operation is not supported */
    CORE_OVERFLOW = 13           /* result does not fit the target type */
} ErrorCode;

/* ------------------------------------------------------------------------ */
/* Constants                                                                */
/* ------------------------------------------------------------------------ */

#define NULL ((void *)0)

#define true ((bool)1)
#define false ((bool)0)

#define USIZE_MAX ((usize)0xffffffffffffffffULL)
#define ISIZE_MAX ((isize)0x7fffffffffffffffLL)
#define ISIZE_MIN (-ISIZE_MAX - 1)

/* ------------------------------------------------------------------------ */
/* ABI checks                                                               */
/* ------------------------------------------------------------------------ */

_Static_assert(sizeof(usize) == sizeof(void *), "usize must be pointer-sized");
_Static_assert(sizeof(isize) == sizeof(void *), "isize must be pointer-sized");
_Static_assert(sizeof(bool) == 1, "bool must be 8 bits");
_Static_assert(sizeof(f64) == 8, "f64 must be IEEE 754 binary64");
_Static_assert(sizeof(ErrorCode) == 4, "ErrorCode ABI mismatch");
_Static_assert(sizeof(Bytes) == 16, "Bytes ABI mismatch");

/* ------------------------------------------------------------------------ */
/* Operations                                                               */
/* ------------------------------------------------------------------------ */

/* Returns true only if code is CORE_OK. */
bool core_is_ok(ErrorCode code);

/*
 * Stores left + right in *out_value.
 *
 * Returns CORE_OVERFLOW if the sum does not fit in usize and
 * CORE_INVALID_ARGUMENT if out_value is NULL.
 */
ErrorCode core_size_add(usize left, usize right, usize *out_value);

/*
 * Stores left * right in *out_value.
 *
 * Returns CORE_OVERFLOW if the product does not fit in usize and
 * CORE_INVALID_ARGUMENT if out_value is NULL.
 */
ErrorCode core_size_multiply(usize left, usize right, usize *out_value);

/*
 * Converts a Linux errno value to an ErrorCode.
 *
 * The caller passes the value explicitly: this function never reads the
 * global errno. System Libs save errno immediately after a failed wrapper,
 * before calling any other wrapper, and pass the saved value here.
 *
 *   0                        -> CORE_OK (not an error)
 *   1 ... 4095, mapped       -> see the table in DevDoc/SystemLibs/libcore.md
 *   1 ... 4095, not mapped   -> CORE_IO_ERROR
 *   negative or above 4095   -> CORE_INVALID_ARGUMENT (not a Linux errno)
 */
ErrorCode core_error_from_errno(i32 errno_value);

#endif
