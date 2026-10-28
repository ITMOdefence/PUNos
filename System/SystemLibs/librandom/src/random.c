#include <sys/random.h>

#include <punos/syscall/syscall.h>

ErrorCode random_fill(Bytes destination)
{
    usize offset = 0;

    if (destination.data == NULL && destination.size != 0) {
        return CORE_INVALID_ARGUMENT;
    }
    while (offset < destination.size) {
        ssize_t result = getrandom(destination.data + offset,
                                   destination.size - offset, 0);

        if (result > 0) {
            offset += (usize)result;
            continue;
        }
        if (result == 0) {
            return CORE_IO_ERROR;
        }
        if (errno == EINTR) {
            continue;
        }
        return core_error_from_errno(errno);
    }
    return CORE_OK;
}
