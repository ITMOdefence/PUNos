#ifndef PUNOS_SPECIAL_LOG_H
#define PUNOS_SPECIAL_LOG_H

/*
 * loglib (special/log) - sequential storage and retrieval of structured log
 * records.
 *
 * Specification: DevDoc/SpecialLibs/loglib.md
 *
 * loglib is the low-level store behind logserv: it encodes a record into a
 * frame, writes it to a file, and decodes frames back into records when a
 * reader asks for them. It is not a service, it does no IPC, and it exposes
 * nothing to applications. Choosing the directory and file name, the
 * retention policy, level filtering, history search and the moment of
 * rotation all belong to logserv; this library only moves bytes.
 *
 * A log file is a sequence of frames with no global header. Every integer
 * field is little-endian and frame_size covers the whole frame, itself
 * included:
 *
 *   offset  size  field
 *   0       4     frame_size
 *   4       8     timestamp      (WallClockTime, Unix epoch milliseconds)
 *   12      4     process_id     (ProcessId)
 *   16      4     thread_id      (ThreadId)
 *   20      1     severity       (LogSeverity)
 *   21      2     component_length
 *   23      4     message_length
 *   27      n     component      (UTF-8, no NUL)
 *   27+n    m     message        (UTF-8, no NUL)
 *
 * Memory comes from libmem, files from libfile and text from libstr. The
 * identifiers in a record are supplied by logserv; the library never reads a
 * clock or asks the kernel who it is running as.
 *
 * Conventions for every operation that can fail:
 *   - it returns ErrorCode, and CORE_OK is the only success value;
 *   - results are written through out_* pointers;
 *   - out_* is defined only on CORE_OK, except out_status in
 *     log_reader_next(), which is defined on CORE_OK, and out_record, which is
 *     defined only when out_status is LOG_READ_STATUS_LOG_RECORD;
 *   - a closed handle makes every operation on it return CORE_INVALID_STATE.
 *
 * Threading: one store or reader belongs to the thread that opened it.
 * Concurrent calls on one handle are not allowed; logserv serialises them
 * with libsync.
 *
 * Blocking: log_store_append(), log_store_flush() and log_store_rotate() may
 * block on file I/O and must not be called from an event loop that also
 * serves UI or IPC.
 */

#include <sys/core.h>
#include <sys/file.h>
#include <sys/memory.h>
#include <sys/process.h>
#include <sys/string.h>
#include <sys/thread.h>
#include <sys/time.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                */
/* ------------------------------------------------------------------------ */

/* Bytes of a frame before the component text: the fields of the table above. */
#define LOG_FRAME_HEADER_SIZE ((usize)27)

/* Longest accepted component, in bytes, not counting a NUL (none is stored). */
#define LOG_COMPONENT_MAX_LENGTH ((usize)256)

/* Longest accepted message, in bytes, not counting a NUL (none is stored). */
#define LOG_MESSAGE_MAX_LENGTH ((usize)16384)

/*
 * LOG_FRAME_HEADER_SIZE + LOG_COMPONENT_MAX_LENGTH + LOG_MESSAGE_MAX_LENGTH.
 * A record whose total frame would exceed this is rejected with
 * CORE_OVERFLOW rather than written, so frame_size always fits in its u32.
 */
#define LOG_FRAME_MAX_SIZE ((usize)16667)

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/*
 * Opaque context for appending records to one log file. Created by
 * log_store_open() and released exactly once by log_store_close(), which
 * writes NULL back through its pointer. Never build one by hand.
 */
typedef struct LogStoreHandle *LogStoreHandle;

/*
 * Opaque context for reading one log file from the beginning. Created by
 * log_reader_open() and released exactly once by log_reader_close(). A reader
 * and a store may be open on the same file at the same time; they hold
 * separate handles.
 */
typedef struct LogReaderHandle *LogReaderHandle;

/*
 * What to open. path is not owned: log_store_open() and log_store_rotate()
 * copy it before returning, so the caller may destroy its own string
 * immediately afterwards. permissions applies to a file that has to be
 * created and must be within 0000..0777.
 */
typedef struct LogStoreConfig {
    const String *path;
    FilePermissions permissions;
} LogStoreConfig;

/*
 * How important a record is. logserv decides what to filter out; loglib
 * stores the value as given and never compares severities.
 *
 * The specification lists these values by name only, without pinning
 * numbers. This library numbers them in the order the table above lists them,
 * starting at 0; the numbers are then part of the ABI and are written to the
 * file, so they must never be renumbered.
 */
typedef enum LogSeverity {
    LOG_SEVERITY_TRACE = 0,
    LOG_SEVERITY_DEBUG = 1,
    LOG_SEVERITY_INFO = 2,
    LOG_SEVERITY_WARNING = 3,
    LOG_SEVERITY_ERROR = 4,
    LOG_SEVERITY_FATAL = 5
} LogSeverity;

/*
 * A record to append. Nothing here is owned by the library: component and
 * message only have to stay valid for the duration of the
 * log_store_append() call, which serialises both before returning.
 *
 * timestamp, process_id, thread_id and severity are supplied by logserv, not
 * read from the kernel, so a record can be labelled with the time and origin
 * its author intended.
 */
typedef struct LogRecordInput {
    WallClockTime timestamp;
    ProcessId process_id;
    ThreadId thread_id;
    LogSeverity severity;
    const String *component;
    const String *message;
} LogRecordInput;

/*
 * A record read back from a log file. Unlike LogRecordInput this one owns its
 * two strings: both must be released exactly once through
 * log_record_destroy(). The numeric fields are copies of what the file held.
 */
typedef struct LogRecord {
    WallClockTime timestamp;
    ProcessId process_id;
    ThreadId thread_id;
    LogSeverity severity;
    String component;
    String message;
} LogRecord;

/*
 * Outcome of one log_reader_next() attempt: a record was decoded and
 * out_record is set, or the file ended cleanly and out_record is untouched.
 * A truncated frame is not reported through this enumeration; it is an error.
 */
typedef enum LogReadStatus {
    LOG_READ_STATUS_LOG_RECORD = 0,
    LOG_READ_STATUS_END_OF_FILE = 1
} LogReadStatus;

_Static_assert(sizeof(LogStoreConfig) == 16, "LogStoreConfig ABI mismatch");
_Static_assert(sizeof(LogSeverity) == 4, "LogSeverity ABI mismatch");
_Static_assert(sizeof(LogRecordInput) == 40, "LogRecordInput ABI mismatch");
_Static_assert(sizeof(LogRecord) == 56, "LogRecord ABI mismatch");
_Static_assert(sizeof(LogReadStatus) == 4, "LogReadStatus ABI mismatch");

/* ------------------------------------------------------------------------ */
/* Operations - store                                                        */
/* ------------------------------------------------------------------------ */

/*
 * Opens, creating it if needed, the file at config->path in append mode and
 * creates a store for adding records to it. An existing file is never
 * truncated: whatever earlier runs wrote stays.
 *
 * config->path is copied into the store.
 *
 * Errors: CORE_INVALID_ARGUMENT if config, config->path, config->path->data
 * or out_store is invalid, or permissions has bits outside 0000..0777;
 * codes from core_error_from_errno() if open() fails (CORE_NOT_FOUND if a
 * parent directory is missing); errors of libmem and libstr.
 */
ErrorCode log_store_open(const LogStoreConfig *config, LogStoreHandle *out_store);

/*
 * Serialises record into one frame and appends it to the current file. The
 * whole frame is written before returning, so a successful call never leaves
 * a partial record behind. Records of one store keep the order they were
 * appended in.
 *
 * The strings are only read during the call; the library keeps no pointer to
 * them afterwards.
 *
 * Errors: CORE_INVALID_ARGUMENT if store or record is NULL, if either string
 * pointer is NULL, or if severity is not a LogSeverity value; CORE_INVALID_
 * STATE if the store is closed; CORE_OVERFLOW if component, message or the
 * whole frame exceeds its maximum; codes from core_error_from_errno() if the
 * write fails (a failure may leave an incomplete frame at the end of the
 * file, which log_reader_next() reports as CORE_IO_ERROR); errors of libmem.
 */
ErrorCode log_store_append(LogStoreHandle store, const LogRecordInput *record);

/*
 * Pushes every frame written so far to the device through file_flush().
 *
 * Errors: CORE_INVALID_ARGUMENT if store is NULL; CORE_INVALID_STATE if the
 * store is closed; codes from core_error_from_errno() if the flush fails.
 */
ErrorCode log_store_flush(LogStoreHandle store);

/*
 * Makes the file described by config the current one, for writes from now on.
 * logserv chooses the new name; this library does not generate one.
 *
 * The new file is opened first and only then is the previous one closed, so
 * a failure at either step leaves the previous file current and still
 * writable. config->path is copied.
 *
 * Errors: CORE_INVALID_ARGUMENT if store is NULL or config is invalid;
 * CORE_INVALID_STATE if the store is closed; codes from
 * core_error_from_errno() if open() or close() fails; errors of libmem and
 * libstr.
 */
ErrorCode log_store_rotate(LogStoreHandle store, const LogStoreConfig *config);

/*
 * Closes the current file without flushing it implicitly - call
 * log_store_flush() first if the last records must reach the device - releases
 * the store and writes NULL through *store. The file itself stays on disk
 * and keeps its name.
 *
 * Errors: CORE_INVALID_ARGUMENT if store or *store is NULL; codes from
 * core_error_from_errno() if close() fails (the handle is still released).
 */
ErrorCode log_store_close(LogStoreHandle *store);

/* ------------------------------------------------------------------------ */
/* Operations - reader                                                       */
/* ------------------------------------------------------------------------ */

/*
 * Opens the existing log file at path for sequential reading, positioned at
 * its first frame. The file is never created: a missing path is an error.
 *
 * Errors: CORE_INVALID_ARGUMENT if path, path->data or out_reader is invalid;
 * codes from core_error_from_errno() if open() fails (CORE_NOT_FOUND if the
 * file does not exist); errors of libmem.
 */
ErrorCode log_reader_open(const String *path, LogReaderHandle *out_reader);

/*
 * Decodes the next frame. On LOG_READ_STATUS_LOG_RECORD, out_record owns its
 * component and message and must be released with log_record_destroy(). On
 * LOG_READ_STATUS_END_OF_FILE the file ended cleanly between frames and
 * out_record is not written.
 *
 * A file that ends in the middle of a frame, or a frame whose declared sizes
 * do not add up, is a damaged log rather than a clean end: it returns
 * CORE_IO_ERROR. Deciding whether to repair or replace the file is up to
 * logserv.
 *
 * Errors: CORE_INVALID_ARGUMENT if reader, out_status or out_record is NULL;
 * CORE_INVALID_STATE if the reader is closed; CORE_IO_ERROR if the frame is
 * corrupt or the file ends in an incomplete frame; codes from
 * core_error_from_errno() if the read fails; errors of libmem and libstr.
 */
ErrorCode log_reader_next(LogReaderHandle reader, LogReadStatus *out_status, LogRecord *out_record);

/*
 * Releases the component and message of a record read by log_reader_next()
 * and leaves the structure invalid. Destroying a record twice, or one that
 * was never read, is not allowed.
 *
 * Errors: CORE_INVALID_ARGUMENT if record is NULL; errors of libmem.
 */
ErrorCode log_record_destroy(LogRecord *record);

/*
 * Closes the read handle, releases the reader and writes NULL through
 * *reader. Unread records stay the caller's to destroy.
 *
 * Errors: CORE_INVALID_ARGUMENT if reader or *reader is NULL; codes from
 * core_error_from_errno() if close() fails (the handle is still released).
 */
ErrorCode log_reader_close(LogReaderHandle *reader);

#endif
