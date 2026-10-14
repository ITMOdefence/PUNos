/* Linked only into a test executable to exercise device HANGUP/ERROR dispatch. */
#include <sys/eventloop.h>
#include <punos/syscall/syscall.h>

#ifndef TRACE_PATH
#error TRACE_PATH is required
#endif
#ifndef INJECTED_EVENT_MASK
#error INJECTED_EVENT_MASK is required
#endif

extern i32 fake_event_fd;
static EventSourceId device_source;
static bool injected;

static void trace(const char *text)
{
    i32 fd = open(TRACE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;
    usize size = 0; while (text[size]) ++size;
    (void)write(fd, text, size);
    (void)close(fd);
}

ErrorCode __real_eventloop_add_fd(EventLoopHandle, i32, EventMask, EventSourceId *);
ErrorCode __wrap_eventloop_add_fd(EventLoopHandle loop, i32 fd, EventMask mask,
                                  EventSourceId *out_source)
{
    ErrorCode error = __real_eventloop_add_fd(loop, fd, mask, out_source);
    if (error == CORE_OK && fd == fake_event_fd) device_source = *out_source;
    return error;
}

ErrorCode __real_eventloop_wait(EventLoopHandle, Event *, usize, DurationMs, usize *);
ErrorCode __wrap_eventloop_wait(EventLoopHandle loop, Event *events, usize capacity,
                                DurationMs timeout, usize *count)
{
    ErrorCode error = __real_eventloop_wait(loop, events, capacity, timeout, count);
    if (error == CORE_OK && device_source && !injected) {
        for (usize i = 0; i < *count; ++i) {
            if (events[i].source_id == device_source) {
                events[i].mask = INJECTED_EVENT_MASK;
                injected = true;
                trace("device_event_injected\n");
                break;
            }
        }
    }
    return error;
}

ErrorCode __real_eventloop_remove_source(EventLoopHandle, EventSourceId);
ErrorCode __wrap_eventloop_remove_source(EventLoopHandle loop, EventSourceId source)
{
    if (source == device_source) trace("device_source_removed\n");
    return __real_eventloop_remove_source(loop, source);
}
