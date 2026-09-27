/*
 * Runtime test of the libeventloop operations against
 * DevDoc/SystemLibs/libeventloop.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 * Test fixtures use eventfd() directly (through the Syscall Wrappers the
 * test is free to call, unlike the library itself) to stand in for a
 * caller-owned FileDescriptor source, since PUNos has no pipe() wrapper.
 * libthread is a test-only dependency, used to call eventloop_wake() from
 * a real second thread (specification contract 9).
 */
#define CHECK_TEST_NAME "libeventloop"
#include "../../testing/check.h"

#include <sys/eventloop.h>
#include <sys/thread.h>

#define GENEROUS_TIMEOUT_MS ((DurationMs)5000) /* QEMU without KVM is slow */

static i32 make_test_fd(void)
{
    return eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
}

static void bump(i32 fd)
{
    u64 value = 1;

    (void)write(fd, &value, sizeof(value));
}

/* ------------------------------------------------------------------------ */
/* Create / close                                                           */
/* ------------------------------------------------------------------------ */

static void test_create_close(void)
{
    EventLoopHandle loop = NULL;
    EventLoopHandle null_loop = NULL;

    EXPECT(1, eventloop_create(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(2, eventloop_create(&loop) == CORE_OK && loop != NULL);
    EXPECT(3, eventloop_close(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(4, eventloop_close(&null_loop) == CORE_INVALID_ARGUMENT);
    EXPECT(5, eventloop_close(&loop) == CORE_OK);
    EXPECT(6, loop == NULL);
}

/* ------------------------------------------------------------------------ */
/* add_fd / update_fd / remove_source                                      */
/* ------------------------------------------------------------------------ */

static void test_add_update_remove(void)
{
    EventLoopHandle loop = NULL;
    i32 fd;
    EventSourceId source = 0;
    Event events[4];
    usize count = 0;

    EXPECT(10, eventloop_create(&loop) == CORE_OK);
    fd = make_test_fd();
    EXPECT(11, fd >= 0);

    EXPECT(12, eventloop_add_fd(NULL, fd, EVENT_READABLE, &source) ==
                   CORE_INVALID_ARGUMENT);
    EXPECT(13, eventloop_add_fd(loop, fd, EVENT_READABLE, NULL) ==
                   CORE_INVALID_ARGUMENT);
    EXPECT(14, eventloop_add_fd(loop, fd, (EventMask)0x80000000u, &source) ==
                   CORE_INVALID_ARGUMENT);

    /* Register interested only in EVENT_WRITABLE: an eventfd is always
     * writable, so it must be reported ready without ever writing to it. */
    EXPECT(15, eventloop_add_fd(loop, fd, EVENT_WRITABLE, &source) == CORE_OK &&
                   source != 0);
    EXPECT(16, eventloop_wait(loop, events, 4, GENEROUS_TIMEOUT_MS, &count) == CORE_OK);
    EXPECT(17, count == 1 && events[0].source_id == source &&
                   events[0].kind == EVENT_SOURCE_KIND_FILE_DESCRIPTOR &&
                   (events[0].mask & EVENT_WRITABLE) != 0);

    /* Switch interest to EVENT_READABLE only: nothing was written, so a
     * short wait now times out (count == 0). */
    EXPECT(18, eventloop_update_fd(loop, source, EVENT_READABLE) == CORE_OK);
    EXPECT(19, eventloop_wait(loop, events, 4, 50, &count) == CORE_OK && count == 0);

    bump(fd);
    EXPECT(20, eventloop_wait(loop, events, 4, GENEROUS_TIMEOUT_MS, &count) == CORE_OK);
    EXPECT(21, count == 1 && events[0].source_id == source &&
                   (events[0].mask & EVENT_READABLE) != 0);
    /* The library never reads a FileDescriptor source's data (contract 10):
     * the eventfd counter set by bump() is still there for us to drain. */
    {
        u64 drained = 0;

        EXPECT(22, read(fd, &drained, sizeof(drained)) == (ssize_t)sizeof(drained) &&
                       drained == 1);
    }

    EXPECT(23, eventloop_update_fd(NULL, source, EVENT_READABLE) == CORE_INVALID_ARGUMENT);
    EXPECT(24, eventloop_update_fd(loop, 0xdeadbeef, EVENT_READABLE) == CORE_NOT_FOUND);
    EXPECT(25, eventloop_update_fd(loop, source, (EventMask)0x80000000u) ==
                   CORE_INVALID_ARGUMENT);

    EXPECT(26, eventloop_remove_source(NULL, source) == CORE_INVALID_ARGUMENT);
    EXPECT(27, eventloop_remove_source(loop, source) == CORE_OK);
    EXPECT(28, eventloop_remove_source(loop, source) == CORE_NOT_FOUND);

    /* The fd is still ours (the library never closed it): closing it now
     * must succeed exactly once. */
    EXPECT(29, close(fd) == 0);
    EXPECT(30, eventloop_close(&loop) == CORE_OK);
}

/* ------------------------------------------------------------------------ */
/* events_capacity respected, several sources at once                      */
/* ------------------------------------------------------------------------ */

static void test_capacity(void)
{
    EventLoopHandle loop = NULL;
    i32 fd_a;
    i32 fd_b;
    EventSourceId source_a = 0;
    EventSourceId source_b = 0;
    Event events[4];
    usize count = 0;

    EXPECT(35, eventloop_create(&loop) == CORE_OK);
    fd_a = make_test_fd();
    fd_b = make_test_fd();
    EXPECT(36, fd_a >= 0 && fd_b >= 0);
    EXPECT(37, eventloop_add_fd(loop, fd_a, EVENT_WRITABLE, &source_a) == CORE_OK);
    EXPECT(38, eventloop_add_fd(loop, fd_b, EVENT_WRITABLE, &source_b) == CORE_OK);
    EXPECT(39, source_a != source_b);

    /* Both are always-writable: with room for only one, exactly one comes back. */
    EXPECT(40, eventloop_wait(loop, events, 1, GENEROUS_TIMEOUT_MS, &count) == CORE_OK);
    EXPECT(41, count == 1);

    EXPECT(42, eventloop_wait(loop, NULL, 0, 0, &count) == CORE_OK && count == 0);
    EXPECT(43, eventloop_wait(loop, events, 1, 0, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(44, eventloop_wait(loop, NULL, 1, 0, &count) == CORE_INVALID_ARGUMENT);
    EXPECT(45, eventloop_wait(loop, events, 1, (DurationMs)2147483648ULL, &count) ==
                   CORE_INVALID_ARGUMENT);

    EXPECT(46, eventloop_remove_source(loop, source_a) == CORE_OK);
    EXPECT(47, eventloop_remove_source(loop, source_b) == CORE_OK);
    EXPECT(48, close(fd_a) == 0 && close(fd_b) == 0);
    EXPECT(49, eventloop_close(&loop) == CORE_OK);
}

/* ------------------------------------------------------------------------ */
/* Timers                                                                   */
/* ------------------------------------------------------------------------ */

static void test_timer_one_shot(void)
{
    EventLoopHandle loop = NULL;
    EventSourceId source = 0;
    Event events[4];
    usize count = 0;

    EXPECT(55, eventloop_create(&loop) == CORE_OK);
    EXPECT(56, eventloop_create_timer(NULL, 10, 0, &source) == CORE_INVALID_ARGUMENT);
    EXPECT(57, eventloop_create_timer(loop, 10, 0, NULL) == CORE_INVALID_ARGUMENT);
    /* delay_ms == 0 must still fire, not disarm the timer. */
    EXPECT(58, eventloop_create_timer(loop, 0, 0, &source) == CORE_OK);
    EXPECT(59, eventloop_wait(loop, events, 4, GENEROUS_TIMEOUT_MS, &count) == CORE_OK);
    EXPECT(60, count == 1 && events[0].source_id == source &&
                   events[0].kind == EVENT_SOURCE_KIND_TIMER);
    /* One-shot: drained already, and it never fires again. */
    EXPECT(61, eventloop_wait(loop, events, 4, 100, &count) == CORE_OK && count == 0);
    EXPECT(62, eventloop_remove_source(loop, source) == CORE_OK);
    EXPECT(63, eventloop_close(&loop) == CORE_OK);
}

static void test_timer_periodic(void)
{
    EventLoopHandle loop = NULL;
    EventSourceId source = 0;
    Event events[4];
    usize count = 0;
    i32 fired = 0;
    i32 attempt;

    EXPECT(68, eventloop_create(&loop) == CORE_OK);
    EXPECT(69, eventloop_create_timer(loop, 20, 20, &source) == CORE_OK);

    /* Collect at least 3 firings; each wait drains one, so the loop cannot
     * spin on a stuck-readable fd. Generous bounds: QEMU without KVM is slow. */
    for (attempt = 0; attempt < 50 && fired < 3; ++attempt) {
        if (eventloop_wait(loop, events, 4, GENEROUS_TIMEOUT_MS, &count) != CORE_OK) {
            break;
        }
        fired += (i32)count;
    }
    EXPECT(70, fired >= 3);

    EXPECT(71, eventloop_remove_source(loop, source) == CORE_OK);
    EXPECT(72, eventloop_close(&loop) == CORE_OK);
}

/* ------------------------------------------------------------------------ */
/* Wake from another thread                                                 */
/* ------------------------------------------------------------------------ */

static i32 waker_thread(void *argument)
{
    EventLoopHandle loop = argument;
    DurationMs remaining;

    (void)time_sleep(50, &remaining);
    if (eventloop_wake(loop) != CORE_OK) {
        return 1;
    }
    return 0;
}

static void test_wake(void)
{
    EventLoopHandle loop = NULL;
    ThreadHandle thread = NULL;
    ThreadExitStatus status = -1;
    Event events[4];
    usize count = 0;

    EXPECT(80, eventloop_create(&loop) == CORE_OK);
    EXPECT(81, eventloop_wake(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(82, thread_create(waker_thread, loop, &thread) == CORE_OK);

    /* Blocks until the other thread calls eventloop_wake(); a bug here
     * would hang the test forever instead of just failing a check. */
    EXPECT(83, eventloop_wait(loop, events, 4, EVENTLOOP_WAIT_FOREVER, &count) == CORE_OK);
    EXPECT(84, count == 1 && events[0].kind == EVENT_SOURCE_KIND_WAKE);

    EXPECT(85, thread_join(&thread, &status) == CORE_OK && status == 0);
    /* The wake source is internal and never leaked to the caller as an id
     * from eventloop_create(); it cannot be removed through this API. */
    EXPECT(86, eventloop_close(&loop) == CORE_OK);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    test_create_close();
    test_add_update_remove();
    test_capacity();
    test_timer_one_shot();
    test_timer_periodic();
    test_wake();

    check_passed();
    return 0;
}
