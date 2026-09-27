#include <sys/thread.h>

#include <punos/syscall/syscall.h>

#include "internal.h"

/* clone() flags required by the specification (contract 15). */
#define THREAD_CLONE_FLAGS                                                    \
    (CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |       \
     CLONE_SYSVSEM | CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_SETTID | \
     CLONE_CHILD_CLEARTID)

/*
 * First C code of a new thread, called by the assembly trampoline of clone()
 * with the handle as argument. The trampoline calls thread_exit() with the
 * value returned here (contracts 1 and 9).
 */
static i32 thread_trampoline(void *context)
{
    ThreadHandle handle = context;
    ThreadExitStatus result = handle->entry(handle->argument);

    /*
     * Publish the result for thread_join(), which reads it on another thread.
     * The release store pairs with the acquire load there, so the read is
     * ordered after this write without a data race.
     */
    __atomic_store_n(&handle->result, result, __ATOMIC_RELEASE);
    return result;
}

static void release(ThreadHandle handle)
{
    MemoryBlock stack = handle->stack;
    MemoryBlock handle_block = handle->handle_block;

    (void)memory_free(&stack);
    (void)memory_free(&handle_block);
}

ErrorCode thread_current_id(ThreadId *out_thread_id)
{
    if (out_thread_id == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_thread_id = gettid();
    return CORE_OK;
}

ErrorCode thread_create(ThreadEntry entry, void *argument, ThreadHandle *out_thread)
{
    MemoryBlock handle_block;
    MemoryBlock stack;
    ThreadHandle handle;
    ErrorCode result;
    pid_t tid;

    if (entry == NULL || out_thread == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    result = memory_allocate(sizeof(struct ThreadHandle), &handle_block);
    if (result != CORE_OK) {
        return result;
    }
    result = memory_allocate(LIBTHREAD_INTERNAL_STACK_SIZE, &stack);
    if (result != CORE_OK) {
        (void)memory_free(&handle_block);
        return result;
    }

    /* The block is zero-filled, so tid and result start at 0. */
    handle = (ThreadHandle)handle_block.data;
    handle->entry = entry;
    handle->argument = argument;
    handle->handle_block = handle_block;
    handle->stack = stack;
    handle->state.self = &handle->state;
    handle->state.errno_value = 0;
    handle->state.reserved = 0;

    /*
     * The stack top is the end of the block; clone() aligns it down to 16
     * bytes itself. The same tid field is the parent and child tid pointer.
     */
    tid = clone(thread_trampoline, stack.data + stack.size, THREAD_CLONE_FLAGS,
                handle, (i32 *)&handle->tid, &handle->state, (i32 *)&handle->tid);
    if (tid < 0) {
        ErrorCode error = core_error_from_errno(errno);

        release(handle);
        return error;
    }

    *out_thread = handle;
    return CORE_OK;
}

ErrorCode thread_join(ThreadHandle *thread, ThreadExitStatus *out_status)
{
    ThreadHandle handle;
    i32 tid;

    if (thread == NULL || *thread == NULL || out_status == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    handle = *thread;
    if (handle->tid != 0 && handle->tid == gettid()) {
        return CORE_INVALID_STATE;
    }

    /*
     * Contracts 16-17: wait until the kernel clears tid. futex(FUTEX_WAIT)
     * sleeps only while the word still holds the tid we saw; EAGAIN (the value
     * changed first) and EINTR (a signal) just mean "check again".
     */
    while ((tid = __atomic_load_n(&handle->tid, __ATOMIC_ACQUIRE)) != 0) {
        if (futex((u32 *)&handle->tid, FUTEX_WAIT, (u32)tid, NULL, NULL, 0) != 0) {
            i32 error = errno;

            if (error != EAGAIN && error != EINTR) {
                return core_error_from_errno(error);
            }
        }
    }

    /* The thread has exited: its result is final and its stack is unused. */
    *out_status = __atomic_load_n(&handle->result, __ATOMIC_ACQUIRE);
    release(handle);
    *thread = NULL;
    return CORE_OK;
}
