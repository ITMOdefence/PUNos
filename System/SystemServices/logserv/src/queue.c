#include <logserv.h>
#include <punos/syscall/syscall.h>
#include "wire.h"

struct LogNode {
    MemoryBlock block;
    LogNode *next;
    String component, message;
    WallClockTime timestamp;
    i32 pid, tid;
    LogSeverity severity;
};

static void release(LogNode *node)
{
    MemoryBlock block = node->block;
    (void)string_destroy(&node->component);
    (void)string_destroy(&node->message);
    (void)memory_free(&block);
}

static void failed(LogQueue *q, ErrorCode error)
{
    (void)mutex_lock(q->mutex, SYNC_WAIT_FOREVER);
    q->failed = true;
    q->failure = error;
    EventLoopHandle loop = q->loop;
    (void)condition_signal_all(q->condition);
    (void)mutex_unlock(q->mutex);
    if (loop != NULL) (void)eventloop_wake(loop);
}

static i32 writer(void *argument)
{
    LogQueue *q = argument;
    LogStoreHandle store = NULL;
    LogStoreConfig config = { q->path, 0600 };
    ErrorCode error = log_store_open(&config, &store);
    (void)mutex_lock(q->mutex, SYNC_WAIT_FOREVER);
    q->ready = error == CORE_OK;
    q->failed = error != CORE_OK;
    q->failure = error;
    (void)condition_signal_all(q->condition);
    (void)mutex_unlock(q->mutex);
    if (error != CORE_OK) return THREAD_EXIT_FAILURE;
    for (;;) {
        (void)mutex_lock(q->mutex, SYNC_WAIT_FOREVER);
        while (q->head == NULL && !q->stopping && !q->failed) {
            error = condition_wait(q->condition, q->mutex, SYNC_WAIT_FOREVER);
            if (error != CORE_OK && error != CORE_INTERRUPTED) {
                q->failed = true; q->failure = error;
            }
        }
        if (q->failed || (q->head == NULL && q->stopping)) {
            (void)mutex_unlock(q->mutex);
            break;
        }
        LogNode *node = q->head;
        q->head = node->next;
        if (q->head == NULL) q->tail = NULL;
        (void)mutex_unlock(q->mutex);
        LogRecordInput record = { node->timestamp, node->pid, node->tid,
                                  node->severity, &node->component, &node->message };
        error = log_store_append(store, &record);
        usize bytes = node->component.length + node->message.length;
        release(node);
        (void)mutex_lock(q->mutex, SYNC_WAIT_FOREVER);
        --q->records; q->bytes -= bytes;
        bool overflow = q->overflow;
        q->overflow = false;
        bool stopping = q->stopping;
        (void)mutex_unlock(q->mutex);
        if (error != CORE_OK) { failed(q, error); break; }
        if (overflow && !stopping) (void)log_queue_self(q, "queue overflow");
    }
    if (log_queue_failed(q)) {
        (void)mutex_lock(q->mutex, SYNC_WAIT_FOREVER);
        EventLoopHandle failed_loop = q->loop;
        (void)mutex_unlock(q->mutex);
        if (failed_loop != NULL) (void)eventloop_wake(failed_loop);
    }
    if (!log_queue_failed(q)) {
        error = log_store_flush(store);
        if (error != CORE_OK) failed(q, error);
    }
    error = log_store_close(&store);
    if (error != CORE_OK) failed(q, error);
    return log_queue_failed(q) ? THREAD_EXIT_FAILURE : THREAD_EXIT_SUCCESS;
}

ErrorCode log_queue_start(LogQueue *q, const String *path)
{
    *q = (LogQueue){ .path = path };
    ErrorCode error = mutex_create(&q->mutex);
    if (error != CORE_OK) return error;
    error = condition_create(&q->condition);
    if (error != CORE_OK) { (void)mutex_destroy(&q->mutex); return error; }
    error = thread_create(writer, q, &q->worker);
    if (error != CORE_OK) {
        (void)condition_destroy(&q->condition); (void)mutex_destroy(&q->mutex);
        return error;
    }
    (void)mutex_lock(q->mutex, SYNC_WAIT_FOREVER);
    while (!q->ready && !q->failed) {
        error = condition_wait(q->condition, q->mutex, SYNC_WAIT_FOREVER);
        if (error != CORE_OK && error != CORE_INTERRUPTED) {
            q->failed = true; q->failure = error;
        }
    }
    error = q->failed ? q->failure : CORE_OK;
    (void)mutex_unlock(q->mutex);
    return error;
}

static ErrorCode enqueue(LogQueue *q, i32 pid, i32 tid, LogSeverity severity,
                          Bytes component, Bytes message)
{
    usize bytes;
    if (core_size_add(component.size, message.size, &bytes) != CORE_OK) return CORE_OVERFLOW;
    (void)mutex_lock(q->mutex, SYNC_WAIT_FOREVER);
    ErrorCode error = CORE_OK;
    if (q->failed) error = CORE_IO_ERROR;
    else if (!q->ready || q->stopping) error = CORE_INVALID_STATE;
    else if (q->records >= LOG_QUEUE_RECORDS || bytes > LOG_QUEUE_BYTES - q->bytes) {
        q->overflow = true; error = CORE_WOULD_BLOCK;
    }
    (void)mutex_unlock(q->mutex);
    if (error != CORE_OK) return error;
    MemoryBlock block;
    error = memory_allocate(sizeof(LogNode), &block);
    if (error != CORE_OK) return error;
    LogNode *node = (LogNode *)block.data;
    *node = (LogNode){ .block = block, .pid = pid, .tid = tid, .severity = severity };
    error = string_from_utf8(component, &node->component);
    if (error == CORE_OK) error = string_from_utf8(message, &node->message);
    if (error == CORE_OK) error = time_wall_clock_now(&node->timestamp);
    if (error != CORE_OK) { release(node); return error; }
    (void)mutex_lock(q->mutex, SYNC_WAIT_FOREVER);
    if (q->failed) error = CORE_IO_ERROR;
    else if (q->stopping) error = CORE_INVALID_STATE;
    else if (q->records >= LOG_QUEUE_RECORDS || bytes > LOG_QUEUE_BYTES - q->bytes) {
        q->overflow = true; error = CORE_WOULD_BLOCK;
    } else {
        if (q->tail != NULL) q->tail->next = node;
        else q->head = node;
        q->tail = node;
        ++q->records; q->bytes += bytes;
        (void)condition_signal_one(q->condition);
    }
    (void)mutex_unlock(q->mutex);
    if (error != CORE_OK) release(node);
    return error;
}

ErrorCode log_queue_submit(LogQueue *q, Bytes payload)
{
    if (payload.data == NULL || payload.size < 17) return CORE_INVALID_ARGUMENT;
    if (wire_read(payload.data, 2) != 1) return CORE_UNSUPPORTED;
    i32 pid = (i32)wire_read(payload.data + 2, 4);
    i32 tid = (i32)wire_read(payload.data + 6, 4);
    u8 severity = payload.data[10];
    usize component = wire_read(payload.data + 11, 2);
    usize message = wire_read(payload.data + 13, 4);
    if (pid <= 0 || tid <= 0 || severity > LOG_SEVERITY_FATAL || component == 0)
        return CORE_INVALID_ARGUMENT;
    if (component > LOG_COMPONENT_MAX_LENGTH || message > LOG_MESSAGE_MAX_LENGTH)
        return CORE_OVERFLOW;
    if (payload.size != 17 + component + message) return CORE_INVALID_ARGUMENT;
    return enqueue(q, pid, tid, (LogSeverity)severity,
                    (Bytes){ payload.data + 17, component },
                    (Bytes){ payload.data + 17 + component, message });
}

ErrorCode log_queue_self(LogQueue *q, const char *message)
{
    usize length = 0;
    while (message[length] != '\0') ++length;
    return enqueue(q, getpid(), gettid(), LOG_SEVERITY_INFO,
                    (Bytes){ (u8 *)"logserv", 7 }, (Bytes){ (u8 *)message, length });
}

bool log_queue_failed(LogQueue *q)
{
    if (q->mutex == NULL) return true;
    (void)mutex_lock(q->mutex, SYNC_WAIT_FOREVER);
    bool result = q->failed;
    (void)mutex_unlock(q->mutex);
    return result;
}

ErrorCode log_queue_stop(LogQueue *q)
{
    if (q->mutex == NULL) return CORE_OK;
    (void)mutex_lock(q->mutex, SYNC_WAIT_FOREVER);
    q->stopping = true;
    (void)condition_signal_all(q->condition);
    (void)mutex_unlock(q->mutex);
    ThreadExitStatus status = THREAD_EXIT_SUCCESS;
    ErrorCode error = CORE_OK;
    if (q->worker != NULL) error = thread_join(&q->worker, &status);
    if (error != CORE_OK) return error; /* Never free resources a live worker owns. */
    while (q->head != NULL) {
        LogNode *node = q->head;
        q->head = node->next;
        release(node);
    }
    q->tail = NULL; q->records = q->bytes = 0;
    if (q->failed || status != THREAD_EXIT_SUCCESS) error = CORE_IO_ERROR;
    (void)condition_destroy(&q->condition);
    (void)mutex_destroy(&q->mutex);
    return error;
}
