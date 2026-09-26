#ifndef PUNOS_SYS_PROCESS_H
#define PUNOS_SYS_PROCESS_H

/*
 * libprocess (sys/process) - starting, waiting for and stopping native child
 * processes: the VM and System Services.
 *
 * Specification: DevDoc/SystemLibs/libprocess.md
 *
 * This is a low-level API for initserv and System Services. It does not check
 * whether an executable is allowed to run: that policy belongs to the owner of
 * the component catalogue (initserv, appserv) and is checked before calling
 * process_start().
 *
 * The only syscall wrappers libprocess calls itself are getpid(), fork(),
 * execve(), waitpid(), exit() and kill(). Memory comes from libmem.
 * All operations return ErrorCode; see <sys/core.h> for the conventions.
 */

#include <sys/core.h>
#include <sys/string.h>

typedef i32 ProcessId;

/*
 * Opaque handle of a started child process. It is released by process_wait()
 * or by process_poll() once the child has finished, which also set it to NULL.
 * Every started process must be waited for, or it stays a zombie.
 */
typedef struct ProcessHandle *ProcessHandle;

/*
 * What to start. Nothing here is owned by the library: the strings only need
 * to stay valid during process_start().
 *
 * The child's argv is executable, then arguments[0 .. argument_count). Its
 * environment is exactly environment[0 .. environment_count), each entry in
 * "NAME=value" form. An array may be NULL when its count is 0.
 */
typedef struct ProcessLaunch {
    const String *executable;
    const String *arguments;
    usize argument_count;
    const String *environment;
    usize environment_count;
} ProcessLaunch;

typedef enum ProcessTerminationKind {
    PROCESS_TERMINATION_KIND_EXITED = 0,   /* value is the exit code, 0..255 */
    PROCESS_TERMINATION_KIND_SIGNALED = 1  /* value is the signal number */
} ProcessTerminationKind;

typedef struct ProcessExitStatus {
    ProcessTerminationKind kind;
    i32 value;
} ProcessExitStatus;

/* Exit codes shared by PUNos components. */
#define PROCESS_EXIT_SUCCESS 0
#define PROCESS_EXIT_FAILURE 1
#define PROCESS_EXIT_INVALID_ARGUMENT 2
#define PROCESS_EXIT_INITIALIZATION_FAILED 3
/*
 * Exit code of a child whose execve() failed (no such file, not executable,
 * ...). The parent cannot see the execve() error itself: process_start() has
 * already returned CORE_OK. 127 is the usual Unix code for this case.
 */
#define PROCESS_EXIT_EXEC_FAILED 127

_Static_assert(sizeof(ProcessLaunch) == 40, "ProcessLaunch ABI mismatch");
_Static_assert(sizeof(ProcessExitStatus) == 8, "ProcessExitStatus ABI mismatch");

/*
 * Stores the id of the calling process in *out_pid.
 *
 * Errors: CORE_INVALID_ARGUMENT if out_pid is NULL.
 */
ErrorCode process_current_id(ProcessId *out_pid);

/*
 * Starts launch->executable as a child process and stores its handle in
 * *out_process.
 *
 * All memory the child needs is prepared before fork(); after fork() the
 * child only calls execve(), and exit(PROCESS_EXIT_EXEC_FAILED) if execve()
 * fails. The child inherits the caller's signal mask and ignored signals, as
 * with any execve().
 *
 * Errors: CORE_INVALID_ARGUMENT if launch or out_process is NULL, a string is
 * not a valid String, or an array is NULL with a non-zero count;
 * CORE_OVERFLOW if the counts are too large; errors of libmem;
 * core_error_from_errno() of fork().
 */
ErrorCode process_start(const ProcessLaunch *launch, ProcessHandle *out_process);

/*
 * Checks without blocking whether the child has finished.
 *
 * If it has: stores true in *out_finished and its status in *out_status,
 * releases the handle and sets *process to NULL. Otherwise stores false and
 * leaves *out_status unspecified.
 *
 * Errors: CORE_INVALID_ARGUMENT if an argument or *process is NULL;
 * core_error_from_errno() of waitpid() (CORE_NOT_FOUND if the process is not
 * a child of the caller). On error the handle is kept.
 */
ErrorCode process_poll(ProcessHandle *process, bool *out_finished,
                       ProcessExitStatus *out_status);

/*
 * Waits until the child has finished, stores its status in *out_status,
 * releases the handle and sets *process to NULL.
 *
 * Errors: CORE_INVALID_ARGUMENT if an argument or *process is NULL;
 * CORE_INTERRUPTED if a signal handler interrupted the wait (the handle is
 * kept, call again to continue waiting); other codes from
 * core_error_from_errno() of waitpid(). On error the handle is kept.
 */
ErrorCode process_wait(ProcessHandle *process, ProcessExitStatus *out_status);

/*
 * Sends SIGTERM to the child. Does not wait for it to finish: the handle stays
 * valid and must still be passed to process_wait() or process_poll().
 *
 * Errors: CORE_INVALID_ARGUMENT if process is NULL; core_error_from_errno()
 * of kill().
 */
ErrorCode process_terminate(ProcessHandle process);

#endif
