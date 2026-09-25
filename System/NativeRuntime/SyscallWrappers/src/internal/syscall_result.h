#ifndef PUNOS_INTERNAL_SYSCALL_RESULT_H
#define PUNOS_INTERNAL_SYSCALL_RESULT_H

/* Converts a raw kernel error to -1 and stores its positive errno value. */
long __punos_syscall_result(long result);

#endif
