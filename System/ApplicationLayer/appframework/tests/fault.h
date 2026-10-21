#ifndef PUNOS_APPFRAMEWORK_TESTS_FAULT_H
#define PUNOS_APPFRAMEWORK_TESTS_FAULT_H

/* Fault injection for the runtime tests: tests/fault_fake.c wraps (--wrap)
 * three functions and passes the calls through until a test arms a failure. */

#include <sys/core.h>
#include <punos/syscall/types.h>

typedef struct FaultPlan {
    /* memory_allocate()/memory_reallocate(): the Nth call from now fails
     * (1 = the next one); 0 = never. */
    u32 fail_allocation_at;
    /* eventloop_wait() returns this instead of waiting, once; CORE_OK = off. */
    ErrorCode wait_error;
    /* sigaction(): fails with this errno once; 0 = off. */
    i32 sigaction_errno;
    /* eventloop_wake(): fails once, leaving this errno behind; 0 = off. */
    i32 wake_errno;
    /* ipc_connection_finish_connect(): reports Connecting this many times
     * before the real call is made. */
    u32 connecting_polls;
    /* ipc_connection_send(): fails once with this code; CORE_OK = off. */
    ErrorCode send_error;
    /* ipc_connection_flush(): fails once with this code; CORE_OK = off. */
    ErrorCode flush_error;
    /* Counters, always on. */
    u32 allocations;
    u32 frees;
    u32 reallocations;
    u32 waits; /* eventloop_wait() calls */
} FaultPlan;

extern FaultPlan fault_plan;

#endif
