#include <sys/process.h>

#include <sys/memory.h>

#include <punos/syscall/syscall.h>

/*
 * A handle is a small structure in its own libmem block. The block is also
 * what the handle pointer points to, so releasing the handle frees the block.
 */
struct ProcessHandle {
    MemoryBlock block;
    pid_t pid;
};

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static const char *string_data(const String *string)
{
    const char *data = NULL;

    if (string_c_str(string, &data) != CORE_OK) {
        return NULL;
    }
    return data;
}

/*
 * Writes the C strings of count Strings into slots and a NULL after them.
 * Returns false if a String is invalid.
 */
static bool fill_vector(char **slots, const String *strings, usize count)
{
    usize index;

    for (index = 0; index < count; ++index) {
        const char *data = string_data(&strings[index]);

        if (data == NULL) {
            return false;
        }
        slots[index] = (char *)data;
    }
    slots[count] = NULL;
    return true;
}

/*
 * An environment entry is a non-empty name, "=", then any value: the first
 * "=" must exist and must not be the first byte. "NAME=" and "NAME=a=b" are
 * valid; "", "NAME" and "=value" are not.
 */
static bool is_environment_entry(const char *entry)
{
    usize index = 0;

    while (entry[index] != '\0' && entry[index] != '=') {
        ++index;
    }
    return entry[index] == '=' && index != 0;
}

/* Same as fill_vector(), and every entry must also be in NAME=value form. */
static bool fill_environment(char **slots, const String *strings, usize count)
{
    usize index;

    if (!fill_vector(slots, strings, count)) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        if (!is_environment_entry(slots[index])) {
            return false;
        }
    }
    return true;
}

/*
 * Builds argv (executable, arguments..., NULL) followed by envp
 * (environment..., NULL) in one block. The pointers refer to the caller's
 * strings, which stay valid until process_start() returns; the child's
 * execve() copies them into the new program.
 */
static ErrorCode build_vectors(const ProcessLaunch *launch, MemoryBlock *out_block,
                               char ***out_argv, char ***out_envp)
{
    const char *executable = string_data(launch->executable);
    usize argv_slots;
    usize envp_slots;
    usize slots;
    usize size;
    ErrorCode result;
    char **argv;
    char **envp;

    if (executable == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    /* argv: executable + arguments + NULL; envp: environment + NULL. */
    if (core_size_add(launch->argument_count, 2, &argv_slots) != CORE_OK ||
        core_size_add(launch->environment_count, 1, &envp_slots) != CORE_OK ||
        core_size_add(argv_slots, envp_slots, &slots) != CORE_OK ||
        core_size_multiply(slots, sizeof(char *), &size) != CORE_OK) {
        return CORE_OVERFLOW;
    }

    result = memory_allocate(size, out_block);
    if (result != CORE_OK) {
        return result;
    }
    argv = (char **)out_block->data;
    envp = argv + argv_slots;

    argv[0] = (char *)executable;
    if (!fill_vector(argv + 1, launch->arguments, launch->argument_count) ||
        !fill_environment(envp, launch->environment, launch->environment_count)) {
        (void)memory_free(out_block);
        return CORE_INVALID_ARGUMENT;
    }
    *out_argv = argv;
    *out_envp = envp;
    return CORE_OK;
}

static ProcessExitStatus decode_status(i32 status)
{
    ProcessExitStatus result;
    i32 signal = status & 0x7f;

    /* Linux wait status: low 7 bits are the terminating signal, 0 if exited. */
    if (signal == 0) {
        result.kind = PROCESS_TERMINATION_KIND_EXITED;
        result.value = (status >> 8) & 0xff;
    } else {
        result.kind = PROCESS_TERMINATION_KIND_SIGNALED;
        result.value = signal;
    }
    return result;
}

static void release(ProcessHandle *process)
{
    MemoryBlock block = (*process)->block;

    (void)memory_free(&block);
    *process = NULL;
}

/* ------------------------------------------------------------------------ */
/* Operations                                                               */
/* ------------------------------------------------------------------------ */

ErrorCode process_current_id(ProcessId *out_pid)
{
    if (out_pid == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_pid = getpid();
    return CORE_OK;
}

ErrorCode process_start(const ProcessLaunch *launch, ProcessHandle *out_process)
{
    MemoryBlock handle_block;
    MemoryBlock vectors;
    ProcessHandle handle;
    ErrorCode result;
    char **argv;
    char **envp;
    pid_t child;
    i32 error;

    if (launch == NULL || out_process == NULL ||
        (launch->arguments == NULL && launch->argument_count != 0) ||
        (launch->environment == NULL && launch->environment_count != 0)) {
        return CORE_INVALID_ARGUMENT;
    }

    /* Everything that can fail is done before fork(). */
    result = build_vectors(launch, &vectors, &argv, &envp);
    if (result != CORE_OK) {
        return result;
    }
    result = memory_allocate(sizeof(struct ProcessHandle), &handle_block);
    if (result != CORE_OK) {
        (void)memory_free(&vectors);
        return result;
    }

    child = fork();
    if (child == 0) {
        /* Child: only execve(), and exit() if it fails (contract 5). */
        (void)execve(argv[0], argv, envp);
        exit(PROCESS_EXIT_EXEC_FAILED);
    }
    error = errno;
    (void)memory_free(&vectors);

    if (child < 0) {
        (void)memory_free(&handle_block);
        return core_error_from_errno(error);
    }

    handle = (ProcessHandle)handle_block.data;
    handle->block = handle_block;
    handle->pid = child;
    *out_process = handle;
    return CORE_OK;
}

ErrorCode process_poll(ProcessHandle *process, bool *out_finished,
                       ProcessExitStatus *out_status)
{
    pid_t result;
    i32 status = 0;

    if (process == NULL || *process == NULL || out_finished == NULL ||
        out_status == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    result = waitpid((*process)->pid, &status, WNOHANG);
    if (result < 0) {
        return core_error_from_errno(errno);
    }
    if (result == 0) {
        *out_finished = false;
        return CORE_OK;
    }

    *out_status = decode_status(status);
    *out_finished = true;
    release(process);
    return CORE_OK;
}

ErrorCode process_wait(ProcessHandle *process, ProcessExitStatus *out_status)
{
    i32 status = 0;

    if (process == NULL || *process == NULL || out_status == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    /* EINTR becomes CORE_INTERRUPTED; the handle stays valid for a retry. */
    if (waitpid((*process)->pid, &status, 0) < 0) {
        return core_error_from_errno(errno);
    }

    *out_status = decode_status(status);
    release(process);
    return CORE_OK;
}

ErrorCode process_terminate(ProcessHandle process)
{
    if (process == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (kill(process->pid, SIGTERM) != 0) {
        return core_error_from_errno(errno);
    }
    return CORE_OK;
}
