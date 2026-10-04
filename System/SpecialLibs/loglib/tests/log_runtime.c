/* 1-14: append, wire format, read ownership; 15-25: rotation and limits;
 * 26-42: malformed/truncated frames, empty strings, errors and lifetime. */
#define CHECK_TEST_NAME "loglib"
#include "../../../SystemLibs/testing/check.h"
#include <special/log.h>

static String text(const char *value)
{
    String result;
    if (string_from_c_str(value, &result) != CORE_OK) check_fail(250);
    return result;
}
int main(void)
{
    char name[] = "/tmp/punos-log-0000000000";
    u32 pid = (u32)getpid();
    for (usize i = 0; i < 10; ++i) { name[23 - i] = (char)('0' + pid % 10); pid /= 10; }
    String path = text(name), second;
    String component = text("io"), message = text("hello"), empty = text("");
    LogStoreHandle store = NULL;
    LogReaderHandle reader = NULL;
    LogRecord record;
    LogReadStatus status;
    LogRecordInput input = {0x0102030405060708ULL, -7, 19, LOG_SEVERITY_WARNING, &component, &message};
    FileHandle file;
    u8 bytes[64];
    usize count;
    bool equal;
    (void)file_remove(&path);
    EXPECT(1, log_store_open(NULL, &store) == CORE_INVALID_ARGUMENT);
    EXPECT(2, log_store_open(&(LogStoreConfig){&path, 0600}, &store) == CORE_OK);
    EXPECT(3, log_store_append(store, &input) == CORE_OK);
    EXPECT(4, log_store_flush(store) == CORE_OK);
    EXPECT(5, file_open(&path, FILE_OPEN_MODE_READ, 0, &file) == CORE_OK);
    EXPECT(6, file_read(file, (Bytes){bytes, sizeof(bytes)}, &count) == CORE_OK && count == 34);
    EXPECT(7, bytes[0] == 34 && bytes[1] == 0 && bytes[4] == 8 && bytes[11] == 1 && bytes[12] == 249 && bytes[15] == 255 && bytes[16] == 19 && bytes[20] == 3 && bytes[21] == 2 && bytes[23] == 5 && bytes[27] == 'i' && bytes[33] == 'o');
    EXPECT(8, file_close(&file) == CORE_OK && log_reader_open(&path, &reader) == CORE_OK);
    EXPECT(9, log_reader_next(reader, &status, &record) == CORE_OK && status == LOG_READ_STATUS_LOG_RECORD);
    EXPECT(10, record.timestamp == input.timestamp && record.process_id == -7 && record.thread_id == 19 && record.severity == LOG_SEVERITY_WARNING);
    EXPECT(11, string_equals(&record.component, &component, &equal) == CORE_OK && equal);
    EXPECT(12, string_equals(&record.message, &message, &equal) == CORE_OK && equal);
    EXPECT(13, log_record_destroy(&record) == CORE_OK && record.component.data == NULL && record.message.data == NULL);
    EXPECT(14, log_reader_next(reader, &status, &record) == CORE_OK && status == LOG_READ_STATUS_END_OF_FILE && log_reader_close(&reader) == CORE_OK);
    second = text(".rotated");
    String rotated;
    EXPECT(15, string_concat(&path, &second, &rotated) == CORE_OK);
    (void)file_remove(&rotated);
    EXPECT(16, log_store_rotate(store, &(LogStoreConfig){&rotated, 0600}) == CORE_OK);
    EXPECT(17, log_store_append(store, &input) == CORE_OK);
    String missing = text("/punos-does-not-exist/log");
    EXPECT(18, log_store_rotate(store, &(LogStoreConfig){&missing, 0600}) == CORE_NOT_FOUND);
    EXPECT(19, log_store_append(store, &input) == CORE_OK);
    input.severity = (LogSeverity)6;
    EXPECT(20, log_store_append(store, &input) == CORE_INVALID_ARGUMENT);
    input.severity = LOG_SEVERITY_INFO;
    String oversized = {(char *)bytes, LOG_COMPONENT_MAX_LENGTH + 1};
    input.component = &oversized;
    EXPECT(21, log_store_append(store, &input) == CORE_OVERFLOW);
    char invalid_utf8[] = {(char)0xc0, (char)0x80, 0};
    String invalid = {invalid_utf8, 2};
    input.component = &invalid;
    EXPECT(22, log_store_append(store, &input) == CORE_INVALID_ARGUMENT);
    EXPECT(23, log_store_close(&store) == CORE_OK && store == NULL);
    EXPECT(24, log_reader_open(&rotated, &reader) == CORE_OK);
    for (usize i = 0; i < 2; ++i) {
        EXPECT(25, log_reader_next(reader, &status, &record) == CORE_OK && status == LOG_READ_STATUS_LOG_RECORD);
        (void)log_record_destroy(&record);
    }
    EXPECT(26, log_reader_next(reader, &status, &record) == CORE_OK && status == LOG_READ_STATUS_END_OF_FILE && log_reader_close(&reader) == CORE_OK);
    EXPECT(27, file_open(&path, FILE_OPEN_MODE_WRITE_TRUNCATE, 0600, &file) == CORE_OK && file_write_all(file, (Bytes){bytes, 5}) == CORE_OK && file_close(&file) == CORE_OK && log_reader_open(&path, &reader) == CORE_OK);
    EXPECT(28, log_reader_next(reader, &status, &record) == CORE_IO_ERROR && log_reader_close(&reader) == CORE_OK);
    bytes[20] = 6;
    EXPECT(29, file_open(&path, FILE_OPEN_MODE_WRITE_TRUNCATE, 0600, &file) == CORE_OK && file_write_all(file, (Bytes){bytes, 34}) == CORE_OK && file_close(&file) == CORE_OK && log_reader_open(&path, &reader) == CORE_OK);
    EXPECT(30, log_reader_next(reader, &status, &record) == CORE_IO_ERROR && log_reader_close(&reader) == CORE_OK);
    (void)file_remove(&path);
    input.component = input.message = &empty;
    EXPECT(31, log_store_open(&(LogStoreConfig){&path, 0600}, &store) == CORE_OK && log_store_append(store, &input) == CORE_OK && log_store_close(&store) == CORE_OK && log_reader_open(&path, &reader) == CORE_OK);
    EXPECT(32, log_reader_next(reader, &status, &record) == CORE_OK && record.component.length == 0 && record.message.length == 0 && log_record_destroy(&record) == CORE_OK);
    EXPECT(33, log_reader_close(&reader) == CORE_OK && log_store_close(&store) == CORE_INVALID_STATE && log_reader_next(reader, &status, &record) == CORE_INVALID_STATE);
    EXPECT(34, file_remove(&path) == CORE_OK && file_remove(&rotated) == CORE_OK);
    EXPECT(35, log_reader_open(&path, &reader) == CORE_NOT_FOUND);
    EXPECT(36, log_store_open(&(LogStoreConfig){&path, 01000}, &store) == CORE_INVALID_ARGUMENT);
    bytes[20] = 2;
    EXPECT(37, file_open(&path, FILE_OPEN_MODE_WRITE_TRUNCATE, 0600, &file) == CORE_OK && file_write_all(file, (Bytes){bytes, 30}) == CORE_OK && file_close(&file) == CORE_OK && log_reader_open(&path, &reader) == CORE_OK);
    EXPECT(38, log_reader_next(reader, &status, &record) == CORE_IO_ERROR && log_reader_close(&reader) == CORE_OK);
    bytes[27] = 0; /* NUL is forbidden in component even if lengths are valid. */
    EXPECT(39, file_open(&path, FILE_OPEN_MODE_WRITE_TRUNCATE, 0600, &file) == CORE_OK && file_write_all(file, (Bytes){bytes, 34}) == CORE_OK && file_close(&file) == CORE_OK && log_reader_open(&path, &reader) == CORE_OK);
    EXPECT(40, log_reader_next(reader, &status, &record) == CORE_IO_ERROR && log_reader_close(&reader) == CORE_OK);
    bytes[0] = 33; /* Inconsistent total size. */
    EXPECT(41, file_open(&path, FILE_OPEN_MODE_WRITE_TRUNCATE, 0600, &file) == CORE_OK && file_write_all(file, (Bytes){bytes, 34}) == CORE_OK && file_close(&file) == CORE_OK && log_reader_open(&path, &reader) == CORE_OK);
    EXPECT(42, log_reader_next(reader, &status, &record) == CORE_IO_ERROR && log_reader_close(&reader) == CORE_OK && file_remove(&path) == CORE_OK);
    (void)string_destroy(&path); (void)string_destroy(&rotated); (void)string_destroy(&second);
    (void)string_destroy(&component); (void)string_destroy(&message); (void)string_destroy(&empty); (void)string_destroy(&missing);
    check_passed(); return 0;
}
