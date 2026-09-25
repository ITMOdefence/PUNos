#ifndef PUNOS_INTERNAL_RAW_SYSCALL_H
#define PUNOS_INTERNAL_RAW_SYSCALL_H

/*
 * Internal Linux x86_64 entry points. They return the kernel value unchanged:
 * successful values stay unchanged and failures remain in the range -4095..-1.
 */
long __punos_syscall0(long number);
long __punos_syscall1(long number, unsigned long argument1);
long __punos_syscall2(long number, unsigned long argument1,
                      unsigned long argument2);
long __punos_syscall3(long number, unsigned long argument1,
                      unsigned long argument2, unsigned long argument3);
long __punos_syscall4(long number, unsigned long argument1,
                      unsigned long argument2, unsigned long argument3,
                      unsigned long argument4);
long __punos_syscall5(long number, unsigned long argument1,
                      unsigned long argument2, unsigned long argument3,
                      unsigned long argument4, unsigned long argument5);
long __punos_syscall6(long number, unsigned long argument1,
                      unsigned long argument2, unsigned long argument3,
                      unsigned long argument4, unsigned long argument5,
                      unsigned long argument6);

#endif
