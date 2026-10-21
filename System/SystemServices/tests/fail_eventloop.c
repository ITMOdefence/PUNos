#include <sys/eventloop.h>
ErrorCode __wrap_eventloop_create(EventLoopHandle *out)
{
    (void)out;
    return CORE_IO_ERROR;
}
