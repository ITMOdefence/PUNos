#ifndef PUNOS_LOGSERV_H
#define PUNOS_LOGSERV_H
#include <special/log.h>
#include <sys/sync.h>
#include <sys/eventloop.h>
#define LOG_QUEUE_RECORDS 4096
#define LOG_QUEUE_BYTES 4194304
#ifndef LOG_ENDPOINT
#define LOG_ENDPOINT "/run/punos/logserv.sock"
#endif
typedef struct LogNode LogNode;
typedef struct LogQueue {
    Mutex mutex;
    Condition condition;
    ThreadHandle worker;
    EventLoopHandle loop;
    const String *path;
    LogNode *head, *tail;
    usize records, bytes;
    bool ready, failed, stopping, overflow;
    ErrorCode failure;
} LogQueue;
ErrorCode log_queue_start(LogQueue *queue, const String *path);
ErrorCode log_queue_submit(LogQueue *queue, Bytes payload);
ErrorCode log_queue_self(LogQueue *queue, const char *message);
bool log_queue_failed(LogQueue *queue);
ErrorCode log_queue_stop(LogQueue *queue);
#endif
