#include <sys/eventloop.h>
#include <sys/memory.h>
#include <special/ipc.h>
#include <punos/syscall/syscall.h>

#include "fault.h"

FaultPlan fault_plan;

ErrorCode __real_memory_allocate(usize size, MemoryBlock *out_block);
ErrorCode __real_memory_reallocate(MemoryBlock *block, usize new_size);
ErrorCode __real_memory_free(MemoryBlock *block);
ErrorCode __real_eventloop_wait(EventLoopHandle loop, Event *events, usize capacity,
                                DurationMs timeout, usize *out_count);
ErrorCode __real_eventloop_wake(EventLoopHandle loop);
ErrorCode __real_ipc_connection_finish_connect(IpcConnectionHandle connection,
                                               IpcConnectionState *out_state);
ErrorCode __real_ipc_connection_send(IpcConnectionHandle connection, const IpcMessage *message);
ErrorCode __real_ipc_connection_flush(IpcConnectionHandle connection, bool *out_has_pending);
i32 __real_sigaction(i32 signal, const struct sigaction *action, struct sigaction *old_action);

static bool should_fail_allocation(void)
{
    if (fault_plan.fail_allocation_at == 0) {
        return false;
    }
    return --fault_plan.fail_allocation_at == 0;
}

ErrorCode __wrap_memory_allocate(usize size, MemoryBlock *out_block)
{
    if (should_fail_allocation()) {
        return CORE_OUT_OF_MEMORY;
    }
    if (size != 0) {
        fault_plan.allocations++;
    }
    return __real_memory_allocate(size, out_block);
}

ErrorCode __wrap_memory_reallocate(MemoryBlock *block, usize new_size)
{
    if (should_fail_allocation()) {
        return CORE_OUT_OF_MEMORY;
    }
    if (block != NULL && block->data == NULL && new_size != 0) {
        fault_plan.allocations++;
    }
    fault_plan.reallocations++;
    return __real_memory_reallocate(block, new_size);
}

ErrorCode __wrap_memory_free(MemoryBlock *block)
{
    if (block != NULL && block->data != NULL) {
        fault_plan.frees++;
    }
    return __real_memory_free(block);
}

ErrorCode __wrap_eventloop_wait(EventLoopHandle loop, Event *events, usize capacity,
                                DurationMs timeout, usize *out_count)
{
    fault_plan.waits++;
    if (fault_plan.wait_error != CORE_OK) {
        ErrorCode error = fault_plan.wait_error;

        fault_plan.wait_error = CORE_OK;
        return error;
    }
    return __real_eventloop_wait(loop, events, capacity, timeout, out_count);
}

i32 __wrap_sigaction(i32 signal, const struct sigaction *action, struct sigaction *old_action)
{
    if (fault_plan.sigaction_errno != 0) {
        errno = fault_plan.sigaction_errno;
        fault_plan.sigaction_errno = 0;
        return -1;
    }
    return __real_sigaction(signal, action, old_action);
}

ErrorCode __wrap_eventloop_wake(EventLoopHandle loop)
{
    if (fault_plan.wake_errno != 0) {
        errno = fault_plan.wake_errno; /* what a failing write() leaves behind */
        fault_plan.wake_errno = 0;
        return CORE_IO_ERROR;
    }
    return __real_eventloop_wake(loop);
}

ErrorCode __wrap_ipc_connection_finish_connect(IpcConnectionHandle connection,
                                               IpcConnectionState *out_state)
{
    if (fault_plan.connecting_polls != 0 && out_state != NULL) {
        fault_plan.connecting_polls--;
        *out_state = IPC_CONNECTION_STATE_CONNECTING; /* the connection itself is untouched */
        return CORE_OK;
    }
    return __real_ipc_connection_finish_connect(connection, out_state);
}

ErrorCode __wrap_ipc_connection_send(IpcConnectionHandle connection, const IpcMessage *message)
{
    if (fault_plan.send_error != CORE_OK) {
        ErrorCode error = fault_plan.send_error;

        fault_plan.send_error = CORE_OK;
        return error;
    }
    return __real_ipc_connection_send(connection, message);
}

ErrorCode __wrap_ipc_connection_flush(IpcConnectionHandle connection, bool *out_has_pending)
{
    if (fault_plan.flush_error != CORE_OK) {
        ErrorCode error = fault_plan.flush_error;

        fault_plan.flush_error = CORE_OK;
        return error;
    }
    return __real_ipc_connection_flush(connection, out_has_pending);
}
