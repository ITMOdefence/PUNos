#include <sys/eventloop.h>

#include <sys/memory.h>

#include <punos/syscall/syscall.h>

#define EVENT_KNOWN_MASK (EVENT_READABLE | EVENT_WRITABLE | EVENT_ERROR | EVENT_HANGUP)
#define EVENTLOOP_MAX_TIMEOUT_MS ((DurationMs)0x7fffffff)
#define NANOSECONDS_PER_MILLISECOND 1000000
#define INITIAL_SOURCE_CAPACITY ((usize)8)

/*
 * One registered source. id == 0 marks a free slot (EventSourceId is
 * specified to be nonzero while registered, so 0 can never collide with a
 * real id). The table is a flat array, grown through libmem as needed:
 * event loops are expected to hold at most a handful of sources, so a
 * linear scan is simpler than a hash table and fast enough.
 */
typedef struct EventSourceRecord {
    EventSourceId id;
    i32 fd;
    EventSourceKind kind;
} EventSourceRecord;

struct EventLoopHandle {
    MemoryBlock handle_block;
    i32 epoll_fd;
    i32 wake_fd;
    MemoryBlock sources_block;
    usize sources_capacity;
    EventSourceId next_id;
    /* Reused across eventloop_wait() calls and only ever grown, so a busy
     * event loop is not forced to mmap()/munmap() (libmem contract 10) on
     * every single wait. */
    MemoryBlock scratch_block;
    usize scratch_capacity;
};

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static ErrorCode last_error(void)
{
    return core_error_from_errno(errno);
}

static EventSourceRecord *records_of(struct EventLoopHandle *loop)
{
    return (EventSourceRecord *)loop->sources_block.data;
}

static EventSourceRecord *find_slot(struct EventLoopHandle *loop, EventSourceId id)
{
    EventSourceRecord *records = records_of(loop);
    usize i;

    for (i = 0; i < loop->sources_capacity; ++i) {
        if (records[i].id == id) {
            return &records[i];
        }
    }
    return NULL;
}

/* Finds a free slot, growing the table (doubling it) if none is free. New
 * slots are zero-filled by memory_reallocate(), which already makes their
 * id 0 (free) without any extra initialisation. */
static ErrorCode allocate_slot(struct EventLoopHandle *loop, EventSourceRecord **out_slot)
{
    EventSourceRecord *records = records_of(loop);
    usize old_capacity = loop->sources_capacity;
    usize new_capacity;
    usize new_size;
    ErrorCode result;
    usize i;

    for (i = 0; i < old_capacity; ++i) {
        if (records[i].id == 0) {
            *out_slot = &records[i];
            return CORE_OK;
        }
    }

    new_capacity = old_capacity == 0 ? INITIAL_SOURCE_CAPACITY : old_capacity * 2;
    if (core_size_multiply(new_capacity, sizeof(EventSourceRecord), &new_size) != CORE_OK) {
        return CORE_OVERFLOW;
    }
    result = memory_reallocate(&loop->sources_block, new_size);
    if (result != CORE_OK) {
        return result;
    }
    loop->sources_capacity = new_capacity;
    *out_slot = &records_of(loop)[old_capacity];
    return CORE_OK;
}

static void milliseconds_to_timespec(DurationMs milliseconds, struct timespec *out_value)
{
    out_value->tv_sec = (time_t)(milliseconds / TIME_MILLISECONDS_PER_SECOND);
    out_value->tv_nsec =
        (i64)(milliseconds % TIME_MILLISECONDS_PER_SECOND) * NANOSECONDS_PER_MILLISECOND;
}

/* Drains the 8-byte counter of an internally-owned timerfd/eventfd so
 * epoll does not keep reporting it ready forever (both are level-triggered
 * here). Not "interpreting" the source's data (specification contract 10):
 * the value itself is discarded, this only re-arms the readiness bit. */
static ErrorCode drain(i32 fd)
{
    u64 value;

    if (read(fd, &value, sizeof(value)) < 0 && errno != EAGAIN) {
        return last_error();
    }
    return CORE_OK;
}

/* ------------------------------------------------------------------------ */
/* Create / close                                                           */
/* ------------------------------------------------------------------------ */

ErrorCode eventloop_create(EventLoopHandle *out_loop)
{
    i32 epoll_fd;
    i32 wake_fd;
    MemoryBlock handle_block;
    MemoryBlock sources_block;
    struct EventLoopHandle *loop;
    struct epoll_event event;
    EventSourceRecord *slot;
    ErrorCode result;

    if (out_loop == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        return last_error();
    }
    wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_fd < 0) {
        result = last_error();
        (void)close(epoll_fd);
        return result;
    }

    result = memory_allocate(sizeof(struct EventLoopHandle), &handle_block);
    if (result != CORE_OK) {
        goto fail_fds;
    }
    result = memory_allocate(INITIAL_SOURCE_CAPACITY * sizeof(EventSourceRecord), &sources_block);
    if (result != CORE_OK) {
        goto fail_handle;
    }

    loop = (struct EventLoopHandle *)handle_block.data;
    loop->handle_block = handle_block;
    loop->epoll_fd = epoll_fd;
    loop->wake_fd = wake_fd;
    loop->sources_block = sources_block;
    loop->sources_capacity = INITIAL_SOURCE_CAPACITY;
    loop->next_id = 1;
    loop->scratch_block.data = NULL;
    loop->scratch_block.size = 0;
    loop->scratch_capacity = 0;

    result = allocate_slot(loop, &slot);
    if (result != CORE_OK) {
        goto fail_sources;
    }
    slot->id = loop->next_id++;
    slot->fd = wake_fd;
    slot->kind = EVENT_SOURCE_KIND_WAKE;

    event.events = EVENT_READABLE;
    event.data.u64 = slot->id;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, wake_fd, &event) != 0) {
        result = last_error();
        goto fail_sources;
    }

    *out_loop = loop;
    return CORE_OK;

fail_sources:
    (void)memory_free(&sources_block);
fail_handle:
    (void)memory_free(&handle_block);
fail_fds:
    (void)close(wake_fd);
    (void)close(epoll_fd);
    return result;
}

ErrorCode eventloop_close(EventLoopHandle *loop)
{
    struct EventLoopHandle *handle;
    EventSourceRecord *records;
    MemoryBlock sources_block;
    MemoryBlock handle_block;
    ErrorCode result = CORE_OK;
    usize i;

    if (loop == NULL || *loop == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    handle = *loop;
    records = records_of(handle);

    for (i = 0; i < handle->sources_capacity; ++i) {
        if (records[i].id != 0 && (records[i].kind == EVENT_SOURCE_KIND_TIMER ||
                                    records[i].kind == EVENT_SOURCE_KIND_WAKE)) {
            if (close(records[i].fd) != 0 && result == CORE_OK) {
                result = last_error();
            }
        }
    }
    if (close(handle->epoll_fd) != 0 && result == CORE_OK) {
        result = last_error();
    }

    {
        MemoryBlock scratch_block = handle->scratch_block;

        (void)memory_free(&scratch_block);
    }
    sources_block = handle->sources_block;
    handle_block = handle->handle_block;
    (void)memory_free(&sources_block);
    (void)memory_free(&handle_block);
    *loop = NULL;
    return result;
}

/* ------------------------------------------------------------------------ */
/* Sources                                                                  */
/* ------------------------------------------------------------------------ */

ErrorCode eventloop_add_fd(EventLoopHandle loop, i32 fd, EventMask mask,
                          EventSourceId *out_source)
{
    struct epoll_event event;
    EventSourceRecord *slot;
    ErrorCode result;

    if (loop == NULL || out_source == NULL || (mask & ~(EventMask)EVENT_KNOWN_MASK) != 0) {
        return CORE_INVALID_ARGUMENT;
    }
    result = allocate_slot(loop, &slot);
    if (result != CORE_OK) {
        return result;
    }

    event.events = mask;
    event.data.u64 = loop->next_id;
    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd, &event) != 0) {
        return last_error(); /* slot stays free: its id was never written */
    }

    slot->id = loop->next_id++;
    slot->fd = fd;
    slot->kind = EVENT_SOURCE_KIND_FILE_DESCRIPTOR;
    *out_source = slot->id;
    return CORE_OK;
}

ErrorCode eventloop_update_fd(EventLoopHandle loop, EventSourceId source, EventMask mask)
{
    EventSourceRecord *slot;
    struct epoll_event event;

    if (loop == NULL || (mask & ~(EventMask)EVENT_KNOWN_MASK) != 0) {
        return CORE_INVALID_ARGUMENT;
    }
    slot = find_slot(loop, source);
    if (slot == NULL) {
        return CORE_NOT_FOUND;
    }
    event.events = mask;
    event.data.u64 = source;
    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_MOD, slot->fd, &event) != 0) {
        return last_error();
    }
    return CORE_OK;
}

ErrorCode eventloop_remove_source(EventLoopHandle loop, EventSourceId source)
{
    EventSourceRecord *slot;
    i32 fd;
    EventSourceKind kind;

    if (loop == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    slot = find_slot(loop, source);
    if (slot == NULL) {
        return CORE_NOT_FOUND;
    }
    fd = slot->fd;
    kind = slot->kind;
    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL) != 0) {
        return last_error();
    }
    slot->id = 0; /* free the slot before the close() below can fail */
    if (kind == EVENT_SOURCE_KIND_TIMER && close(fd) != 0) {
        return last_error();
    }
    return CORE_OK;
}

ErrorCode eventloop_create_timer(EventLoopHandle loop, DurationMs delay_ms, DurationMs period_ms,
                                 EventSourceId *out_source)
{
    i32 timer_fd;
    struct itimerspec timer_spec;
    struct epoll_event event;
    EventSourceRecord *slot;
    ErrorCode result;

    if (loop == NULL || out_source == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timer_fd < 0) {
        return last_error();
    }

    milliseconds_to_timespec(delay_ms, &timer_spec.it_value);
    milliseconds_to_timespec(period_ms, &timer_spec.it_interval);
    /* timerfd_settime() treats an all-zero it_value as "disarm the timer",
     * but delay_ms == 0 means "fire as soon as possible", not "never". */
    if (timer_spec.it_value.tv_sec == 0 && timer_spec.it_value.tv_nsec == 0) {
        timer_spec.it_value.tv_nsec = 1;
    }
    if (timerfd_settime(timer_fd, 0, &timer_spec, NULL) != 0) {
        result = last_error();
        goto fail_fd;
    }

    result = allocate_slot(loop, &slot);
    if (result != CORE_OK) {
        goto fail_fd;
    }

    event.events = EVENT_READABLE;
    event.data.u64 = loop->next_id;
    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, timer_fd, &event) != 0) {
        result = last_error();
        goto fail_fd;
    }

    slot->id = loop->next_id++;
    slot->fd = timer_fd;
    slot->kind = EVENT_SOURCE_KIND_TIMER;
    *out_source = slot->id;
    return CORE_OK;

fail_fd:
    (void)close(timer_fd);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Waiting and waking                                                       */
/* ------------------------------------------------------------------------ */

/* Grows loop's reusable scratch buffer to hold at least capacity raw
 * epoll_event records, if it does not already. Never shrinks it. */
static ErrorCode ensure_scratch_capacity(struct EventLoopHandle *loop, usize capacity)
{
    usize new_size;
    ErrorCode result;

    if (capacity <= loop->scratch_capacity) {
        return CORE_OK;
    }
    if (core_size_multiply(capacity, sizeof(struct epoll_event), &new_size) != CORE_OK) {
        return CORE_OVERFLOW;
    }
    result = memory_reallocate(&loop->scratch_block, new_size);
    if (result != CORE_OK) {
        return result;
    }
    loop->scratch_capacity = capacity;
    return CORE_OK;
}

ErrorCode eventloop_wait(EventLoopHandle loop, Event *out_events, usize events_capacity,
                        DurationMs timeout_ms, usize *out_count)
{
    struct epoll_event *raw_events;
    i32 timeout_arg;
    i32 result_count;
    usize produced = 0;
    usize i;
    ErrorCode result;

    if (loop == NULL || out_count == NULL || (events_capacity != 0 && out_events == NULL)) {
        return CORE_INVALID_ARGUMENT;
    }
    if (timeout_ms == EVENTLOOP_WAIT_FOREVER) {
        timeout_arg = -1;
    } else if (timeout_ms > EVENTLOOP_MAX_TIMEOUT_MS) {
        return CORE_INVALID_ARGUMENT;
    } else {
        timeout_arg = (i32)timeout_ms;
    }

    if (events_capacity == 0) {
        *out_count = 0;
        return CORE_OK;
    }
    if (events_capacity > EVENTLOOP_MAX_TIMEOUT_MS) {
        return CORE_OVERFLOW; /* absurdly large: also keeps the (i32) cast below safe */
    }
    result = ensure_scratch_capacity(loop, events_capacity);
    if (result != CORE_OK) {
        return result;
    }
    raw_events = (struct epoll_event *)loop->scratch_block.data;

    result_count = epoll_wait(loop->epoll_fd, raw_events, (i32)events_capacity, timeout_arg);
    if (result_count < 0) {
        return last_error();
    }

    for (i = 0; i < (usize)result_count; ++i) {
        EventSourceId id = raw_events[i].data.u64;
        EventSourceRecord *slot = find_slot(loop, id);

        if (slot == NULL) {
            continue; /* removed between epoll noticing it and us reading the result */
        }
        if ((slot->kind == EVENT_SOURCE_KIND_TIMER || slot->kind == EVENT_SOURCE_KIND_WAKE) &&
            (raw_events[i].events & EVENT_READABLE) != 0) {
            result = drain(slot->fd);
            if (result != CORE_OK) {
                return result;
            }
        }
        out_events[produced].source_id = id;
        out_events[produced].kind = slot->kind;
        out_events[produced].mask = raw_events[i].events & EVENT_KNOWN_MASK;
        ++produced;
    }

    *out_count = produced;
    return CORE_OK;
}

ErrorCode eventloop_wake(EventLoopHandle loop)
{
    u64 value = 1;

    if (loop == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (write(loop->wake_fd, &value, sizeof(value)) < 0) {
        /* EAGAIN: a wake is already pending (the counter would overflow an
         * eventfd created without EFD_SEMAPHORE); the pending one still
         * delivers, so there is nothing to report. */
        if (errno != EAGAIN) {
            return last_error();
        }
    }
    return CORE_OK;
}
