/*
 * Runtime test of the appserv wire format: libappframework_internal_apps_encode()
 * and libappframework_internal_apps_decode() on exact bytes, with no sockets.
 *
 * Check groups:
 *   1-12   requests: bytes, message types
 *   13-45  replies that must be accepted, field by field
 *   46-95  replies that must be refused
 *   96-110 every truncation and every single-byte change of a valid list
 */

#define CHECK_TEST_NAME "appframework-apps-wire"
#include <check.h>

#include "support.h"

static void put16(u8 *out, u16 value)
{
    out[0] = (u8)(value & 0xff);
    out[1] = (u8)(value >> 8);
}

static void put32(u8 *out, u32 value)
{
    for (u32 i = 0; i < 4; ++i) {
        out[i] = (u8)((value >> (8 * i)) & 0xff);
    }
}

static bool bytes_are(const MemoryBlock *block, const u8 *expected, usize size)
{
    if (block->size < size) {
        return false;
    }
    for (usize i = 0; i < size; ++i) {
        if (block->data[i] != expected[i]) {
            return false;
        }
    }
    return true;
}

static bool text_is(const char *actual, const char *expected)
{
    usize i = 0;

    for (; actual[i] != '\0' && expected[i] != '\0'; ++i) {
        if (actual[i] != expected[i]) {
            return false;
        }
    }
    return actual[i] == expected[i];
}

/* ---- requests -------------------------------------------------------------- */

static void encoding(void)
{
    MemoryBlock payload = { NULL, 0 };
    IpcMessageType type = 99;
    static const u8 id_request[] = { 1, 0, 3, 0, 'a', 'b', 'c' };
    static const u8 instance_request[] = { 1, 0, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 };

    EXPECT(1, libappframework_internal_apps_encode(APP_APPS_OPERATION_LIST, NULL, 0, 0, &payload,
                                                   &type) == CORE_OK);
    EXPECT(2, type == 1 && payload.size == 0);
    EXPECT(3, libappframework_internal_apps_encode(APP_APPS_OPERATION_GET_INFO, "abc", 3, 0,
                                                   &payload, &type) == CORE_OK);
    EXPECT(4, type == 3 && payload.size == 7 && bytes_are(&payload, id_request, 7));
    (void)memory_free(&payload);
    EXPECT(5, libappframework_internal_apps_encode(APP_APPS_OPERATION_LAUNCH, "abc", 3, 0,
                                                   &payload, &type) == CORE_OK);
    EXPECT(6, type == 5 && payload.size == 7 && bytes_are(&payload, id_request, 7));
    (void)memory_free(&payload);
    EXPECT(7, libappframework_internal_apps_encode(APP_APPS_OPERATION_STOP, NULL, 0,
                                                   0x0102030405060708ULL, &payload,
                                                   &type) == CORE_OK);
    EXPECT(8, type == 7 && payload.size == 10 && bytes_are(&payload, instance_request, 10));
    (void)memory_free(&payload);
    EXPECT(9, libappframework_internal_apps_encode(APP_APPS_OPERATION_GET_STATUS, NULL, 0,
                                                   0x0102030405060708ULL, &payload,
                                                   &type) == CORE_OK);
    EXPECT(10, type == 9 && payload.size == 10 && bytes_are(&payload, instance_request, 10));
    (void)memory_free(&payload);
    EXPECT(11, libappframework_internal_apps_encode((AppAppsOperation)5, NULL, 0, 0, &payload,
                                                    &type) == CORE_INVALID_ARGUMENT);
    EXPECT(12, libappframework_internal_apps_encode((AppAppsOperation)-1, NULL, 0, 0, &payload,
                                                    &type) == CORE_INVALID_ARGUMENT);
}

/* ---- helpers for replies ---------------------------------------------------------- */

/* Appends an AppInfo (two length-prefixed texts) at out[*offset]. */
static void add_info(u8 *out, usize *offset, const char *id, usize id_length, const char *name,
                     usize name_length)
{
    put16(out + *offset, (u16)id_length);
    *offset += 2;
    for (usize i = 0; i < id_length; ++i) {
        out[*offset + i] = (u8)id[i];
    }
    *offset += id_length;
    put16(out + *offset, (u16)name_length);
    *offset += 2;
    for (usize i = 0; i < name_length; ++i) {
        out[*offset + i] = (u8)name[i];
    }
    *offset += name_length;
}

static ErrorCode decode(AppAppsOperation operation, const u8 *data, usize size, AppDecoded *out)
{
    return libappframework_internal_apps_decode(operation, data, size, out);
}

#define ACCEPTS(operation, data, size)                                          \
    (decode((operation), (data), (size), &decoded) == CORE_OK &&               \
     (cleanup = true))

static void accepted_replies(void)
{
    AppDecoded decoded;
    bool cleanup = false;
    static u8 list[9000];
    usize size = 0;
    u8 launch[12] = { 0 };
    u8 status[10] = { 0 };
    u8 stop[4] = { 0 };

    /* list: two entries, one with an empty display name */
    put32(list, 0);
    put16(list + 4, 2);
    size = 6;
    add_info(list, &size, "clock", 5, "Clock", 5);
    add_info(list, &size, "x", 1, "", 0);
    EXPECT(13, ACCEPTS(APP_APPS_OPERATION_LIST, list, size));
    EXPECT(14, decoded.service_error == CORE_OK && decoded.result.list.count == 2);
    EXPECT(15, text_is(decoded.result.list.items[0].application_id, "clock"));
    EXPECT(16, text_is(decoded.result.list.items[0].display_name, "Clock"));
    EXPECT(17, text_is(decoded.result.list.items[1].application_id, "x"));
    EXPECT(18, text_is(decoded.result.list.items[1].display_name, ""));
    libappframework_internal_apps_decoded_release(&decoded);

    /* list: empty */
    put16(list + 4, 0);
    EXPECT(19, ACCEPTS(APP_APPS_OPERATION_LIST, list, 6) && decoded.result.list.count == 0);
    libappframework_internal_apps_decoded_release(&decoded);

    /* list: 64 entries with 64-byte texts */
    {
        char longest[65];

        for (usize i = 0; i < 64; ++i) {
            longest[i] = 'z';
        }
        longest[64] = '\0';
        put16(list + 4, 64);
        size = 6;
        for (u32 i = 0; i < 64; ++i) {
            add_info(list, &size, longest, 64, longest, 64);
        }
        EXPECT(20, size < sizeof(list));
        EXPECT(21, ACCEPTS(APP_APPS_OPERATION_LIST, list, size) &&
                       decoded.result.list.count == 64);
        EXPECT(22, text_is(decoded.result.list.items[63].display_name, longest));
        libappframework_internal_apps_decoded_release(&decoded);
    }

    /* get_info */
    put32(list, 0);
    size = 4;
    add_info(list, &size, "\xD0\xB0", 2, "\xE2\x82\xAC", 3);
    EXPECT(23, ACCEPTS(APP_APPS_OPERATION_GET_INFO, list, size));
    EXPECT(24, text_is(decoded.result.info.application_id, "\xD0\xB0"));
    EXPECT(25, text_is(decoded.result.info.display_name, "\xE2\x82\xAC"));
    libappframework_internal_apps_decoded_release(&decoded);

    /* launch */
    launch[4] = 0x2a;
    launch[11] = 0x80;
    EXPECT(26, ACCEPTS(APP_APPS_OPERATION_LAUNCH, launch, 12));
    EXPECT(27, decoded.result.instance == 0x800000000000002aULL);

    /* stop */
    EXPECT(28, ACCEPTS(APP_APPS_OPERATION_STOP, stop, 4) && decoded.service_error == CORE_OK);

    /* status: every state */
    status[4] = APP_INSTANCE_STATE_RUNNING;
    EXPECT(29, ACCEPTS(APP_APPS_OPERATION_GET_STATUS, status, 10));
    EXPECT(30, decoded.result.status.state == APP_INSTANCE_STATE_RUNNING &&
                   decoded.result.status.exit_value == 0);
    status[4] = APP_INSTANCE_STATE_TERMINATING;
    EXPECT(31, ACCEPTS(APP_APPS_OPERATION_GET_STATUS, status, 10));
    EXPECT(32, decoded.result.status.state == APP_INSTANCE_STATE_TERMINATING);
    status[4] = APP_INSTANCE_STATE_EXITED;
    status[5] = APP_EXIT_KIND_SIGNALED;
    put32(status + 6, (u32)-15);
    EXPECT(33, ACCEPTS(APP_APPS_OPERATION_GET_STATUS, status, 10));
    EXPECT(34, decoded.result.status.state == APP_INSTANCE_STATE_EXITED &&
                   decoded.result.status.exit_kind == APP_EXIT_KIND_SIGNALED &&
                   decoded.result.status.exit_value == -15);
    status[5] = APP_EXIT_KIND_EXITED;
    put32(status + 6, 7);
    EXPECT(35, ACCEPTS(APP_APPS_OPERATION_GET_STATUS, status, 10) &&
                   decoded.result.status.exit_kind == APP_EXIT_KIND_EXITED &&
                   decoded.result.status.exit_value == 7);
    /* the exit fields of a running instance are not interpreted */
    status[4] = APP_INSTANCE_STATE_RUNNING;
    status[5] = 9;
    put32(status + 6, 1234);
    EXPECT(36, ACCEPTS(APP_APPS_OPERATION_GET_STATUS, status, 10) &&
                   decoded.result.status.exit_kind == APP_EXIT_KIND_EXITED &&
                   decoded.result.status.exit_value == 0);

    /* an error reply: any code 1..13, any extra bytes, for every operation */
    for (u32 code = 1; code <= 13; ++code) {
        u8 error[9] = { 0 };

        put32(error, code);
        for (u32 op = 0; op < 5; ++op) {
            if (decode((AppAppsOperation)op, error, 4, &decoded) != CORE_OK ||
                decoded.service_error != (ErrorCode)code ||
                decode((AppAppsOperation)op, error, 9, &decoded) != CORE_OK ||
                decoded.service_error != (ErrorCode)code) {
                check_fail(37);
            }
        }
    }
    EXPECT(38, cleanup);
    EXPECT(39, true);
}

#undef ACCEPTS

/* ---- replies that must be refused -------------------------------------------------- */

static bool refused(AppAppsOperation operation, const u8 *data, usize size)
{
    AppDecoded decoded;
    ErrorCode result = decode(operation, data, size, &decoded);

    if (result == CORE_OK) {
        libappframework_internal_apps_decoded_release(&decoded);
        return false;
    }
    return result == CORE_IO_ERROR && decoded.storage.data == NULL;
}

static usize list_of(u8 *out, u16 count, const char *id, usize id_length, const char *name,
                     usize name_length)
{
    usize size = 6;

    put32(out, 0);
    put16(out + 4, count);
    for (u16 i = 0; i < count; ++i) {
        add_info(out, &size, id, id_length, name, name_length);
    }
    return size;
}

static void refused_replies(void)
{
    static u8 reply[9000];
    u8 launch[16] = { 0 };
    u8 status[12] = { 0 };
    u8 stop[8] = { 0 };
    char text65[66];
    usize size;

    for (usize i = 0; i < 65; ++i) {
        text65[i] = 'q';
    }
    text65[65] = '\0';

    /* the error code */
    reply[0] = 14; /* one past CORE_OVERFLOW */
    EXPECT(46, refused(APP_APPS_OPERATION_STOP, reply, 4));
    put32(reply, 0xffffffffu);
    EXPECT(47, refused(APP_APPS_OPERATION_LIST, reply, 4));
    EXPECT(48, refused(APP_APPS_OPERATION_STOP, reply, 0));
    EXPECT(49, refused(APP_APPS_OPERATION_STOP, reply, 3));
    reply[0] = 0;
    reply[1] = 0;
    reply[2] = 0;
    reply[3] = 0;
    EXPECT(50, refused(APP_APPS_OPERATION_STOP, reply, 3));

    /* list */
    size = list_of(reply, 1, "a", 1, "b", 1);
    EXPECT(51, refused(APP_APPS_OPERATION_LIST, reply, size - 1));   /* truncated */
    EXPECT(52, refused(APP_APPS_OPERATION_LIST, reply, size + 1));   /* trailing byte */
    EXPECT(53, refused(APP_APPS_OPERATION_LIST, reply, 5));          /* no count */
    size = list_of(reply, 65, "a", 1, "b", 1);
    EXPECT(54, refused(APP_APPS_OPERATION_LIST, reply, size));       /* more than 64 */
    size = list_of(reply, 2, "a", 1, "b", 1);
    put16(reply + 4, 3);
    EXPECT(55, refused(APP_APPS_OPERATION_LIST, reply, size));       /* count too large */
    put16(reply + 4, 1);
    EXPECT(56, refused(APP_APPS_OPERATION_LIST, reply, size));       /* count too small */
    size = list_of(reply, 1, "", 0, "b", 1);
    EXPECT(57, refused(APP_APPS_OPERATION_LIST, reply, size));       /* empty id */
    size = list_of(reply, 1, text65, 65, "b", 1);
    EXPECT(58, refused(APP_APPS_OPERATION_LIST, reply, size));       /* id too long */
    size = list_of(reply, 1, "a", 1, text65, 65);
    EXPECT(59, refused(APP_APPS_OPERATION_LIST, reply, size));       /* name too long */
    size = list_of(reply, 1, "\xC0\x80", 2, "b", 1);
    EXPECT(60, refused(APP_APPS_OPERATION_LIST, reply, size));       /* overlong id */
    size = list_of(reply, 1, "a", 1, "\xED\xA0\x80", 3);
    EXPECT(61, refused(APP_APPS_OPERATION_LIST, reply, size));       /* surrogate name */
    size = list_of(reply, 1, "a\0b", 3, "n", 1);
    EXPECT(62, refused(APP_APPS_OPERATION_LIST, reply, size));       /* NUL in id */
    size = list_of(reply, 1, "a", 1, "n\0", 2);
    EXPECT(63, refused(APP_APPS_OPERATION_LIST, reply, size));       /* NUL in name */

    /* get_info */
    reply[0] = reply[1] = reply[2] = reply[3] = 0;
    size = 4;
    add_info(reply, &size, "a", 1, "b", 1);
    EXPECT(64, refused(APP_APPS_OPERATION_GET_INFO, reply, size - 1));
    EXPECT(65, refused(APP_APPS_OPERATION_GET_INFO, reply, size + 1));
    EXPECT(66, refused(APP_APPS_OPERATION_GET_INFO, reply, 4));
    size = 4;
    add_info(reply, &size, "", 0, "b", 1);
    EXPECT(67, refused(APP_APPS_OPERATION_GET_INFO, reply, size));
    size = 4;
    add_info(reply, &size, "a", 1, text65, 65);
    EXPECT(68, refused(APP_APPS_OPERATION_GET_INFO, reply, size));
    reply[4] = 5; /* a length that runs past the end */
    reply[5] = 0;
    EXPECT(69, refused(APP_APPS_OPERATION_GET_INFO, reply, 8));

    /* launch */
    EXPECT(70, refused(APP_APPS_OPERATION_LAUNCH, launch, 12));      /* instance 0 */
    launch[4] = 1;
    EXPECT(71, refused(APP_APPS_OPERATION_LAUNCH, launch, 11));
    EXPECT(72, refused(APP_APPS_OPERATION_LAUNCH, launch, 13));
    EXPECT(73, refused(APP_APPS_OPERATION_LAUNCH, launch, 4));

    /* stop */
    EXPECT(74, refused(APP_APPS_OPERATION_STOP, stop, 5));
    EXPECT(75, refused(APP_APPS_OPERATION_STOP, stop, 8));

    /* status */
    status[4] = APP_INSTANCE_STATE_STARTING;
    EXPECT(76, refused(APP_APPS_OPERATION_GET_STATUS, status, 10));
    status[4] = 4;
    EXPECT(77, refused(APP_APPS_OPERATION_GET_STATUS, status, 10));
    status[4] = 255;
    EXPECT(78, refused(APP_APPS_OPERATION_GET_STATUS, status, 10));
    status[4] = APP_INSTANCE_STATE_EXITED;
    status[5] = 2;
    EXPECT(79, refused(APP_APPS_OPERATION_GET_STATUS, status, 10));  /* exit kind */
    status[5] = 0;
    EXPECT(80, refused(APP_APPS_OPERATION_GET_STATUS, status, 9));
    EXPECT(81, refused(APP_APPS_OPERATION_GET_STATUS, status, 11));
    EXPECT(82, refused(APP_APPS_OPERATION_GET_STATUS, status, 4));
}

/* ---- truncations and single-byte changes ---------------------------------------------------- */

static void exhaustive(void)
{
    static u8 reply[512];
    AppDecoded decoded;
    usize size = 6;
    u32 accepted_changes = 0;

    put32(reply, 0);
    put16(reply + 4, 2);
    add_info(reply, &size, "clock", 5, "Clock", 5);
    add_info(reply, &size, "notes", 5, "Notes", 5);

    /* every shorter prefix is refused, the whole payload accepted */
    for (usize cut = 0; cut < size; ++cut) {
        if (decode(APP_APPS_OPERATION_LIST, reply, cut, &decoded) == CORE_OK) {
            libappframework_internal_apps_decoded_release(&decoded);
            check_fail(96);
        }
    }
    EXPECT(97, decode(APP_APPS_OPERATION_LIST, reply, size, &decoded) == CORE_OK);
    libappframework_internal_apps_decoded_release(&decoded);

    /* any byte set to every value: the decoder either refuses or returns a
     * result whose strings are all NUL-terminated inside its storage */
    for (usize position = 0; position < size; ++position) {
        u8 saved = reply[position];

        for (u32 value = 0; value < 256; ++value) {
            ErrorCode result;

            reply[position] = (u8)value;
            result = decode(APP_APPS_OPERATION_LIST, reply, size, &decoded);
            if (result == CORE_OK && decoded.service_error == CORE_OK) {
                for (usize i = 0; i < decoded.result.list.count; ++i) {
                    const char *id = decoded.result.list.items[i].application_id;
                    usize length = 0;

                    while (id[length] != '\0') {
                        ++length;
                    }
                    if (length == 0 || length > 64) {
                        check_fail(98);
                    }
                }
                accepted_changes++;
            }
            if (result == CORE_OK) {
                libappframework_internal_apps_decoded_release(&decoded);
            }
        }
        reply[position] = saved;
    }
    EXPECT(99, accepted_changes > 0); /* letters of the texts may change freely */
}

int main(void)
{
    encoding();
    accepted_replies();
    refused_replies();
    exhaustive();
    check_passed();
    return 0;
}
