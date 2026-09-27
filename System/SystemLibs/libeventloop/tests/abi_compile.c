/*
 * Compile-time checks of the libeventloop ABI against
 * DevDoc/SystemLibs/libeventloop.md. This file only has to compile; it
 * produces no runnable program.
 */
#include <sys/eventloop.h>

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)

_Static_assert(SAME_TYPE((EventSourceId)0, u64), "EventSourceId must be u64");
_Static_assert(SAME_TYPE((EventMask)0, u32), "EventMask must be u32");

/* EVENT_* bits equal the corresponding EPOLL* bits (implementation decision,
 * see the library's README): EPOLLIN=1, EPOLLOUT=4, EPOLLERR=8, EPOLLHUP=16. */
_Static_assert(EVENT_READABLE == 1, "EVENT_READABLE");
_Static_assert(EVENT_WRITABLE == 4, "EVENT_WRITABLE");
_Static_assert(EVENT_ERROR == 8, "EVENT_ERROR");
_Static_assert(EVENT_HANGUP == 16, "EVENT_HANGUP");

_Static_assert(SAME_TYPE(EVENTLOOP_WAIT_FOREVER, DurationMs), "EVENTLOOP_WAIT_FOREVER type");
_Static_assert(EVENTLOOP_WAIT_FOREVER == (DurationMs)0xffffffffffffffffULL,
               "EVENTLOOP_WAIT_FOREVER value");

/*
 * EventSourceKind values are not pinned by the specification; this library
 * numbers them in the order the specification's table lists them.
 */
_Static_assert(EVENT_SOURCE_KIND_FILE_DESCRIPTOR == 0, "EVENT_SOURCE_KIND_FILE_DESCRIPTOR");
_Static_assert(EVENT_SOURCE_KIND_TIMER == 1, "EVENT_SOURCE_KIND_TIMER");
_Static_assert(EVENT_SOURCE_KIND_WAKE == 2, "EVENT_SOURCE_KIND_WAKE");

/* Event layout: source_id, kind, mask. */
_Static_assert(__builtin_offsetof(Event, source_id) == 0, "Event.source_id offset");
_Static_assert(__builtin_offsetof(Event, kind) == 8, "Event.kind offset");
_Static_assert(__builtin_offsetof(Event, mask) == 12, "Event.mask offset");
_Static_assert(SAME_TYPE(((Event *)0)->source_id, EventSourceId), "Event.source_id type");
_Static_assert(SAME_TYPE(((Event *)0)->mask, EventMask), "Event.mask type");

_Static_assert(sizeof(EventLoopHandle) == sizeof(void *), "EventLoopHandle must be a pointer");
