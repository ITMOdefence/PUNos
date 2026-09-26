/*
 * Runtime test of libstr against DevDoc/SystemLibs/libstr.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 * Check numbers are grouped: 1-29 well-formed UTF-8, 30-59 malformed UTF-8,
 * 60-79 length limit, 80-99 from_c_str, 100-119 destroy and copy,
 * 120-149 concat, 150-169 compare/equals, 170-179 length/c_str.
 */
#define CHECK_TEST_NAME "libstr"
#include "../../testing/check.h"

#include <sys/memory.h>
#include <sys/string.h>

#define PAGE 4096

static Bytes view(const void *data, usize size)
{
    Bytes bytes = { (u8 *)data, size };
    return bytes;
}

/* true if the page containing address is mapped (mprotect fails with ENOMEM otherwise). */
static bool page_is_mapped(const void *address)
{
    uintptr_t page = (uintptr_t)address & ~(uintptr_t)(PAGE - 1);

    return mprotect((void *)page, PAGE, PROT_READ | PROT_WRITE) == 0 || errno != ENOMEM;
}

static bool same_bytes(const char *left, const char *right, usize size)
{
    usize index;

    for (index = 0; index < size; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

/* Creates a string from size bytes, checks its content and NUL, destroys it. */
static bool accepts(const char *bytes, usize size)
{
    String string;

    if (string_from_utf8(view(bytes, size), &string) != CORE_OK) {
        return false;
    }
    if (string.length != size || !same_bytes(string.data, bytes, size) ||
        string.data[size] != '\0') {
        return false;
    }
    return string_destroy(&string) == CORE_OK;
}

static bool rejects(const char *bytes, usize size)
{
    String string;

    return string_from_utf8(view(bytes, size), &string) == CORE_INVALID_ARGUMENT;
}

static void test_valid_utf8(void)
{
    EXPECT(1, accepts("", 0));
    EXPECT(2, accepts("hello", 5));
    EXPECT(3, accepts("\x7f", 1));                 /* U+007F, last 1-byte */
    EXPECT(4, accepts("\xc2\x80", 2));             /* U+0080, first 2-byte */
    EXPECT(5, accepts("h\xc3\xa9llo", 6));         /* é */
    EXPECT(6, accepts("\xdf\xbf", 2));             /* U+07FF, last 2-byte */
    EXPECT(7, accepts("\xe0\xa0\x80", 3));         /* U+0800, first 3-byte */
    EXPECT(8, accepts("\xe2\x82\xac", 3));         /* € */
    EXPECT(9, accepts("\xed\x9f\xbf", 3));         /* U+D7FF, just below surrogates */
    EXPECT(10, accepts("\xee\x80\x80", 3));        /* U+E000, just above surrogates */
    EXPECT(11, accepts("\xef\xbf\xbf", 3));        /* U+FFFF, last 3-byte */
    EXPECT(12, accepts("\xf0\x90\x80\x80", 4));    /* U+10000, first 4-byte */
    EXPECT(13, accepts("\xf0\x9f\x98\x80", 4));    /* emoji */
    EXPECT(14, accepts("\xf4\x8f\xbf\xbf", 4));    /* U+10FFFF, last code point */
    EXPECT(15, accepts("\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82", 12)); /* Привет */
}

static void test_malformed_utf8(void)
{
    String string;

    EXPECT(30, rejects("a\0b", 3));                /* NUL inside */
    EXPECT(31, rejects("\0", 1));
    EXPECT(32, rejects("\x80", 1));                /* lone continuation */
    EXPECT(33, rejects("\xbf", 1));
    EXPECT(34, rejects("\xc0\x80", 2));            /* overlong NUL */
    EXPECT(35, rejects("\xc1\xbf", 2));            /* overlong U+007F */
    EXPECT(36, rejects("\xe0\x9f\xbf", 3));        /* overlong U+07FF */
    EXPECT(37, rejects("\xed\xa0\x80", 3));        /* surrogate U+D800 */
    EXPECT(38, rejects("\xed\xbf\xbf", 3));        /* surrogate U+DFFF */
    EXPECT(39, rejects("\xf0\x8f\xbf\xbf", 4));    /* overlong U+FFFF */
    EXPECT(40, rejects("\xf4\x90\x80\x80", 4));    /* U+110000 */
    EXPECT(41, rejects("\xf5\x80\x80\x80", 4));    /* lead byte above F4 */
    EXPECT(42, rejects("\xff", 1));
    EXPECT(43, rejects("\xc3", 1));                /* truncated at end */
    EXPECT(44, rejects("\xe2\x82", 2));
    EXPECT(45, rejects("\xf0\x9f\x98", 3));
    EXPECT(46, rejects("\xc3\x28", 2));            /* bad continuation */
    EXPECT(47, rejects("\xe2\x28\xac", 3));
    EXPECT(48, rejects("\xe2\x82\x28", 3));
    EXPECT(49, rejects("\xf0\x9f\x98\x28", 4));
    EXPECT(50, rejects("ok\xc3", 3));              /* valid prefix, bad end */
    /* Truncated by size, with a valid continuation byte just past the end. */
    EXPECT(55, rejects("\xc3\xa9", 1));
    EXPECT(56, rejects("\xe2\x82\xac", 2));
    EXPECT(57, rejects("\xf0\x9f\x98\x80", 3));

    EXPECT(51, string_from_utf8(view("a", 1), NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(52, string_from_utf8(view(NULL, 1), &string) == CORE_INVALID_ARGUMENT);
    EXPECT(53, string_from_utf8(view(NULL, 0), &string) == CORE_OK && string.length == 0);
    EXPECT(54, string_destroy(&string) == CORE_OK);
}

static void test_length_limit(void)
{
    MemoryBlock block;
    String string;
    usize length = 0;

    EXPECT(60, memory_allocate(STRING_MAX_LENGTH + 1, &block) == CORE_OK);
    EXPECT(61, memory_set(view(block.data, block.size), 'a') == CORE_OK);

    EXPECT(62, string_from_utf8(view(block.data, STRING_MAX_LENGTH), &string) == CORE_OK);
    EXPECT(63, string_length(&string, &length) == CORE_OK && length == STRING_MAX_LENGTH);
    EXPECT(64, string.data[STRING_MAX_LENGTH] == '\0');
    EXPECT(65, string_destroy(&string) == CORE_OK);

    EXPECT(66, string_from_utf8(view(block.data, STRING_MAX_LENGTH + 1), &string) ==
                   CORE_OVERFLOW);
    EXPECT(67, memory_free(&block) == CORE_OK);
}

static void test_from_c_str(void)
{
    MemoryBlock block;
    String string;
    const usize readable = STRING_MAX_LENGTH + PAGE; /* whole pages */
    char *text;

    EXPECT(80, string_from_c_str("Hello, \xd0\xbc\xd0\xb8\xd1\x80", &string) == CORE_OK);
    EXPECT(81, string.length == 13 && string.data[13] == '\0');
    EXPECT(82, same_bytes(string.data, "Hello, \xd0\xbc\xd0\xb8\xd1\x80", 13));
    EXPECT(83, string_destroy(&string) == CORE_OK);

    EXPECT(84, string_from_c_str("", &string) == CORE_OK && string.length == 0);
    EXPECT(85, string_destroy(&string) == CORE_OK);
    EXPECT(86, string_from_c_str("bad \xc3", &string) == CORE_INVALID_ARGUMENT);
    EXPECT(87, string_from_c_str(NULL, &string) == CORE_INVALID_ARGUMENT);
    EXPECT(88, string_from_c_str("a", NULL) == CORE_INVALID_ARGUMENT);

    /*
     * No NUL within STRING_MAX_LENGTH + 1 bytes: CORE_OVERFLOW, and nothing
     * beyond those bytes is read. The text is placed so that its byte
     * STRING_MAX_LENGTH + 1 is the first byte of an inaccessible page; reading
     * it would kill the test with SIGSEGV.
     */
    EXPECT(89, memory_allocate(readable + PAGE, &block) == CORE_OK);
    EXPECT(90, memory_set(view(block.data, readable), 'a') == CORE_OK);
    EXPECT(91, mprotect(block.data + readable, PAGE, PROT_NONE) == 0);
    text = (char *)block.data + readable - (STRING_MAX_LENGTH + 1);
    EXPECT(92, string_from_c_str(text, &string) == CORE_OVERFLOW);

    /* Exactly STRING_MAX_LENGTH bytes before the NUL is still accepted. */
    text[STRING_MAX_LENGTH] = '\0';
    EXPECT(93, string_from_c_str(text, &string) == CORE_OK &&
                   string.length == STRING_MAX_LENGTH);
    EXPECT(94, string_destroy(&string) == CORE_OK);
    EXPECT(95, memory_free(&block) == CORE_OK);
}

static void test_destroy_and_copy(void)
{
    String original;
    String copy;
    String empty;
    String bogus;
    char *data;

    EXPECT(100, string_from_c_str("text", &original) == CORE_OK);
    EXPECT(101, string_copy(&original, &copy) == CORE_OK);
    EXPECT(102, copy.data != original.data && copy.length == 4);
    EXPECT(103, same_bytes(copy.data, "text", 5));

    /* The copy is independent: it survives the original. */
    data = original.data;
    EXPECT(104, string_destroy(&original) == CORE_OK);
    EXPECT(105, original.data == NULL && original.length == 0);
    EXPECT(106, !page_is_mapped(data));
    EXPECT(107, same_bytes(copy.data, "text", 5));

    /* A second destroy of the same variable is harmless. */
    EXPECT(108, string_destroy(&original) == CORE_OK);
    EXPECT(109, string_destroy(&copy) == CORE_OK);

    /* Empty strings and their copies. */
    EXPECT(110, string_from_c_str("", &empty) == CORE_OK);
    EXPECT(111, string_copy(&empty, &copy) == CORE_OK && copy.length == 0 &&
                    copy.data[0] == '\0');
    EXPECT(112, string_destroy(&copy) == CORE_OK && string_destroy(&empty) == CORE_OK);

    /* Invalid arguments. */
    bogus.data = "abc";
    bogus.length = 2; /* data[2] is not NUL */
    EXPECT(113, string_destroy(&bogus) == CORE_INVALID_ARGUMENT);
    EXPECT(114, string_copy(&bogus, &copy) == CORE_INVALID_ARGUMENT);
    EXPECT(115, string_destroy(NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(116, string_copy(NULL, &copy) == CORE_INVALID_ARGUMENT);
    EXPECT(117, string_from_c_str("x", &original) == CORE_OK);
    EXPECT(118, string_copy(&original, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(119, string_destroy(&original) == CORE_OK);
}

static void test_concat(void)
{
    MemoryBlock block;
    String left;
    String right;
    String empty;
    String result;
    String half;

    EXPECT(120, string_from_c_str("Hello, ", &left) == CORE_OK);
    EXPECT(121, string_from_c_str("\xd0\xbc\xd0\xb8\xd1\x80", &right) == CORE_OK);
    EXPECT(122, string_from_c_str("", &empty) == CORE_OK);

    EXPECT(123, string_concat(&left, &right, &result) == CORE_OK);
    EXPECT(124, result.length == 13 && result.data[13] == '\0');
    EXPECT(125, same_bytes(result.data, "Hello, \xd0\xbc\xd0\xb8\xd1\x80", 13));
    EXPECT(126, string_destroy(&result) == CORE_OK);

    EXPECT(127, string_concat(&left, &empty, &result) == CORE_OK &&
                    result.length == 7 && same_bytes(result.data, "Hello, ", 8));
    EXPECT(128, string_destroy(&result) == CORE_OK);
    EXPECT(129, string_concat(&empty, &empty, &result) == CORE_OK && result.length == 0);
    EXPECT(130, string_destroy(&result) == CORE_OK);

    /* The same string on both sides. */
    EXPECT(131, string_concat(&right, &right, &result) == CORE_OK && result.length == 12);
    EXPECT(132, same_bytes(result.data, "\xd0\xbc\xd0\xb8\xd1\x80\xd0\xbc\xd0\xb8\xd1\x80", 13));
    EXPECT(133, string_destroy(&result) == CORE_OK);

    /* Exactly the maximum length is fine; one byte more overflows. */
    EXPECT(134, memory_allocate(STRING_MAX_LENGTH / 2 + 1, &block) == CORE_OK);
    EXPECT(135, memory_set(view(block.data, block.size), 'z') == CORE_OK);
    EXPECT(136, string_from_utf8(view(block.data, STRING_MAX_LENGTH / 2), &half) == CORE_OK);
    EXPECT(137, string_concat(&half, &half, &result) == CORE_OK &&
                    result.length == STRING_MAX_LENGTH);
    EXPECT(138, string_destroy(&result) == CORE_OK && string_destroy(&half) == CORE_OK);
    EXPECT(139, string_from_utf8(view(block.data, STRING_MAX_LENGTH / 2 + 1), &half) == CORE_OK);
    EXPECT(140, string_concat(&half, &half, &result) == CORE_OVERFLOW);
    EXPECT(141, string_destroy(&half) == CORE_OK && memory_free(&block) == CORE_OK);

    EXPECT(142, string_concat(NULL, &right, &result) == CORE_INVALID_ARGUMENT);
    EXPECT(143, string_concat(&left, NULL, &result) == CORE_INVALID_ARGUMENT);
    EXPECT(144, string_concat(&left, &right, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(145, string_destroy(&left) == CORE_OK && string_destroy(&right) == CORE_OK &&
                    string_destroy(&empty) == CORE_OK);
}

static void test_compare(void)
{
    String abc;
    String abd;
    String ab;
    String other_abc;
    String accented;
    String z;
    StringCompareResult result = STRING_COMPARE_RESULT_EQUAL;
    bool equal = false;

    EXPECT(150, string_from_c_str("abc", &abc) == CORE_OK &&
                    string_from_c_str("abd", &abd) == CORE_OK &&
                    string_from_c_str("ab", &ab) == CORE_OK &&
                    string_from_c_str("abc", &other_abc) == CORE_OK &&
                    string_from_c_str("\xc3\xa9", &accented) == CORE_OK &&
                    string_from_c_str("z", &z) == CORE_OK);

    EXPECT(151, string_compare(&abc, &other_abc, &result) == CORE_OK &&
                    result == STRING_COMPARE_RESULT_EQUAL);
    EXPECT(152, string_compare(&abc, &abd, &result) == CORE_OK &&
                    result == STRING_COMPARE_RESULT_LESS);
    EXPECT(153, string_compare(&abd, &abc, &result) == CORE_OK &&
                    result == STRING_COMPARE_RESULT_GREATER);
    EXPECT(154, string_compare(&ab, &abc, &result) == CORE_OK &&
                    result == STRING_COMPARE_RESULT_LESS);
    /* Code point order: U+007A 'z' < U+00E9 'é' (bytes compared unsigned). */
    EXPECT(155, string_compare(&z, &accented, &result) == CORE_OK &&
                    result == STRING_COMPARE_RESULT_LESS);

    EXPECT(156, string_equals(&abc, &other_abc, &equal) == CORE_OK && equal);
    EXPECT(157, string_equals(&abc, &abd, &equal) == CORE_OK && !equal);
    EXPECT(158, string_equals(&abc, &ab, &equal) == CORE_OK && !equal);

    EXPECT(159, string_compare(&abc, &abd, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(160, string_compare(NULL, &abd, &result) == CORE_INVALID_ARGUMENT);
    EXPECT(161, string_equals(&abc, &abd, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(162, string_equals(&abc, NULL, &equal) == CORE_INVALID_ARGUMENT);

    EXPECT(163, string_destroy(&abc) == CORE_OK && string_destroy(&abd) == CORE_OK &&
                    string_destroy(&ab) == CORE_OK && string_destroy(&other_abc) == CORE_OK &&
                    string_destroy(&accented) == CORE_OK && string_destroy(&z) == CORE_OK);
}

static void test_queries(void)
{
    String string;
    String destroyed = { NULL, 0 };
    const char *data = NULL;
    usize length = 0;

    EXPECT(170, string_from_c_str("\xe2\x82\xac" "5", &string) == CORE_OK);
    /* Length counts bytes, not characters. */
    EXPECT(171, string_length(&string, &length) == CORE_OK && length == 4);
    EXPECT(172, string_c_str(&string, &data) == CORE_OK && data == string.data);
    EXPECT(173, same_bytes(data, "\xe2\x82\xac" "5", 5));

    EXPECT(174, string_length(&string, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(175, string_c_str(&string, NULL) == CORE_INVALID_ARGUMENT);
    EXPECT(176, string_length(NULL, &length) == CORE_INVALID_ARGUMENT);
    /* A destroyed string is no longer valid. */
    EXPECT(177, string_c_str(&destroyed, &data) == CORE_INVALID_ARGUMENT);
    EXPECT(178, string_destroy(&string) == CORE_OK);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    test_valid_utf8();
    test_malformed_utf8();
    test_length_limit();
    test_from_c_str();
    test_destroy_and_copy();
    test_concat();
    test_compare();
    test_queries();

    check_passed();
    return 0;
}
