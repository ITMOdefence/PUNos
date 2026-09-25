#ifndef PUNOS_RUNTIME_THREAD_STATE_H
#define PUNOS_RUNTIME_THREAD_STATE_H

#include <punos/syscall/types.h>

struct RuntimeThreadState {
    struct RuntimeThreadState *self;
    i32 errno_value;
    i32 reserved;
};

_Static_assert(sizeof(struct RuntimeThreadState) == 16,
               "RuntimeThreadState ABI mismatch");
_Static_assert(__builtin_offsetof(struct RuntimeThreadState, self) == 0,
               "RuntimeThreadState self offset mismatch");
_Static_assert(__builtin_offsetof(struct RuntimeThreadState, errno_value) == 8,
               "RuntimeThreadState errno offset mismatch");
_Static_assert(__builtin_offsetof(struct RuntimeThreadState, reserved) == 12,
               "RuntimeThreadState reserved offset mismatch");

#endif
