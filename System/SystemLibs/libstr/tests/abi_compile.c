/*
 * Compile-time checks of the libstr ABI against DevDoc/SystemLibs/libstr.md.
 * This file only has to compile; it produces no runnable program.
 */
#include <sys/string.h>

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)

_Static_assert(__builtin_offsetof(String, data) == 0, "String.data offset");
_Static_assert(__builtin_offsetof(String, length) == 8, "String.length offset");
_Static_assert(SAME_TYPE(((String *)0)->data, char *), "String.data type");
_Static_assert(SAME_TYPE(((String *)0)->length, usize), "String.length type");

_Static_assert(STRING_MAX_LENGTH == 1048576, "STRING_MAX_LENGTH");
_Static_assert(SAME_TYPE(STRING_MAX_LENGTH, usize), "STRING_MAX_LENGTH type");

_Static_assert(STRING_COMPARE_RESULT_LESS == -1, "LESS");
_Static_assert(STRING_COMPARE_RESULT_EQUAL == 0, "EQUAL");
_Static_assert(STRING_COMPARE_RESULT_GREATER == 1, "GREATER");
