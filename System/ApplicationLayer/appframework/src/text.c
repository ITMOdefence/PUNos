#include "internal.h"

/*
 * Well-formed UTF-8 as in Unicode Table 3-7, with NUL refused. libstr applies
 * the same rules; the test compares the two on a corpus, because a string that
 * this check lets through but loglib refuses could stop logserv.
 */
static bool in_range(u8 value, u8 low, u8 high)
{
    return value >= low && value <= high;
}

bool libappframework_internal_utf8_valid(const u8 *data, usize size)
{
    usize index = 0;

    if (data == NULL) {
        return size == 0;
    }
    while (index < size) {
        u8 lead = data[index];
        usize tail; /* continuation bytes that follow the lead byte */
        u8 second_low = 0x80;
        u8 second_high = 0xbf;

        if (lead == 0x00) {
            return false;
        }
        if (lead < 0x80) {
            ++index;
            continue;
        }
        if (in_range(lead, 0xc2, 0xdf)) {
            tail = 1;
        } else if (in_range(lead, 0xe0, 0xef)) {
            tail = 2;
            if (lead == 0xe0) {
                second_low = 0xa0;  /* no overlong forms */
            } else if (lead == 0xed) {
                second_high = 0x9f; /* no surrogates */
            }
        } else if (in_range(lead, 0xf0, 0xf4)) {
            tail = 3;
            if (lead == 0xf0) {
                second_low = 0x90;  /* no overlong forms */
            } else if (lead == 0xf4) {
                second_high = 0x8f; /* nothing above U+10FFFF */
            }
        } else {
            return false;
        }
        if (size - index <= tail) {
            return false; /* truncated sequence */
        }
        if (!in_range(data[index + 1], second_low, second_high)) {
            return false;
        }
        for (usize i = 2; i <= tail; ++i) {
            if (!in_range(data[index + i], 0x80, 0xbf)) {
                return false;
            }
        }
        index += tail + 1;
    }
    return true;
}

ErrorCode libappframework_internal_text_check(const char *text, usize min_length,
                                              usize max_length, usize *out_length)
{
    usize length = 0;

    if (text == NULL || out_length == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    while (text[length] != '\0') {
        if (length == max_length) {
            return CORE_INVALID_ARGUMENT; /* too long; never read further */
        }
        ++length;
    }
    if (length < min_length ||
        !libappframework_internal_utf8_valid((const u8 *)text, length)) {
        return CORE_INVALID_ARGUMENT;
    }
    *out_length = length;
    return CORE_OK;
}
