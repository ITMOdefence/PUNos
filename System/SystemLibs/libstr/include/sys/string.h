#ifndef PUNOS_SYS_STRING_H
#define PUNOS_SYS_STRING_H

/*
 * libstr (sys/string) - owned, immutable UTF-8 strings.
 *
 * Specification: DevDoc/SystemLibs/libstr.md
 *
 * Invariants of every valid String:
 *   - data holds exactly length bytes of well-formed UTF-8 (RFC 3629: no
 *     overlong forms, no UTF-16 surrogates, nothing above U+10FFFF);
 *   - none of those bytes is NUL, and data[length] is NUL, so data is also a
 *     C string;
 *   - length <= STRING_MAX_LENGTH.
 *
 * Operations check these invariants when a String is created and trust them
 * afterwards; they only do cheap sanity checks on String arguments.
 *
 * Memory comes only from libmem, and libstr makes no syscalls of its own.
 * All operations return ErrorCode; see <sys/core.h> for the conventions.
 */

#include <sys/core.h>

/* Maximum length of a string in bytes, not counting the terminating NUL. */
#define STRING_MAX_LENGTH ((usize)1048576)

/*
 * Owning UTF-8 string. Created by a string_* operation and released exactly
 * once with string_destroy(). Copying the structure does not copy the text:
 * after one copy is destroyed, all copies are invalid. Treat the bytes as
 * read-only; use string_copy() for an independent string.
 */
typedef struct String {
    char *data;
    usize length;
} String;

/* Result of string_compare(). The values follow the sign of left - right. */
typedef enum StringCompareResult {
    STRING_COMPARE_RESULT_LESS = -1,
    STRING_COMPARE_RESULT_EQUAL = 0,
    STRING_COMPARE_RESULT_GREATER = 1
} StringCompareResult;

_Static_assert(sizeof(String) == 16, "String ABI mismatch");
_Static_assert(sizeof(StringCompareResult) == 4, "StringCompareResult ABI mismatch");

/*
 * Creates a string from bytes.size bytes of UTF-8 (no terminating NUL needed).
 *
 * Errors: CORE_INVALID_ARGUMENT if out_string is NULL, bytes.data is NULL with
 * a non-zero size, or the bytes are not well-formed UTF-8 or contain NUL;
 * CORE_OVERFLOW if bytes.size > STRING_MAX_LENGTH; errors of libmem.
 */
ErrorCode string_from_utf8(Bytes bytes, String *out_string);

/*
 * Creates a string from a NUL-terminated UTF-8 C string. At most
 * STRING_MAX_LENGTH + 1 bytes of source are read.
 *
 * Errors: CORE_INVALID_ARGUMENT if source or out_string is NULL or source is
 * not well-formed UTF-8; CORE_OVERFLOW if source is longer than
 * STRING_MAX_LENGTH; errors of libmem.
 */
ErrorCode string_from_c_str(const char *source, String *out_string);

/*
 * Releases the string's memory and sets *string to { NULL, 0 }. Destroying
 * { NULL, 0 } again is a no-op.
 *
 * Errors: CORE_INVALID_ARGUMENT if string is NULL or not a valid String;
 * errors of libmem.
 */
ErrorCode string_destroy(String *string);

/*
 * Creates an independent copy of *source.
 *
 * Errors: CORE_INVALID_ARGUMENT if an argument is NULL or *source is not a
 * valid String; errors of libmem.
 */
ErrorCode string_copy(const String *source, String *out_string);

/*
 * Creates a new string holding *left followed by *right. left and right may
 * be the same string. The result is written to *out_string; a string already
 * stored there is not destroyed.
 *
 * Errors: CORE_INVALID_ARGUMENT if an argument is NULL or not a valid String;
 * CORE_OVERFLOW if the result would exceed STRING_MAX_LENGTH; errors of libmem.
 */
ErrorCode string_concat(const String *left, const String *right, String *out_string);

/*
 * Stores the length in bytes, without the terminating NUL, in *out_length.
 * This is the number of UTF-8 bytes, not of characters.
 *
 * Errors: CORE_INVALID_ARGUMENT if an argument is NULL or *string is not a
 * valid String.
 */
ErrorCode string_length(const String *string, usize *out_length);

/*
 * Compares two strings lexicographically by bytes. For UTF-8 this is the same
 * as comparing by Unicode code points. A prefix is less than the longer
 * string. No locale or normalisation is applied.
 *
 * Errors: CORE_INVALID_ARGUMENT if an argument is NULL or not a valid String.
 */
ErrorCode string_compare(const String *left, const String *right,
                         StringCompareResult *out_result);

/*
 * Stores true in *out_equal if both strings have the same bytes.
 *
 * Errors: CORE_INVALID_ARGUMENT if an argument is NULL or not a valid String.
 */
ErrorCode string_equals(const String *left, const String *right, bool *out_equal);

/*
 * Stores a pointer to the string's NUL-terminated bytes in *out_data. The
 * pointer does not own the memory and stays valid until *string is destroyed.
 *
 * Errors: CORE_INVALID_ARGUMENT if an argument is NULL or *string is not a
 * valid String.
 */
ErrorCode string_c_str(const String *string, const char **out_data);

#endif
