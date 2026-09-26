#include <sys/string.h>

#include <sys/memory.h>

/*
 * Shared buffer of every empty string. An empty string needs only its NUL
 * byte, and giving each one a page from libmem would waste 4 KiB. The buffer
 * is never written: libstr treats string bytes as read-only.
 */
static char empty_data[1] = { '\0' };

/* ------------------------------------------------------------------------ */
/* UTF-8                                                                    */
/* ------------------------------------------------------------------------ */

/*
 * Returns true if data[0..size) is well-formed UTF-8 without NUL.
 *
 * Well-formed means RFC 3629 / Unicode table 3-7: every sequence has the
 * minimal length for its code point, and no sequence encodes a surrogate
 * (U+D800..U+DFFF) or a value above U+10FFFF. The second byte's allowed range
 * depends on the first byte; later continuation bytes are always 0x80..0xBF.
 */
static bool utf8_is_valid(const u8 *data, usize size)
{
    usize index = 0;

    while (index < size) {
        u8 lead = data[index];
        u8 second_min = 0x80;
        u8 second_max = 0xbf;
        usize count;
        usize offset;

        if (lead == 0x00) {
            return false; /* NUL is not allowed inside a string */
        }
        if (lead < 0x80) {
            ++index;
            continue;
        }

        if (lead >= 0xc2 && lead <= 0xdf) {
            count = 2;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            count = 3;
            if (lead == 0xe0) {
                second_min = 0xa0; /* below: overlong */
            } else if (lead == 0xed) {
                second_max = 0x9f; /* above: surrogates */
            }
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            count = 4;
            if (lead == 0xf0) {
                second_min = 0x90; /* below: overlong */
            } else if (lead == 0xf4) {
                second_max = 0x8f; /* above: beyond U+10FFFF */
            }
        } else {
            /* 0x80..0xC1: continuation or overlong lead; 0xF5..0xFF: never valid. */
            return false;
        }

        if (size - index < count) {
            return false; /* truncated sequence */
        }
        if (data[index + 1] < second_min || data[index + 1] > second_max) {
            return false;
        }
        for (offset = 2; offset < count; ++offset) {
            if (data[index + offset] < 0x80 || data[index + offset] > 0xbf) {
                return false;
            }
        }
        index += count;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static Bytes string_bytes(const String *string)
{
    Bytes bytes = { (u8 *)string->data, string->length };
    return bytes;
}

/* Cheap checks of the String invariants; UTF-8 is verified only on creation. */
static bool string_is_valid(const String *string)
{
    return string != NULL && string->data != NULL &&
           string->length <= STRING_MAX_LENGTH &&
           string->data[string->length] == '\0';
}

/* The MemoryBlock that backs a non-empty string: its bytes plus the NUL. */
static MemoryBlock string_block(const String *string)
{
    MemoryBlock block = { (u8 *)string->data, string->length + 1 };
    return block;
}

/*
 * Creates a string from length bytes taken from first and then second (either
 * part may be empty). The caller has checked length <= STRING_MAX_LENGTH and
 * that the bytes are valid.
 */
static ErrorCode string_build(Bytes first, Bytes second, String *out_string)
{
    MemoryBlock block;
    ErrorCode result;
    usize length = first.size + second.size;
    Bytes head;
    Bytes tail;

    if (length == 0) {
        out_string->data = empty_data;
        out_string->length = 0;
        return CORE_OK;
    }

    /* The block is zero-filled, so the terminating NUL is already in place. */
    result = memory_allocate(length + 1, &block);
    if (result != CORE_OK) {
        return result;
    }
    /* A fresh block never overlaps the sources, so the copies cannot fail. */
    head.data = block.data;
    head.size = first.size;
    tail.data = block.data + first.size;
    tail.size = second.size;
    (void)memory_copy(head, first);
    (void)memory_copy(tail, second);

    out_string->data = (char *)block.data;
    out_string->length = length;
    return CORE_OK;
}

/* ------------------------------------------------------------------------ */
/* Creation and destruction                                                 */
/* ------------------------------------------------------------------------ */

ErrorCode string_from_utf8(Bytes bytes, String *out_string)
{
    static const Bytes nothing = { NULL, 0 };

    if (out_string == NULL || (bytes.data == NULL && bytes.size != 0)) {
        return CORE_INVALID_ARGUMENT;
    }
    if (bytes.size > STRING_MAX_LENGTH) {
        return CORE_OVERFLOW;
    }
    if (!utf8_is_valid(bytes.data, bytes.size)) {
        return CORE_INVALID_ARGUMENT;
    }
    return string_build(bytes, nothing, out_string);
}

ErrorCode string_from_c_str(const char *source, String *out_string)
{
    Bytes bytes;
    usize length = 0;

    if (source == NULL || out_string == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    /* Never read past STRING_MAX_LENGTH + 1 bytes, even if NUL is missing. */
    while (source[length] != '\0') {
        if (length == STRING_MAX_LENGTH) {
            return CORE_OVERFLOW;
        }
        ++length;
    }

    bytes.data = (u8 *)source;
    bytes.size = length;
    return string_from_utf8(bytes, out_string);
}

ErrorCode string_destroy(String *string)
{
    MemoryBlock block;
    ErrorCode result;

    if (string == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (string->data == NULL && string->length == 0) {
        return CORE_OK; /* already destroyed */
    }
    if (!string_is_valid(string)) {
        return CORE_INVALID_ARGUMENT;
    }

    if (string->data != empty_data) {
        block = string_block(string);
        result = memory_free(&block);
        if (result != CORE_OK) {
            return result;
        }
    }
    string->data = NULL;
    string->length = 0;
    return CORE_OK;
}

ErrorCode string_copy(const String *source, String *out_string)
{
    static const Bytes nothing = { NULL, 0 };

    if (!string_is_valid(source) || out_string == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    return string_build(string_bytes(source), nothing, out_string);
}

ErrorCode string_concat(const String *left, const String *right, String *out_string)
{
    usize length;

    if (!string_is_valid(left) || !string_is_valid(right) || out_string == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    /* Two lengths <= STRING_MAX_LENGTH cannot overflow usize. */
    length = left->length + right->length;
    if (length > STRING_MAX_LENGTH) {
        return CORE_OVERFLOW;
    }
    /* Both parts are complete UTF-8 sequences, so the result is valid too. */
    return string_build(string_bytes(left), string_bytes(right), out_string);
}

/* ------------------------------------------------------------------------ */
/* Queries                                                                  */
/* ------------------------------------------------------------------------ */

ErrorCode string_length(const String *string, usize *out_length)
{
    if (!string_is_valid(string) || out_length == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_length = string->length;
    return CORE_OK;
}

ErrorCode string_compare(const String *left, const String *right,
                         StringCompareResult *out_result)
{
    MemoryCompareResult bytes_result;
    ErrorCode result;

    if (!string_is_valid(left) || !string_is_valid(right) || out_result == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = memory_compare(string_bytes(left), string_bytes(right), &bytes_result);
    if (result != CORE_OK) {
        return result;
    }
    switch (bytes_result) {
    case MEMORY_COMPARE_RESULT_LESS:
        *out_result = STRING_COMPARE_RESULT_LESS;
        break;
    case MEMORY_COMPARE_RESULT_GREATER:
        *out_result = STRING_COMPARE_RESULT_GREATER;
        break;
    default:
        *out_result = STRING_COMPARE_RESULT_EQUAL;
        break;
    }
    return CORE_OK;
}

ErrorCode string_equals(const String *left, const String *right, bool *out_equal)
{
    StringCompareResult compared;
    ErrorCode result;

    if (out_equal == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = string_compare(left, right, &compared);
    if (result != CORE_OK) {
        return result;
    }
    *out_equal = compared == STRING_COMPARE_RESULT_EQUAL;
    return CORE_OK;
}

ErrorCode string_c_str(const String *string, const char **out_data)
{
    if (!string_is_valid(string) || out_data == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_data = string->data;
    return CORE_OK;
}
