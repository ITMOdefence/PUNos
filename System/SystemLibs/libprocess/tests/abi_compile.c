/*
 * Compile-time checks of the libprocess ABI against DevDoc/SystemLibs/libprocess.md.
 * This file only has to compile; it produces no runnable program.
 */
#include <sys/process.h>

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)

_Static_assert(SAME_TYPE((ProcessId)0, i32), "ProcessId must be i32");
_Static_assert(SAME_TYPE((ProcessId)0, pid_t), "ProcessId must match pid_t");
_Static_assert(sizeof(ProcessHandle) == sizeof(void *), "ProcessHandle is a pointer");

_Static_assert(__builtin_offsetof(ProcessLaunch, executable) == 0, "executable offset");
_Static_assert(__builtin_offsetof(ProcessLaunch, arguments) == 8, "arguments offset");
_Static_assert(__builtin_offsetof(ProcessLaunch, argument_count) == 16, "argument_count offset");
_Static_assert(__builtin_offsetof(ProcessLaunch, environment) == 24, "environment offset");
_Static_assert(__builtin_offsetof(ProcessLaunch, environment_count) == 32, "environment_count offset");
_Static_assert(SAME_TYPE(((ProcessLaunch *)0)->executable, const String *), "executable type");

_Static_assert(__builtin_offsetof(ProcessExitStatus, kind) == 0, "kind offset");
_Static_assert(__builtin_offsetof(ProcessExitStatus, value) == 4, "value offset");
_Static_assert(SAME_TYPE(((ProcessExitStatus *)0)->value, i32), "value type");

_Static_assert(PROCESS_TERMINATION_KIND_EXITED == 0, "EXITED");
_Static_assert(PROCESS_TERMINATION_KIND_SIGNALED == 1, "SIGNALED");

_Static_assert(PROCESS_EXIT_SUCCESS == 0, "PROCESS_EXIT_SUCCESS");
_Static_assert(PROCESS_EXIT_FAILURE == 1, "PROCESS_EXIT_FAILURE");
_Static_assert(PROCESS_EXIT_INVALID_ARGUMENT == 2, "PROCESS_EXIT_INVALID_ARGUMENT");
_Static_assert(PROCESS_EXIT_INITIALIZATION_FAILED == 3, "PROCESS_EXIT_INITIALIZATION_FAILED");
_Static_assert(PROCESS_EXIT_EXEC_FAILED == 127, "PROCESS_EXIT_EXEC_FAILED");
