#ifndef PUNOS_SYS_RANDOM_H
#define PUNOS_SYS_RANDOM_H

/* Secure bytes supplied by the Linux getrandom() syscall. */
#include <sys/core.h>

/*
 * Fills every byte in destination with kernel-provided cryptographic random
 * data. Empty buffers succeed. The function retries partial reads and EINTR;
 * it never returns CORE_OK with a partially filled buffer.
 */
ErrorCode random_fill(Bytes destination);

#endif
