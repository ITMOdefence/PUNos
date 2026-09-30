#include <special/log.h>

#include <punos/syscall/constants.h>
#include <punos/syscall/syscall.h>
#include <sys/memory.h>
#include <sys/string.h>

struct LogStoreHandle {
    FileHandle file;
    String path;
};

struct LogReaderHandle {
    FileHandle file;
};

static void log_write_u16(u8 *buffer, u16 value)
{
    buffer[0] = (u8)(value & 0xffu);
    buffer[1] = (u8)((value >> 8) & 0xffu);
}

static void log_write_u32(u8 *buffer, u32 value)
{
    buffer[0] = (u8)(value & 0xffu);
    buffer[1] = (u8)((value >> 8) & 0xffu);
    buffer[2] = (u8)((value >> 16) & 0xffu);
    buffer[3] = (u8)((value >> 24) & 0xffu);
}

static void log_write_u64(u8 *buffer, u64 value)
{
    for (u32 index = 0; index < 8; ++index) {
        buffer[index] = (u8)((value >> (index * 8)) & 0xffu);
    }
}

static u16 log_read_u16(const u8 *buffer)
{
    return (u16)((u16)buffer[0] | ((u16)buffer[1] << 8));
}

static u32 log_read_u32(const u8 *buffer)
{
    return (u32)buffer[0] | ((u32)buffer[1] << 8) | ((u32)buffer[2] << 16) |
           ((u32)buffer[3] << 24);
}

static u64 log_read_u64(const u8 *buffer)
{
    u64 value = 0;

    for (u32 index = 0; index < 8; ++index) {
        value |= (u64)buffer[index] << (index * 8);
    }
    return value;
}

/* Reads until size bytes are collected or the file ends; *out_read tells how
 * many were actually available. A short read is not an error here: the caller
 * decides whether a partial frame is acceptable. */
static ErrorCode log_read_exact(FileHandle file, u8 *buffer, usize size, usize *out_read)
{
    usize total = 0;

    while (total < size) {
        usize got = 0;
        ErrorCode result = file_read(file, (Bytes){ buffer + total, size - total }, &got);

        if (result != CORE_OK) {
            return result;
        }
        if (got == 0) {
            break;
        }
        total += got;
    }

    *out_read = total;
    return CORE_OK;
}

static ErrorCode log_validate_config(const LogStoreConfig *config)
{
    if (config == NULL || config->path == NULL || config->path->data == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (config->permissions > 0777u) {
        return CORE_INVALID_ARGUMENT;
    }
    return CORE_OK;
}

ErrorCode log_store_open(const LogStoreConfig *config, LogStoreHandle **out_store)
{
    ErrorCode result = log_validate_config(config);

    if (result != CORE_OK) {
        return result;
    }
    if (out_store == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    MemoryBlock block;
    result = memory_allocate(sizeof(LogStoreHandle), &block);
    if (result != CORE_OK) {
        return result;
    }

    LogStoreHandle *store = (LogStoreHandle *)block.data;
    store->file = NULL;
    store->path.data = NULL;
    store->path.length = 0;

    result = string_copy(config->path, &store->path);
    if (result != CORE_OK) {
        memory_free(&block);
        return result;
    }

    result = file_open(config->path, FILE_OPEN_MODE_APPEND_CREATE, config->permissions, &store->file);
    if (result != CORE_OK) {
        (void)string_destroy(&store->path);
        memory_free(&block);
        return result;
    }

    *out_store = store;
    return CORE_OK;
}

ErrorCode log_store_append(LogStoreHandle *store, const LogRecordInput *record)
{
    if (store == NULL || record == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (store->file == NULL) {
        return CORE_INVALID_STATE;
    }
    if (record->component == NULL || record->message == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (record->severity < LOG_SEVERITY_TRACE || record->severity > LOG_SEVERITY_FATAL) {
        return CORE_INVALID_ARGUMENT;
    }

    usize component_length = record->component->length;
    usize message_length = record->message->length;

    if (component_length > LOG_COMPONENT_MAX_LENGTH) {
        return CORE_OVERFLOW;
    }
    if (message_length > LOG_MESSAGE_MAX_LENGTH) {
        return CORE_OVERFLOW;
    }

    usize frame_size = LOG_FRAME_HEADER_SIZE + component_length + message_length;

    if (frame_size > LOG_FRAME_MAX_SIZE) {
        return CORE_OVERFLOW;
    }

    MemoryBlock block;
    ErrorCode result = memory_allocate(frame_size, &block);
    if (result != CORE_OK) {
        return result;
    }

    log_write_u32(block.data, (u32)frame_size);
    log_write_u64(block.data + 4, record->timestamp);
    log_write_u32(block.data + 12, (u32)record->process_id);
    log_write_u32(block.data + 16, (u32)record->thread_id);
    block.data[20] = (u8)record->severity;
    log_write_u16(block.data + 21, (u16)component_length);
    log_write_u32(block.data + 23, (u32)message_length);

    if (component_length > 0) {
        memory_copy((Bytes){ block.data + LOG_FRAME_HEADER_SIZE, component_length },
                    (Bytes){ (u8 *)record->component->data, component_length });
    }
    if (message_length > 0) {
        memory_copy((Bytes){ block.data + LOG_FRAME_HEADER_SIZE + component_length, message_length },
                    (Bytes){ (u8 *)record->message->data, message_length });
    }

    result = file_write_all(store->file, (Bytes){ block.data, frame_size });
    memory_free(&block);
    return result;
}

ErrorCode log_store_flush(LogStoreHandle *store)
{
    if (store == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (store->file == NULL) {
        return CORE_INVALID_STATE;
    }
    return file_flush(store->file);
}

ErrorCode log_store_rotate(LogStoreHandle *store, const LogStoreConfig *config)
{
    ErrorCode result = log_validate_config(config);

    if (result != CORE_OK) {
        return result;
    }
    if (store == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (store->file == NULL) {
        return CORE_INVALID_STATE;
    }

    String new_path;
    result = string_copy(config->path, &new_path);
    if (result != CORE_OK) {
        return result;
    }

    /* The new file is opened first: if that fails the old one stays current. */
    FileHandle new_file = NULL;
    result = file_open(config->path, FILE_OPEN_MODE_APPEND_CREATE, config->permissions, &new_file);
    if (result != CORE_OK) {
        (void)string_destroy(&new_path);
        return result;
    }

    result = file_close(&store->file);
    if (result != CORE_OK) {
        (void)file_close(&new_file);
        (void)string_destroy(&new_path);
        return result;
    }

    (void)string_destroy(&store->path);
    store->path = new_path;
    store->file = new_file;
    return CORE_OK;
}

ErrorCode log_store_close(LogStoreHandle **store)
{
    if (store == NULL || *store == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    LogStoreHandle *target = *store;
    ErrorCode result = CORE_OK;

    if (target->file != NULL) {
        result = file_close(&target->file);
    }
    (void)string_destroy(&target->path);

    MemoryBlock block;
    block.data = (u8 *)target;
    block.size = sizeof(LogStoreHandle);
    (void)memory_free(&block);

    *store = NULL;
    return result;
}

ErrorCode log_reader_open(const String *path, LogReaderHandle **out_reader)
{
    if (path == NULL || path->data == NULL || out_reader == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    MemoryBlock block;
    ErrorCode result = memory_allocate(sizeof(LogReaderHandle), &block);
    if (result != CORE_OK) {
        return result;
    }

    LogReaderHandle *reader = (LogReaderHandle *)block.data;
    reader->file = NULL;

    result = file_open(path, FILE_OPEN_MODE_READ, 0, &reader->file);
    if (result != CORE_OK) {
        memory_free(&block);
        return result;
    }

    *out_reader = reader;
    return CORE_OK;
}

ErrorCode log_reader_next(LogReaderHandle *reader, LogReadStatus *out_status, LogRecord *out_record)
{
    if (reader == NULL || out_status == NULL || out_record == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (reader->file == NULL) {
        return CORE_INVALID_STATE;
    }

    u8 header[LOG_FRAME_HEADER_SIZE];
    usize read = 0;

    ErrorCode result = log_read_exact(reader->file, header, sizeof(header), &read);
    if (result != CORE_OK) {
        return result;
    }
    if (read == 0) {
        *out_status = LOG_READ_STATUS_END_OF_FILE;
        return CORE_OK;
    }
    if (read < sizeof(header)) {
        /* The file ends in the middle of a frame header. */
        return CORE_IO_ERROR;
    }

    u32 frame_size = log_read_u32(header);

    if (frame_size < LOG_FRAME_HEADER_SIZE || frame_size > LOG_FRAME_MAX_SIZE) {
        return CORE_IO_ERROR;
    }

    u16 component_length = log_read_u16(header + 21);
    u32 message_length = log_read_u32(header + 23);
    usize payload_size = (usize)frame_size - LOG_FRAME_HEADER_SIZE;

    if ((usize)component_length + (usize)message_length != payload_size) {
        return CORE_IO_ERROR;
    }

    MemoryBlock block;
    result = memory_allocate(payload_size, &block);
    if (result != CORE_OK) {
        return result;
    }

    result = log_read_exact(reader->file, block.data, payload_size, &read);
    if (result != CORE_OK) {
        memory_free(&block);
        return result;
    }
    if (read < payload_size) {
        memory_free(&block);
        return CORE_IO_ERROR;
    }

    out_record->component.data = NULL;
    out_record->component.length = 0;
    out_record->message.data = NULL;
    out_record->message.length = 0;

    result = string_from_utf8((Bytes){ block.data, (usize)component_length }, &out_record->component);
    if (result != CORE_OK) {
        memory_free(&block);
        return result;
    }

    result = string_from_utf8((Bytes){ block.data + component_length, (usize)message_length },
                              &out_record->message);
    if (result != CORE_OK) {
        (void)string_destroy(&out_record->component);
        memory_free(&block);
        return result;
    }

    memory_free(&block);

    out_record->timestamp = log_read_u64(header + 4);
    out_record->process_id = (ProcessId)log_read_u32(header + 12);
    out_record->thread_id = (ThreadId)log_read_u32(header + 16);
    out_record->severity = (LogSeverity)header[20];

    *out_status = LOG_READ_STATUS_LOG_RECORD;
    return CORE_OK;
}

ErrorCode log_record_destroy(LogRecord *record)
{
    if (record == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    ErrorCode result = string_destroy(&record->component);
    ErrorCode message_result = string_destroy(&record->message);

    return result != CORE_OK ? result : message_result;
}

ErrorCode log_reader_close(LogReaderHandle **reader)
{
    if (reader == NULL || *reader == NULL) {
        return CORE_INVALID_ARGUMENT;
    }

    LogReaderHandle *target = *reader;
    ErrorCode result = CORE_OK;

    if (target->file != NULL) {
        result = file_close(&target->file);
    }

    MemoryBlock block;
    block.data = (u8 *)target;
    block.size = sizeof(LogReaderHandle);
    (void)memory_free(&block);

    *reader = NULL;
    return result;
}
