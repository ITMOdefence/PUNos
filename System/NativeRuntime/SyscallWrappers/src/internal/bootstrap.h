#ifndef PUNOS_INTERNAL_BOOTSTRAP_H
#define PUNOS_INTERNAL_BOOTSTRAP_H

/* These functions are valid before RuntimeThreadState and errno exist. */
long __punos_raw_arch_set_fs(void *thread_state);
_Noreturn void __punos_raw_exit_group(int status);

#endif
