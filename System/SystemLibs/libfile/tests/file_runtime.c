/*
 * Runtime test of the libfile operations against DevDoc/SystemLibs/libfile.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 * Uses /tmp (present on the host and inside the PUNos initramfs) and
 * /dev/null (present via devtmpfs in both environments) as fixtures.
 */
#define CHECK_TEST_NAME "libfile"
#include "../../testing/check.h"

#include <sys/file.h>

#define TEST_FILE_A "/tmp/libfile_test_a"
#define TEST_FILE_B "/tmp/libfile_test_b"
#define TEST_DIR "/tmp/libfile_test_dir"

static String str(const char *text)
{
    String result;

    if (string_from_c_str(text, &result) != CORE_OK) {
        check_fail(250);
    }
    return result;
}

static void destroy(String *s)
{
    (void)string_destroy(s);
}

static void test_open_close_errors(void)
{
    String missing = str("/tmp/libfile_test_missing_xyz");
    FileHandle file = NULL;

    EXPECT(1, file_open(NULL, FILE_OPEN_MODE_READ, FILE_DEFAULT_PERMISSIONS, &file) ==
                  CORE_INVALID_ARGUMENT);
    EXPECT(2, file_open(&missing, FILE_OPEN_MODE_READ, FILE_DEFAULT_PERMISSIONS, NULL) ==
                  CORE_INVALID_ARGUMENT);
    EXPECT(3, file_open(&missing, (FileOpenMode)99, FILE_DEFAULT_PERMISSIONS, &file) ==
                  CORE_INVALID_ARGUMENT);
    EXPECT(4, file_open(&missing, FILE_OPEN_MODE_READ, (FilePermissions)0xffffffffu, &file) ==
                  CORE_INVALID_ARGUMENT);
    EXPECT(5, file_open(&missing, FILE_OPEN_MODE_READ, FILE_DEFAULT_PERMISSIONS, &file) ==
                  CORE_NOT_FOUND);
    EXPECT(6, file_close(NULL) == CORE_INVALID_ARGUMENT);
    {
        FileHandle already_null = NULL;

        EXPECT(7, file_close(&already_null) == CORE_INVALID_ARGUMENT);
    }
    destroy(&missing);
}

static void test_write_and_read(void)
{
    String path = str(TEST_FILE_A);
    FileHandle file = NULL;
    u8 payload[] = { 'h', 'e', 'l', 'l', 'o' };
    Bytes payload_bytes = { payload, sizeof(payload) };
    u8 read_buffer[16];
    Bytes read_bytes = { read_buffer, sizeof(read_buffer) };
    usize read_count = 0;

    EXPECT(10, file_open(&path, FILE_OPEN_MODE_WRITE_TRUNCATE, FILE_DEFAULT_PERMISSIONS,
                          &file) == CORE_OK);
    EXPECT(11, file_write_all(file, payload_bytes) == CORE_OK);
    EXPECT(12, file_close(&file) == CORE_OK);
    EXPECT(13, file == NULL);

    EXPECT(14, file_open(&path, FILE_OPEN_MODE_READ, FILE_DEFAULT_PERMISSIONS, &file) ==
                   CORE_OK);
    EXPECT(15, file_read(file, read_bytes, &read_count) == CORE_OK);
    EXPECT(16, read_count == sizeof(payload));
    {
        usize index;
        bool same = true;

        for (index = 0; index < sizeof(payload); ++index) {
            if (read_buffer[index] != payload[index]) {
                same = false;
            }
        }
        EXPECT(17, same);
    }
    /* End of file: another read returns 0. */
    EXPECT(18, file_read(file, read_bytes, &read_count) == CORE_OK);
    EXPECT(19, read_count == 0);
    /* Zero-size buffer needs no syscall and reports 0 bytes read. */
    {
        Bytes empty = { NULL, 0 };

        EXPECT(20, file_read(file, empty, &read_count) == CORE_OK && read_count == 0);
    }
    EXPECT(21, file_read(file, read_bytes, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(22, file_close(&file) == CORE_OK);
    destroy(&path);
}

static void test_seek(void)
{
    String path = str(TEST_FILE_A); /* still holds the 5 bytes from above */
    FileHandle file = NULL;
    u64 position = 0;

    EXPECT(30, file_open(&path, FILE_OPEN_MODE_READ, FILE_DEFAULT_PERMISSIONS, &file) ==
                   CORE_OK);
    EXPECT(31, file_seek(file, 2, FILE_SEEK_ORIGIN_START, &position) == CORE_OK &&
                   position == 2);
    EXPECT(32, file_seek(file, 1, FILE_SEEK_ORIGIN_CURRENT, &position) == CORE_OK &&
                   position == 3);
    EXPECT(33, file_seek(file, 0, FILE_SEEK_ORIGIN_END, &position) == CORE_OK &&
                   position == 5);
    EXPECT(34, file_seek(file, -1, FILE_SEEK_ORIGIN_START, &position) ==
                   CORE_INVALID_ARGUMENT);
    EXPECT(35, file_seek(file, 0, (FileSeekOrigin)99, &position) == CORE_INVALID_ARGUMENT);
    EXPECT(36, file_seek(NULL, 0, FILE_SEEK_ORIGIN_START, &position) ==
                   CORE_INVALID_ARGUMENT);
    EXPECT(37, file_close(&file) == CORE_OK);
    destroy(&path);
}

static void test_info(void)
{
    String path = str(TEST_FILE_A);
    String device_path = str("/dev/null");
    FileHandle file = NULL;
    FileInfo info;

    EXPECT(40, file_open(&path, FILE_OPEN_MODE_READ, FILE_DEFAULT_PERMISSIONS, &file) ==
                   CORE_OK);
    EXPECT(41, file_info(file, &info) == CORE_OK);
    EXPECT(42, info.size == 5);
    EXPECT(43, info.kind == FILE_KIND_REGULAR);
    EXPECT(44, file_info(NULL, &info) == CORE_INVALID_ARGUMENT);
    EXPECT(45, file_info(file, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(46, file_close(&file) == CORE_OK);

    EXPECT(47, file_open(&device_path, FILE_OPEN_MODE_READ, FILE_DEFAULT_PERMISSIONS,
                          &file) == CORE_OK);
    EXPECT(48, file_info(file, &info) == CORE_OK);
    EXPECT(49, info.kind == FILE_KIND_DEVICE);
    EXPECT(50, file_close(&file) == CORE_OK);

    destroy(&path);
    destroy(&device_path);
}

static void test_flush(void)
{
    String path = str(TEST_FILE_A);
    FileHandle file = NULL;

    EXPECT(55, file_open(&path, FILE_OPEN_MODE_READ_WRITE_CREATE, FILE_DEFAULT_PERMISSIONS,
                          &file) == CORE_OK);
    EXPECT(56, file_flush(file) == CORE_OK);
    EXPECT(57, file_flush(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(58, file_close(&file) == CORE_OK);
    destroy(&path);
}

static void test_append(void)
{
    String path = str(TEST_FILE_A);
    FileHandle file = NULL;
    u8 more[] = { '!' };
    Bytes more_bytes = { more, sizeof(more) };
    u8 read_buffer[16];
    Bytes read_bytes = { read_buffer, sizeof(read_buffer) };
    usize read_count = 0;

    EXPECT(60, file_open(&path, FILE_OPEN_MODE_APPEND_CREATE, FILE_DEFAULT_PERMISSIONS,
                          &file) == CORE_OK);
    EXPECT(61, file_write_all(file, more_bytes) == CORE_OK);
    EXPECT(62, file_close(&file) == CORE_OK);

    EXPECT(63, file_open(&path, FILE_OPEN_MODE_READ, FILE_DEFAULT_PERMISSIONS, &file) ==
                   CORE_OK);
    EXPECT(64, file_read(file, read_bytes, &read_count) == CORE_OK);
    EXPECT(65, read_count == 6); /* "hello" + "!" */
    EXPECT(66, read_buffer[5] == '!');
    EXPECT(67, file_close(&file) == CORE_OK);
    destroy(&path);
}

static void test_directory_create(void)
{
    String dir_path = str(TEST_DIR);
    ErrorCode first;

    first = directory_create(&dir_path, DIRECTORY_DEFAULT_PERMISSIONS);
    /* Repeated test runs on the same host reuse the same directory. */
    EXPECT(70, first == CORE_OK || first == CORE_ALREADY_EXISTS);
    EXPECT(71, directory_create(&dir_path, DIRECTORY_DEFAULT_PERMISSIONS) ==
                   CORE_ALREADY_EXISTS);
    EXPECT(72, directory_create(NULL, DIRECTORY_DEFAULT_PERMISSIONS) ==
                   CORE_INVALID_ARGUMENT);
    EXPECT(73, directory_create(&dir_path, (FilePermissions)0xffffffffu) ==
                   CORE_INVALID_ARGUMENT);
    {
        FileHandle file = NULL;
        FileInfo info;

        EXPECT(74, file_open(&dir_path, FILE_OPEN_MODE_READ, FILE_DEFAULT_PERMISSIONS,
                              &file) == CORE_OK);
        EXPECT(75, file_info(file, &info) == CORE_OK && info.kind == FILE_KIND_DIRECTORY);
        EXPECT(76, file_close(&file) == CORE_OK);
    }
    destroy(&dir_path);
}

static void test_remove_and_rename(void)
{
    String path_a = str(TEST_FILE_A);
    String path_b = str(TEST_FILE_B);
    FileHandle file = NULL;

    /* path_a still holds "hello!" (6 bytes) from test_append(). */
    EXPECT(80, file_rename(&path_a, &path_b) == CORE_OK);
    EXPECT(81, file_open(&path_a, FILE_OPEN_MODE_READ, FILE_DEFAULT_PERMISSIONS, &file) ==
                   CORE_NOT_FOUND);
    EXPECT(82, file_open(&path_b, FILE_OPEN_MODE_READ, FILE_DEFAULT_PERMISSIONS, &file) ==
                   CORE_OK);
    EXPECT(83, file_close(&file) == CORE_OK);

    EXPECT(84, file_remove(&path_b) == CORE_OK);
    EXPECT(85, file_open(&path_b, FILE_OPEN_MODE_READ, FILE_DEFAULT_PERMISSIONS, &file) ==
                   CORE_NOT_FOUND);
    EXPECT(86, file_remove(&path_b) == CORE_NOT_FOUND);
    EXPECT(87, file_remove(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(88, file_rename(NULL, &path_b) == CORE_INVALID_ARGUMENT);
    EXPECT(89, file_rename(&path_a, NULL) == CORE_INVALID_ARGUMENT);

    destroy(&path_a);
    destroy(&path_b);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    test_open_close_errors();
    test_write_and_read();
    test_seek();
    test_info();
    test_flush();
    test_append();
    test_directory_create();
    test_remove_and_rename();

    check_passed();
    return 0;
}
