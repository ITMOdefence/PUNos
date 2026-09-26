#include <sys/core.h>

#include <punos/syscall/constants.h>

/*
 * Largest value Linux uses as an errno (MAX_ERRNO in the kernel). Syscall
 * Wrappers treat raw results in [-4095, -1] as errors for the same reason.
 */
#define CORE_MAX_ERRNO 4095

bool core_is_ok(ErrorCode code)
{
    return code == CORE_OK;
}

ErrorCode core_size_add(usize left, usize right, usize *out_value)
{
    usize sum;

    if (out_value == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (__builtin_add_overflow(left, right, &sum)) {
        return CORE_OVERFLOW;
    }
    *out_value = sum;
    return CORE_OK;
}

ErrorCode core_size_multiply(usize left, usize right, usize *out_value)
{
    usize product;

    if (out_value == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (__builtin_mul_overflow(left, right, &product)) {
        return CORE_OVERFLOW;
    }
    *out_value = product;
    return CORE_OK;
}

ErrorCode core_error_from_errno(i32 errno_value)
{
    if (errno_value == 0) {
        return CORE_OK;
    }
    if (errno_value < 0 || errno_value > CORE_MAX_ERRNO) {
        return CORE_INVALID_ARGUMENT;
    }

    switch (errno_value) {
    case EINVAL:
        return CORE_INVALID_ARGUMENT;
    case EISDIR:
    case ENOTDIR:
        return CORE_INVALID_PATH;
    case ENOENT:
    case ESRCH:
    case ECHILD:
    case ENODEV:
        return CORE_NOT_FOUND;
    case EEXIST:
    case EADDRINUSE:
        return CORE_ALREADY_EXISTS;
    case EPERM:
    case EACCES:
        return CORE_PERMISSION_DENIED;
    case ENOMEM:
        return CORE_OUT_OF_MEMORY;
    case ETIMEDOUT:
        return CORE_TIMEOUT;
    case EINTR:
        return CORE_INTERRUPTED;
    case EAGAIN:
    case EINPROGRESS:
        return CORE_WOULD_BLOCK;
    case ENOSYS:
    case ENOTTY:
    case EOPNOTSUPP:
        return CORE_UNSUPPORTED;
    case EOVERFLOW:
        return CORE_OVERFLOW;
    default:
        return CORE_IO_ERROR;
    }
}
