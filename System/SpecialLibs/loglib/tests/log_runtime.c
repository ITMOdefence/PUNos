/*
 * Runtime test of the loglib operations against DevDoc/SpecialLibs/loglib.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 * Log files live under /tmp, present on the host and inside the PUNos
 * initramfs. Every test removes its files first, because log_store_open()
 * always appends and a leftover frame would change the expected counts.
 *
 * Check number groups:
 *   1-8    log_store_open / log_store_close
 *   10-27  append one record, read it back, then reach end of file
 *   30-41  five records with different severities, in order
 *   50-65  rotate: records land in the file that is current at the time
 *   70-83  invalid arguments, missing files, NULL handles
 */

#define CHECK_TEST_NAME "loglib"
#include <check.h>

#include <punos/syscall/syscall.h>
#include <punos/syscall/constants.h>
#include <special/log.h>
#include <sys/file.h>
#include <sys/memory.h>
#include <sys/process.h>
#include <sys/string.h>
#include <sys/thread.h>
#include <sys/time.h>

#define TEST_LOG "/tmp/loglib_test.log"
#define TEST_LOG_2 "/tmp/loglib_test2.log"
#define TEST_CORRUPT_LOG "/tmp/loglib_corrupt.log"

/* Writes a hand-built frame so a reader can be tested against a damaged file.
 * payload_extra bytes are appended after the header to make the frame look
 * complete when it is not. */
static ErrorCode write_raw_frame(const String *path, const u8 *header, usize header_size,
                                 usize payload_extra)
{
    FileHandle file = NULL;
    ErrorCode result = file_open(path, FILE_OPEN_MODE_WRITE_TRUNCATE, 0644, &file);

    if (result != CORE_OK) {
        return result;
    }
    result = file_write_all(file, (Bytes){ (u8 *)header, header_size });
    if (result == CORE_OK && payload_extra > 0) {
        u8 filler[64];
        usize written = 0;

        for (usize i = 0; i < sizeof(filler); ++i) {
            filler[i] = (u8)'x';
        }
        while (written < payload_extra) {
            usize chunk = payload_extra - written;

            if (chunk > sizeof(filler)) {
                chunk = sizeof(filler);
            }
            result = file_write_all(file, (Bytes){ filler, chunk });
            if (result != CORE_OK) {
                break;
            }
            written += chunk;
        }
    }
    (void)file_close(&file);
    return result;
}

static String make_str(const char *text)
{
    String s;
    if (string_from_c_str(text, &s) != CORE_OK) {
        check_fail(250);
    }
    return s;
}

static void destroy_str(String *s)
{
    (void)string_destroy(s);
}

/* log_store_open() always appends, so a file left by an earlier run would
 * make the record counts below wrong. Every test starts from an empty file. */
static void remove_if_present(const String *path)
{
    ErrorCode result = file_remove(path);

    if (result != CORE_OK && result != CORE_NOT_FOUND) {
        check_fail(249);
    }
}

static void test_store_open_close(void)
{
    String path = make_str(TEST_LOG);
    LogStoreHandle store = NULL;
    LogStoreConfig config;

    config.path = &path;
    config.permissions = 0644;

    EXPECT(1, log_store_open(NULL, &store) == CORE_INVALID_ARGUMENT);
    EXPECT(2, log_store_open(&config, NULL) == CORE_INVALID_ARGUMENT);

    LogStoreConfig bad_config;
    bad_config.path = NULL;
    bad_config.permissions = 0644;
    EXPECT(3, log_store_open(&bad_config, &store) == CORE_INVALID_ARGUMENT);

    EXPECT(4, log_store_open(&config, &store) == CORE_OK && store != NULL);
    EXPECT(5, log_store_close(NULL) == CORE_INVALID_ARGUMENT);
    {
        LogStoreHandle null_store = NULL;
        EXPECT(6, log_store_close(&null_store) == CORE_INVALID_ARGUMENT);
    }
    EXPECT(7, log_store_close(&store) == CORE_OK);
    EXPECT(8, store == NULL);

    destroy_str(&path);
}

static void test_append_and_read(void)
{
    String path = make_str(TEST_LOG);
    LogStoreHandle store = NULL;
    LogStoreConfig config;

    remove_if_present(&path);

    config.path = &path;
    config.permissions = 0644;

    EXPECT(10, log_store_open(&config, &store) == CORE_OK);

    String component = make_str("test_component");
    String message = make_str("Hello, World!");

    LogRecordInput record;
    record.timestamp = 1234567890;
    record.process_id = 42;
    record.thread_id = 7;
    record.severity = LOG_SEVERITY_INFO;
    record.component = &component;
    record.message = &message;

    EXPECT(11, log_store_append(store, &record) == CORE_OK);
    EXPECT(12, log_store_flush(store) == CORE_OK);
    EXPECT(13, log_store_close(&store) == CORE_OK);

    LogReaderHandle reader = NULL;
    EXPECT(14, log_reader_open(&path, &reader) == CORE_OK);

    LogRecord read_record;
    LogReadStatus status;

    EXPECT(15, log_reader_next(reader, &status, &read_record) == CORE_OK);
    EXPECT(16, status == LOG_READ_STATUS_LOG_RECORD);
    EXPECT(17, read_record.timestamp == 1234567890);
    EXPECT(18, read_record.process_id == 42);
    EXPECT(19, read_record.thread_id == 7);
    EXPECT(20, read_record.severity == LOG_SEVERITY_INFO);
    EXPECT(21, read_record.component.length == 14);
    EXPECT(22, read_record.message.length == 13);

    EXPECT(23, log_record_destroy(&read_record) == CORE_OK);

    EXPECT(24, log_reader_next(reader, &status, &read_record) == CORE_OK);
    EXPECT(25, status == LOG_READ_STATUS_END_OF_FILE);

    EXPECT(26, log_reader_close(&reader) == CORE_OK);
    EXPECT(27, reader == NULL);

    destroy_str(&path);
    destroy_str(&component);
    destroy_str(&message);
}

static void test_multiple_records(void)
{
    String path = make_str(TEST_LOG_2);
    LogStoreHandle store = NULL;
    LogStoreConfig config;

    remove_if_present(&path);

    config.path = &path;
    config.permissions = 0644;

    EXPECT(30, log_store_open(&config, &store) == CORE_OK);

    for (int i = 0; i < 5; i++) {
        String component = make_str("comp");
        String message = make_str("msg");

        LogRecordInput record;
        record.timestamp = (WallClockTime)i;
        record.process_id = 1;
        record.thread_id = 1;
        record.severity = (LogSeverity)(i % 6);
        record.component = &component;
        record.message = &message;

        EXPECT(31, log_store_append(store, &record) == CORE_OK);
        destroy_str(&component);
        destroy_str(&message);
    }

    EXPECT(32, log_store_flush(store) == CORE_OK);
    EXPECT(33, log_store_close(&store) == CORE_OK);

    LogReaderHandle reader = NULL;
    EXPECT(34, log_reader_open(&path, &reader) == CORE_OK);

    for (int i = 0; i < 5; i++) {
        LogRecord read_record;
        LogReadStatus status;

        EXPECT(35, log_reader_next(reader, &status, &read_record) == CORE_OK);
        EXPECT(36, status == LOG_READ_STATUS_LOG_RECORD);
        EXPECT(37, read_record.timestamp == (WallClockTime)i);
        EXPECT(38, read_record.severity == (LogSeverity)(i % 6));

        log_record_destroy(&read_record);
    }

    LogRecord read_record;
    LogReadStatus status;
    EXPECT(39, log_reader_next(reader, &status, &read_record) == CORE_OK);
    EXPECT(40, status == LOG_READ_STATUS_END_OF_FILE);

    EXPECT(41, log_reader_close(&reader) == CORE_OK);
    destroy_str(&path);
}

static void test_rotate(void)
{
    String path1 = make_str(TEST_LOG);
    String path2 = make_str(TEST_LOG_2);
    LogStoreHandle store = NULL;
    LogStoreConfig config1, config2;

    remove_if_present(&path1);
    remove_if_present(&path2);

    config1.path = &path1;
    config1.permissions = 0644;
    config2.path = &path2;
    config2.permissions = 0644;

    EXPECT(50, log_store_open(&config1, &store) == CORE_OK);

    String component = make_str("comp");
    String message = make_str("before rotate");

    LogRecordInput record;
    record.timestamp = 1;
    record.process_id = 1;
    record.thread_id = 1;
    record.severity = LOG_SEVERITY_INFO;
    record.component = &component;
    record.message = &message;

    EXPECT(51, log_store_append(store, &record) == CORE_OK);
    EXPECT(52, log_store_rotate(store, &config2) == CORE_OK);

    String message2 = make_str("after rotate");
    record.message = &message2;
    EXPECT(53, log_store_append(store, &record) == CORE_OK);
    EXPECT(54, log_store_flush(store) == CORE_OK);
    EXPECT(55, log_store_close(&store) == CORE_OK);

    LogReaderHandle reader1 = NULL;
    EXPECT(56, log_reader_open(&path1, &reader1) == CORE_OK);
    LogRecord rec1;
    LogReadStatus status1;
    EXPECT(57, log_reader_next(reader1, &status1, &rec1) == CORE_OK);
    EXPECT(58, status1 == LOG_READ_STATUS_LOG_RECORD);
    EXPECT(59, log_record_destroy(&rec1) == CORE_OK);
    EXPECT(60, log_reader_close(&reader1) == CORE_OK);

    LogReaderHandle reader2 = NULL;
    EXPECT(61, log_reader_open(&path2, &reader2) == CORE_OK);
    LogRecord rec2;
    LogReadStatus status2;
    EXPECT(62, log_reader_next(reader2, &status2, &rec2) == CORE_OK);
    EXPECT(63, status2 == LOG_READ_STATUS_LOG_RECORD);
    EXPECT(64, log_record_destroy(&rec2) == CORE_OK);
    EXPECT(65, log_reader_close(&reader2) == CORE_OK);

    destroy_str(&path1);
    destroy_str(&path2);
    destroy_str(&component);
    destroy_str(&message);
    destroy_str(&message2);
}

static void test_error_cases(void)
{
    String path = make_str(TEST_LOG);
    LogStoreHandle store = NULL;
    LogStoreConfig config;

    config.path = &path;
    config.permissions = 0644;

    EXPECT(70, log_store_open(&config, &store) == CORE_OK);

    String component = make_str("comp");
    String message = make_str("msg");

    LogRecordInput record;
    record.timestamp = 1;
    record.process_id = 1;
    record.thread_id = 1;
    record.severity = LOG_SEVERITY_INFO;
    record.component = &component;
    record.message = &message;

    EXPECT(71, log_store_append(NULL, &record) == CORE_INVALID_ARGUMENT);
    EXPECT(72, log_store_append(store, NULL) == CORE_INVALID_ARGUMENT);

    LogRecordInput bad_record = record;
    bad_record.severity = (LogSeverity)99;
    EXPECT(73, log_store_append(store, &bad_record) == CORE_INVALID_ARGUMENT);

    EXPECT(74, log_store_flush(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(75, log_store_rotate(NULL, &config) == CORE_INVALID_ARGUMENT);
    EXPECT(76, log_store_rotate(store, NULL) == CORE_INVALID_ARGUMENT);

    EXPECT(77, log_store_close(&store) == CORE_OK);

    LogReaderHandle reader = NULL;
    EXPECT(78, log_reader_open(NULL, &reader) == CORE_INVALID_ARGUMENT);
    EXPECT(79, log_reader_open(&path, NULL) == CORE_INVALID_ARGUMENT);

    String missing = make_str("/tmp/loglib_nonexistent.log");
    EXPECT(80, log_reader_open(&missing, &reader) == CORE_NOT_FOUND);
    destroy_str(&missing);

    EXPECT(81, log_record_destroy(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(82, log_reader_close(NULL) == CORE_INVALID_ARGUMENT);
    {
        LogReaderHandle null_reader = NULL;
        EXPECT(83, log_reader_close(&null_reader) == CORE_INVALID_ARGUMENT);
    }

    destroy_str(&path);
    destroy_str(&component);
    destroy_str(&message);
}

/* A header whose frame_size disagrees with component_length + message_length. */
static const u8 *corrupt_sum_header(void)
{
    static u8 header[LOG_FRAME_HEADER_SIZE];
    usize index;

    for (index = 0; index < sizeof(header); ++index) {
        header[index] = 0;
    }
    header[0] = 40; /* frame_size */
    header[21] = 10; /* component_length */
    header[23] = 3;  /* message_length */
    return header;
}

/* Builds a frame whose declared sizes add up exactly, so the only thing that
 * can reject it is the rule under test. An earlier version of these checks let
 * frame_size disagree with the two lengths, which the pre-existing consistency
 * check rejected first, so they never reached the new validation at all. */
static ErrorCode write_consistent_frame(const String *path, usize component_length,
                                        usize message_length, u8 severity)
{
    u8 header[LOG_FRAME_HEADER_SIZE];
    u32 frame_size = (u32)(LOG_FRAME_HEADER_SIZE + component_length + message_length);
    usize index;

    for (index = 0; index < sizeof(header); ++index) {
        header[index] = 0;
    }
    header[0] = (u8)(frame_size & 0xffu);
    header[1] = (u8)((frame_size >> 8) & 0xffu);
    header[2] = (u8)((frame_size >> 16) & 0xffu);
    header[3] = (u8)((frame_size >> 24) & 0xffu);
    header[12] = 7;   /* process_id */
    header[16] = 9;   /* thread_id  */
    header[20] = severity;
    header[21] = (u8)(component_length & 0xffu);
    header[22] = (u8)((component_length >> 8) & 0xffu);
    header[23] = (u8)(message_length & 0xffu);
    header[24] = (u8)((message_length >> 8) & 0xffu);
    header[25] = (u8)((message_length >> 16) & 0xffu);
    header[26] = (u8)((message_length >> 24) & 0xffu);

    return write_raw_frame(path, header, sizeof(header), component_length + message_length);
}

/* Reads one frame and reports whether it was refused as damaged. */
static bool reader_rejects(const String *path)
{
    LogReaderHandle reader = NULL;
    LogReadStatus status;
    LogRecord record;
    bool rejected;

    if (log_reader_open(path, &reader) != CORE_OK) {
        return false;
    }
    rejected = log_reader_next(reader, &status, &record) == CORE_IO_ERROR;
    if (!rejected) {
        (void)log_record_destroy(&record);
    }
    (void)log_reader_close(&reader);
    return rejected;
}

/*
 * Issue 9: a reader has to reject a damaged frame instead of accepting it as a
 * valid record. Each case below is a frame that adds up perfectly and is
 * refused for exactly one reason.
 */
static void test_corrupt_frames(void)
{
    String path = make_str(TEST_CORRUPT_LOG);

    /* Control: a frame that is within every limit has to be accepted. If this
     * failed, a rejection below would prove nothing. */
    EXPECT(80, write_consistent_frame(&path, 4, 5, (u8)LOG_SEVERITY_INFO) == CORE_OK);
    EXPECT(81, !reader_rejects(&path));

    /* component_length one byte over LOG_COMPONENT_MAX_LENGTH. */
    EXPECT(82, write_consistent_frame(&path, LOG_COMPONENT_MAX_LENGTH + 1, 0,
                                      (u8)LOG_SEVERITY_INFO) == CORE_OK);
    EXPECT(83, reader_rejects(&path));

    /* message_length one byte over LOG_MESSAGE_MAX_LENGTH. */
    EXPECT(84, write_consistent_frame(&path, 0, LOG_MESSAGE_MAX_LENGTH + 1,
                                      (u8)LOG_SEVERITY_INFO) == CORE_OK);
    EXPECT(85, reader_rejects(&path));

    /* A severity byte above LOG_SEVERITY_FATAL. */
    EXPECT(86, write_consistent_frame(&path, 1, 1, (u8)(LOG_SEVERITY_FATAL + 1)) == CORE_OK);
    EXPECT(87, reader_rejects(&path));

    /* Both lengths legal, so only a corrupt length sum is left to catch it. */
    EXPECT(88, write_raw_frame(&path, corrupt_sum_header(), LOG_FRAME_HEADER_SIZE, 3) == CORE_OK);
    EXPECT(89, reader_rejects(&path));

    /* A frame_size below the header size. */
    {
        u8 header[LOG_FRAME_HEADER_SIZE];
        usize index;

        for (index = 0; index < sizeof(header); ++index) {
            header[index] = 0;
        }
        header[0] = 4;
        EXPECT(90, write_raw_frame(&path, header, sizeof(header), 0) == CORE_OK);
        EXPECT(91, reader_rejects(&path));
    }

    /* A frame the file ends in the middle of. */
    {
        u8 header[LOG_FRAME_HEADER_SIZE];
        usize index;

        for (index = 0; index < sizeof(header); ++index) {
            header[index] = 0;
        }
        header[0] = 64; /* claims 64 bytes, only 27 + 8 are written */
        EXPECT(92, write_raw_frame(&path, header, sizeof(header), 8) == CORE_OK);
        EXPECT(93, reader_rejects(&path));
    }

    destroy_str(&path);
    (void)unlink(TEST_CORRUPT_LOG);
}


/* Issue 13: an empty log path is not a path. */
static void test_path_validation(void)
{
    LogStoreHandle store = NULL;
    LogReaderHandle reader = NULL;
    LogStoreConfig config;

    String empty = make_str("");
    config.path = &empty;
    config.permissions = 0644;
    EXPECT(90, log_store_open(&config, &store) == CORE_INVALID_ARGUMENT);
    EXPECT(91, log_reader_open(&empty, &reader) == CORE_INVALID_ARGUMENT);

    LogStoreConfig null_path;
    null_path.path = NULL;
    null_path.permissions = 0644;
    EXPECT(92, log_store_open(&null_path, &store) == CORE_INVALID_ARGUMENT);

    destroy_str(&empty);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    test_store_open_close();
    test_append_and_read();
    test_multiple_records();
    test_rotate();
    test_error_cases();
    test_corrupt_frames();
    test_path_validation();

    check_passed();
    return 0;
}
