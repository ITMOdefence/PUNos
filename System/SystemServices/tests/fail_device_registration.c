/* Linked only into the camera integration-test executable. */
#include <sys/eventloop.h>

extern i32 fake_event_fd;

ErrorCode __real_eventloop_add_fd(EventLoopHandle, i32, EventMask, EventSourceId *);
ErrorCode __wrap_eventloop_add_fd(EventLoopHandle loop, i32 fd, EventMask mask,
                                  EventSourceId *out_source)
{
    if (fd == fake_event_fd) return CORE_IO_ERROR;
    return __real_eventloop_add_fd(loop, fd, mask, out_source);
}
