#include "fakes.h"

i32 fake_allocate_after = -1;
ErrorCode fake_start_error, fake_terminate_error;
bool fake_finished, fake_wait_interrupt;
ProcessExitStatus fake_exit_status;
usize fake_starts, fake_terminates, fake_polls, fake_waits, fake_releases;
bool fake_bad_ownership;
ProcessLaunch fake_launch;

struct ProcessHandle { bool active; bool terminated; };
static struct ProcessHandle handles[128];

ErrorCode __real_memory_allocate(usize size, MemoryBlock *out_block);
ErrorCode __wrap_memory_allocate(usize size, MemoryBlock *out_block)
{
    if (fake_allocate_after == 0) return CORE_OUT_OF_MEMORY;
    if (fake_allocate_after > 0) --fake_allocate_after;
    return __real_memory_allocate(size, out_block);
}

void fake_reset(void)
{
    fake_allocate_after = -1;
    fake_start_error = fake_terminate_error = CORE_OK;
    fake_finished = fake_wait_interrupt = false;
    fake_exit_status = (ProcessExitStatus){ PROCESS_TERMINATION_KIND_EXITED, 37 };
    fake_starts = fake_terminates = fake_polls = fake_waits = fake_releases = 0;
    fake_bad_ownership = false;
    for (usize i = 0; i < 128; ++i) handles[i] = (struct ProcessHandle){ 0 };
}

ErrorCode __wrap_process_start(const ProcessLaunch *launch, ProcessHandle *out)
{
    fake_launch = *launch;
    ++fake_starts;
    if (fake_start_error != CORE_OK) return fake_start_error;
    for (usize i = 0; i < 128; ++i) {
        if (!handles[i].active) {
            handles[i] = (struct ProcessHandle){ true, false };
            *out = &handles[i];
            return CORE_OK;
        }
    }
    return CORE_OUT_OF_MEMORY;
}

ErrorCode __wrap_process_terminate(ProcessHandle process)
{
    ++fake_terminates;
    if (process == NULL || !process->active) fake_bad_ownership = true;
    if (fake_terminate_error != CORE_OK) return fake_terminate_error;
    process->terminated = true;
    return CORE_OK;
}

static void release(ProcessHandle *process)
{
    if (*process == NULL || !(*process)->active) fake_bad_ownership = true;
    (*process)->active = false;
    *process = NULL;
    ++fake_releases;
}

ErrorCode __wrap_process_poll(ProcessHandle *process, bool *finished,
                               ProcessExitStatus *status)
{
    ++fake_polls;
    if (*process == NULL || !(*process)->active) fake_bad_ownership = true;
    *finished = fake_finished;
    if (fake_finished) { *status = fake_exit_status; release(process); }
    return CORE_OK;
}

ErrorCode __wrap_process_wait(ProcessHandle *process, ProcessExitStatus *status)
{
    ++fake_waits;
    for (usize i = 0; i < 128; ++i)
        if (handles[i].active && !handles[i].terminated) fake_bad_ownership = true;
    if (fake_wait_interrupt) { fake_wait_interrupt = false; return CORE_INTERRUPTED; }
    *status = fake_exit_status;
    release(process);
    return CORE_OK;
}
