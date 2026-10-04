#include <special/log.h>
#include <sys/memory.h>

struct LogStoreHandle { MemoryBlock block; FileHandle file; String path; };
struct LogReaderHandle { MemoryBlock block; FileHandle file; bool failed; };

static void put(u8 *p, u64 value, usize width)
{
    for (usize i = 0; i < width; ++i) p[i] = (u8)(value >> (i * 8));
}
static u64 get(const u8 *p, usize width)
{
    u64 value = 0;
    for (usize i = 0; i < width; ++i) value |= (u64)p[i] << (i * 8);
    return value;
}
static ErrorCode copy_path(const String *path, String *copy)
{
    const char *text;
    if (path == NULL || path->length == 0 || string_c_str(path, &text) != CORE_OK)
        return CORE_INVALID_ARGUMENT;
    return string_from_utf8((Bytes){(u8 *)text, path->length}, copy);
}
static ErrorCode open_store_file(const LogStoreConfig *config, FileHandle *file, String *path)
{
    ErrorCode result;
    if (config == NULL || (config->permissions & ~(FilePermissions)0777) != 0)
        return CORE_INVALID_ARGUMENT;
    result = copy_path(config->path, path);
    if (result != CORE_OK) return result;
    result = file_open(path, FILE_OPEN_MODE_APPEND_CREATE, config->permissions, file);
    if (result != CORE_OK) (void)string_destroy(path);
    return result;
}
ErrorCode log_store_open(const LogStoreConfig *config, LogStoreHandle *out_store)
{
    MemoryBlock block;
    ErrorCode result;
    if (out_store == NULL) return CORE_INVALID_ARGUMENT;
    result = memory_allocate(sizeof(struct LogStoreHandle), &block);
    if (result != CORE_OK) return result;
    LogStoreHandle store = (LogStoreHandle)block.data;
    store->block = block;
    result = open_store_file(config, &store->file, &store->path);
    if (result != CORE_OK) { (void)memory_free(&block); return result; }
    *out_store = store;
    return CORE_OK;
}
ErrorCode log_store_append(LogStoreHandle store, const LogRecordInput *record)
{
    String component, message;
    MemoryBlock frame;
    ErrorCode result;
    if (record == NULL || record->component == NULL || record->message == NULL ||
        (u32)record->severity > LOG_SEVERITY_FATAL) return CORE_INVALID_ARGUMENT;
    if (store == NULL || store->file == NULL) return CORE_INVALID_STATE;
    if (record->component->length > LOG_COMPONENT_MAX_LENGTH ||
        record->message->length > LOG_MESSAGE_MAX_LENGTH) return CORE_OVERFLOW;
    const char *text;
    if (string_c_str(record->component, &text) != CORE_OK) return CORE_INVALID_ARGUMENT;
    result = string_from_utf8((Bytes){(u8 *)text, record->component->length}, &component);
    if (result != CORE_OK) return result;
    if (string_c_str(record->message, &text) != CORE_OK) {
        (void)string_destroy(&component);
        return CORE_INVALID_ARGUMENT;
    }
    result = string_from_utf8((Bytes){(u8 *)text, record->message->length}, &message);
    if (result != CORE_OK) { (void)string_destroy(&component); return result; }
    usize size = LOG_FRAME_HEADER_SIZE + component.length + message.length;
    result = memory_allocate(size, &frame);
    if (result == CORE_OK) {
        put(frame.data, size, 4);
        put(frame.data + 4, record->timestamp, 8);
        put(frame.data + 12, (u32)record->process_id, 4);
        put(frame.data + 16, (u32)record->thread_id, 4);
        frame.data[20] = (u8)record->severity;
        put(frame.data + 21, component.length, 2);
        put(frame.data + 23, message.length, 4);
        (void)memory_copy((Bytes){frame.data + LOG_FRAME_HEADER_SIZE, component.length},
                          (Bytes){(u8 *)component.data, component.length});
        (void)memory_copy((Bytes){frame.data + LOG_FRAME_HEADER_SIZE + component.length, message.length},
                          (Bytes){(u8 *)message.data, message.length});
        result = file_write_all(store->file, (Bytes){frame.data, frame.size});
        (void)memory_free(&frame);
    }
    (void)string_destroy(&component);
    (void)string_destroy(&message);
    return result;
}
ErrorCode log_store_flush(LogStoreHandle store)
{
    if (store == NULL || store->file == NULL) return CORE_INVALID_STATE;
    return file_flush(store->file);
}
ErrorCode log_store_rotate(LogStoreHandle store, const LogStoreConfig *config)
{
    FileHandle file;
    String path;
    ErrorCode result;
    if (store == NULL || store->file == NULL) return CORE_INVALID_STATE;
    result = open_store_file(config, &file, &path);
    if (result != CORE_OK) return result;
    result = file_close(&store->file);
    if (result != CORE_OK) {
        /* libfile consumes the old handle even on close failure. Keep the
         * store closable, but reject further I/O; never adopt the new file. */
        (void)file_close(&file);
        (void)string_destroy(&path);
        return result;
    }
    (void)string_destroy(&store->path);
    store->file = file;
    store->path = path;
    return CORE_OK;
}
ErrorCode log_store_close(LogStoreHandle *store)
{
    ErrorCode result = CORE_OK;
    MemoryBlock block;
    if (store == NULL) return CORE_INVALID_ARGUMENT;
    if (*store == NULL) return CORE_INVALID_STATE;
    block = (*store)->block;
    if ((*store)->file != NULL) result = file_close(&(*store)->file);
    (void)string_destroy(&(*store)->path);
    (void)memory_free(&block);
    *store = NULL;
    return result;
}
ErrorCode log_reader_open(const String *path, LogReaderHandle *out_reader)
{
    MemoryBlock block;
    String validated;
    ErrorCode result;
    if (out_reader == NULL) return CORE_INVALID_ARGUMENT;
    result = copy_path(path, &validated);
    if (result != CORE_OK) return result;
    result = memory_allocate(sizeof(struct LogReaderHandle), &block);
    if (result != CORE_OK) { (void)string_destroy(&validated); return result; }
    LogReaderHandle reader = (LogReaderHandle)block.data;
    reader->block = block;
    result = file_open(&validated, FILE_OPEN_MODE_READ, 0, &reader->file);
    (void)string_destroy(&validated);
    if (result != CORE_OK) { (void)memory_free(&block); return result; }
    *out_reader = reader;
    return CORE_OK;
}
/* EOF is legal only before the first byte of a frame. */
static ErrorCode read_exact(FileHandle file, u8 *data, usize size, bool *empty)
{
    usize used = 0;
    *empty = false;
    while (used < size) {
        usize count;
        ErrorCode result = file_read(file, (Bytes){data + used, size - used}, &count);
        if (result == CORE_INTERRUPTED) continue;
        if (result != CORE_OK) return result;
        if (count == 0) {
            if (used != 0) return CORE_IO_ERROR;
            *empty = true;
            return CORE_OK;
        }
        used += count;
    }
    return CORE_OK;
}
ErrorCode log_reader_next(LogReaderHandle reader, LogReadStatus *out_status, LogRecord *out_record)
{
    u8 header[LOG_FRAME_HEADER_SIZE];
    MemoryBlock payload;
    LogRecord record;
    bool empty;
    ErrorCode result;
    if (out_status == NULL || out_record == NULL) return CORE_INVALID_ARGUMENT;
    if (reader == NULL) return CORE_INVALID_STATE;
    if (reader->failed) return CORE_IO_ERROR;
    (void)memory_set((Bytes){(u8 *)&record, sizeof(record)}, 0);
    result = read_exact(reader->file, header, sizeof(header), &empty);
    if (result != CORE_OK) { reader->failed = true; return result; }
    if (empty) { *out_status = LOG_READ_STATUS_END_OF_FILE; return CORE_OK; }
    usize size = (usize)get(header, 4);
    usize component = (usize)get(header + 21, 2), message = (usize)get(header + 23, 4);
    if (component > LOG_COMPONENT_MAX_LENGTH || message > LOG_MESSAGE_MAX_LENGTH ||
        size != LOG_FRAME_HEADER_SIZE + component + message || size > LOG_FRAME_MAX_SIZE ||
        header[20] > LOG_SEVERITY_FATAL) {
        reader->failed = true;
        return CORE_IO_ERROR;
    }
    result = memory_allocate(component + message, &payload);
    if (result != CORE_OK) { reader->failed = true; return result; }
    result = read_exact(reader->file, payload.data, payload.size, &empty);
    if (result == CORE_OK && empty) result = CORE_IO_ERROR;
    if (result == CORE_OK) result = string_from_utf8((Bytes){payload.data, component}, &record.component);
    if (result == CORE_OK) result = string_from_utf8(
        (Bytes){payload.data == NULL ? NULL : payload.data + component, message}, &record.message);
    (void)memory_free(&payload);
    if (result != CORE_OK) {
        (void)string_destroy(&record.component);
        reader->failed = true;
        return result == CORE_INVALID_ARGUMENT || result == CORE_OVERFLOW ? CORE_IO_ERROR : result;
    }
    record.timestamp = get(header + 4, 8);
    record.process_id = (i32)(u32)get(header + 12, 4);
    record.thread_id = (i32)(u32)get(header + 16, 4);
    record.severity = (LogSeverity)header[20];
    (void)memory_copy((Bytes){(u8 *)out_record, sizeof(record)},
                      (Bytes){(u8 *)&record, sizeof(record)});
    *out_status = LOG_READ_STATUS_LOG_RECORD;
    return CORE_OK;
}
ErrorCode log_record_destroy(LogRecord *record)
{
    ErrorCode result;
    if (record == NULL) return CORE_INVALID_ARGUMENT;
    result = string_destroy(&record->component);
    if (result != CORE_OK) return result;
    result = string_destroy(&record->message);
    if (result == CORE_OK) (void)memory_set((Bytes){(u8 *)record, sizeof(*record)}, 0);
    return result;
}
ErrorCode log_reader_close(LogReaderHandle *reader)
{
    MemoryBlock block;
    ErrorCode result;
    if (reader == NULL) return CORE_INVALID_ARGUMENT;
    if (*reader == NULL) return CORE_INVALID_STATE;
    block = (*reader)->block;
    result = file_close(&(*reader)->file);
    (void)memory_free(&block);
    *reader = NULL;
    return result;
}
