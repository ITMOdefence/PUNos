#ifndef PUNOS_SPECIAL_LOG_H
#define PUNOS_SPECIAL_LOG_H
#include <sys/core.h>
#include <sys/string.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/process.h>
#include <sys/thread.h>

typedef struct LogStoreHandle *LogStoreHandle;
typedef struct LogReaderHandle *LogReaderHandle;
typedef struct LogStoreConfig { const String *path; FilePermissions permissions; } LogStoreConfig;
typedef enum LogSeverity {
    LOG_SEVERITY_TRACE = 0, LOG_SEVERITY_DEBUG = 1, LOG_SEVERITY_INFO = 2,
    LOG_SEVERITY_WARNING = 3, LOG_SEVERITY_ERROR = 4, LOG_SEVERITY_FATAL = 5
} LogSeverity;
typedef struct LogRecordInput {
    WallClockTime timestamp;
    ProcessId process_id;
    ThreadId thread_id;
    LogSeverity severity;
    const String *component, *message;
} LogRecordInput;
typedef struct LogRecord {
    WallClockTime timestamp;
    ProcessId process_id;
    ThreadId thread_id;
    LogSeverity severity;
    String component, message;
} LogRecord;
typedef enum LogReadStatus { LOG_READ_STATUS_LOG_RECORD = 0, LOG_READ_STATUS_END_OF_FILE = 1 } LogReadStatus;
#define LOG_FRAME_HEADER_SIZE ((usize)27)
#define LOG_COMPONENT_MAX_LENGTH ((usize)256)
#define LOG_MESSAGE_MAX_LENGTH ((usize)16384)
#define LOG_FRAME_MAX_SIZE ((usize)16667)

/* Handles belong to one thread. File I/O can block. Paths are copied;
 * records returned by next own their strings. close does not flush. */
ErrorCode log_store_open(const LogStoreConfig *config, LogStoreHandle *out_store);
ErrorCode log_store_append(LogStoreHandle store, const LogRecordInput *record);
ErrorCode log_store_flush(LogStoreHandle store);
ErrorCode log_store_rotate(LogStoreHandle store, const LogStoreConfig *config);
ErrorCode log_store_close(LogStoreHandle *store);
ErrorCode log_reader_open(const String *path, LogReaderHandle *out_reader);
ErrorCode log_reader_next(LogReaderHandle reader, LogReadStatus *out_status, LogRecord *out_record);
ErrorCode log_record_destroy(LogRecord *record);
ErrorCode log_reader_close(LogReaderHandle *reader);
#endif
