#ifndef PUNOS_SYS_EVENTLOOP_H
#define PUNOS_SYS_EVENTLOOP_H

/*
 * libeventloop (sys/eventloop) - waiting for events from file descriptors,
 * timers and other threads in one loop.
 *
 * Specification: DevDoc/SystemLibs/libeventloop.md. Public header:
 * <sys/eventloop.h>.
 *
 * Built on epoll_create1(), epoll_ctl(), epoll_wait(), eventfd(),
 * timerfd_create(), timerfd_settime(), read(), write() and close(). The
 * event loop owns the epoll instance, the internal eventfd used by
 * eventloop_wake() and every timerfd created by eventloop_create_timer();
 * a file descriptor added with eventloop_add_fd() stays owned by its
 * caller and is never closed by this library. All operations return
 * ErrorCode; see <sys/core.h> for the conventions.
 */

#include <sys/core.h>
#include <sys/time.h>

/* Opaque handle of an event loop. Closed exactly once with eventloop_close(). */
typedef struct EventLoopHandle *EventLoopHandle;

/* Nonzero while the source stays registered; unique only among currently
 * registered sources of one EventLoopHandle. */
typedef u64 EventSourceId;

/* Bit mask of EVENT_* flags. The values equal the corresponding EPOLL* bits
 * (see eventloop.h's implementation notes in the library's README), so
 * translating to and from the kernel's own event mask is a plain bitwise AND. */
typedef u32 EventMask;

#define EVENT_READABLE ((EventMask)1)
#define EVENT_WRITABLE ((EventMask)4)
#define EVENT_ERROR ((EventMask)8)
#define EVENT_HANGUP ((EventMask)16)

#define EVENTLOOP_WAIT_FOREVER ((DurationMs)0xffffffffffffffffULL)

/*
 * The specification lists these values by name only, without pinning
 * numbers. This library numbers them in the order the specification's
 * table lists them, starting at 0; the numbers are then part of the ABI.
 */
typedef enum EventSourceKind {
    EVENT_SOURCE_KIND_FILE_DESCRIPTOR = 0,
    EVENT_SOURCE_KIND_TIMER = 1,
    EVENT_SOURCE_KIND_WAKE = 2
} EventSourceKind;

typedef struct Event {
    EventSourceId source_id;
    EventSourceKind kind;
    EventMask mask;
} Event;

_Static_assert(sizeof(EventSourceKind) == 4, "EventSourceKind ABI mismatch");
_Static_assert(sizeof(Event) == 16, "Event ABI mismatch");

/*
 * Creates an event loop: an epoll instance plus an internal eventfd for
 * eventloop_wake().
 *
 * Errors: CORE_INVALID_ARGUMENT if out_loop is NULL; core_error_from_errno()
 * if epoll_create1() or eventfd() fails; errors of libmem.
 */
ErrorCode eventloop_create(EventLoopHandle *out_loop);

/*
 * Closes loop: every timerfd it created, its internal eventfd, its epoll
 * instance, and sets *loop to NULL. File descriptors added with
 * eventloop_add_fd() are left open: their owner closes them.
 *
 * Errors: CORE_INVALID_ARGUMENT if loop or *loop is NULL; core_error_from_errno()
 * if a close() fails (the handle is still released).
 */
ErrorCode eventloop_close(EventLoopHandle *loop);

/*
 * Registers fd, owned by the caller, for the events in mask, and stores its
 * id in *out_source. Call only from loop's owner thread (specification
 * contract 8).
 *
 * Errors: CORE_INVALID_ARGUMENT if loop or out_source is NULL, or mask has
 * bits outside EVENT_READABLE|EVENT_WRITABLE|EVENT_ERROR|EVENT_HANGUP;
 * core_error_from_errno() if epoll_ctl() fails; errors of libmem.
 */
ErrorCode eventloop_add_fd(EventLoopHandle loop, i32 fd, EventMask mask,
                          EventSourceId *out_source);

/*
 * Changes the events source is watched for. Call only from loop's owner
 * thread.
 *
 * Errors: CORE_INVALID_ARGUMENT if loop is NULL or mask has unknown bits;
 * CORE_NOT_FOUND if source is not currently registered; core_error_from_errno()
 * if epoll_ctl() fails.
 */
ErrorCode eventloop_update_fd(EventLoopHandle loop, EventSourceId source, EventMask mask);

/*
 * Unregisters source. A Timer source's timerfd is closed; a FileDescriptor
 * source's fd is not (its owner closes it); removing the internal Wake
 * source is not possible through this API (its id is never returned by
 * eventloop_create()). Call only from loop's owner thread.
 *
 * Errors: CORE_INVALID_ARGUMENT if loop is NULL; CORE_NOT_FOUND if source is
 * not currently registered; core_error_from_errno() if epoll_ctl() or
 * close() fails.
 */
ErrorCode eventloop_remove_source(EventLoopHandle loop, EventSourceId source);

/*
 * Creates a timerfd that first fires after delay_ms and then, if
 * period_ms != 0, every period_ms afterward, and registers it. delay_ms ==
 * 0 fires as soon as possible rather than never (see the library's README
 * for why 0 needs special handling with timerfd_settime()).
 *
 * Errors: CORE_INVALID_ARGUMENT if loop or out_source is NULL;
 * core_error_from_errno() if timerfd_create() or timerfd_settime() fails;
 * errors of libmem.
 */
ErrorCode eventloop_create_timer(EventLoopHandle loop, DurationMs delay_ms, DurationMs period_ms,
                                 EventSourceId *out_source);

/*
 * Waits through epoll_wait() for up to events_capacity ready sources,
 * waiting at most timeout_ms milliseconds (EVENTLOOP_WAIT_FOREVER for no
 * limit) and stores how many were written in *out_count; 0 means the
 * timeout expired with nothing ready. A ready Timer or Wake source has its
 * internal counter drained before being reported, so it is not reported
 * again until it genuinely fires (or is woken) once more. Call only from
 * loop's owner thread.
 *
 * Errors: CORE_INVALID_ARGUMENT if loop or out_count is NULL, out_events is
 * NULL while events_capacity != 0, or a finite timeout_ms exceeds
 * 2147483647; CORE_OVERFLOW if events_capacity is too large to allocate a
 * temporary buffer for; core_error_from_errno() if epoll_wait() or an
 * internal drain read() fails; errors of libmem.
 */
ErrorCode eventloop_wait(EventLoopHandle loop, Event *out_events, usize events_capacity,
                        DurationMs timeout_ms, usize *out_count);

/*
 * Wakes a concurrent eventloop_wait() (or makes the next one return
 * immediately) by writing to the internal eventfd. Safe to call from any
 * thread, including one other than loop's owner (specification contract 9).
 *
 * Errors: CORE_INVALID_ARGUMENT if loop is NULL; core_error_from_errno() if
 * write() fails.
 */
ErrorCode eventloop_wake(EventLoopHandle loop);

#endif
