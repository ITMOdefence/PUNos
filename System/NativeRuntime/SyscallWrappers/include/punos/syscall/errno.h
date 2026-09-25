#ifndef PUNOS_SYSCALL_ERRNO_H
#define PUNOS_SYSCALL_ERRNO_H

#include <punos/syscall/types.h>

i32 *sys_errno_location(void);

#define errno (*sys_errno_location())

#endif
