/*
 * A getsockopt() fake for the ipclib runtime test.
 *
 * Issue 8 is about what ipc_connection_finish_connect() does with SO_ERROR,
 * but the state it has to recognise cannot be produced on demand: Linux
 * completes an AF_UNIX connect() straight away, so a non-blocking client never
 * observes EINPROGRESS, let alone EINPROGRESS turning into a failure. Filling
 * the listen backlog does not change that - the probe behind this file showed
 * two connects succeeding against a backlog of one.
 *
 * So the one call that reports the outcome is replaced. Everything else,
 * including the real connect() and the real sockets, is untouched: ipclib
 * calls getsockopt() only to read SO_ERROR, and nothing else in the link needs
 * it, so overriding it costs the rest of the suite nothing.
 *
 * The fake is one-shot on purpose: the kernel clears a pending error when it is
 * read, and that is exactly the detail the caching fix depends on. A fake that
 * kept returning the error would hide the bug.
 */

#include <sys/core.h>

#include <punos/syscall/constants.h>
#include <punos/syscall/syscall.h>

/* The error the next SO_ERROR read reports; 0 means "connected". Cleared once
 * read, the way the kernel clears it. */
static i32 pending_so_error;
static usize send_limit;
static bool send_would_block;
static bool listen_failure;

ssize_t __real_send(i32 fd, const void *buffer, usize size, i32 flags);
i32 __real_listen(i32 fd, i32 backlog);

void fake_socket_fail_listen(void) { listen_failure = true; }

i32 __wrap_listen(i32 fd, i32 backlog)
{
    if (listen_failure) {
        listen_failure = false;
        errno = EIO;
        return -1;
    }
    return __real_listen(fd, backlog);
}

void fake_socket_send_limit(usize limit)
{
    send_limit = limit;
}

void fake_socket_block_send(bool block)
{
    send_would_block = block;
}

/* The real peer receives exactly the bytes reported as sent. This makes a
 * short send deterministic while still checking the reassembled stream. */
ssize_t __wrap_send(i32 fd, const void *buffer, usize size, i32 flags)
{
    if (send_would_block) {
        errno = EAGAIN;
        return -1;
    }
    if (send_limit != 0 && size > send_limit) {
        size = send_limit;
    }
    return __real_send(fd, buffer, size, flags);
}

void fake_socket_arm(i32 error_to_report)
{
    pending_so_error = error_to_report;
}

void fake_socket_reset(void)
{
    pending_so_error = 0;
    send_limit = 0;
    send_would_block = false;
    listen_failure = false;
}

/* True while an error is still waiting to be read. */
bool fake_socket_error_pending(void)
{
    return pending_so_error != 0;
}

i32 getsockopt(i32 fd, i32 level, i32 option, void *value, socklen_t *length)
{
    if (value == NULL || length == NULL) {
        errno = EINVAL;
        return -1;
    }

    (void)fd;

    if (level == SOL_SOCKET && option == SO_ERROR) {
        i32 reported = pending_so_error;

        /* Reading SO_ERROR consumes the pending error. */
        pending_so_error = 0;
        if (length != NULL && *length >= (socklen_t)sizeof(i32)) {
            *(i32 *)value = reported;
            *length = (socklen_t)sizeof(i32);
        }
        return 0;
    }

    /* Nothing else in these tests asks for a socket option. */
    errno = EINVAL;
    return -1;
}
