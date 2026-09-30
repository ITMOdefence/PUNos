#ifndef PUNOS_SYSTEM_LIBS_TESTING_CHECK_H
#define PUNOS_SYSTEM_LIBS_TESTING_CHECK_H

/*
 * Minimal check helper shared by the System Libs runtime tests.
 *
 * Every check has a unique number within its test. The first failed check
 * prints "<test> runtime test failed: check N" to stderr and exits with
 * status N (1-255), so a failure is identifiable from a host shell (echo $?)
 * and from the QEMU console. Header-only on purpose: tests link nothing extra.
 *
 * Usage:
 *     #define CHECK_TEST_NAME "libfoo"
 *     #include "../../testing/check.h"
 *     EXPECT(1, foo() == CORE_OK);
 *     ...
 *     check_passed();
 */

#include <punos/syscall/syscall.h>
#include <sys/core.h>

#ifndef CHECK_TEST_NAME
#error "Define CHECK_TEST_NAME before including check.h"
#endif

static inline usize check_text_length(const char *text)
{
    usize length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static inline void check_print(const char *text)
{
    (void)write(STDERR_FILENO, text, check_text_length(text));
}

_Noreturn static inline void check_fail(i32 check)
{
    char digits[4];
    usize count = 0;
    u32 value = (u32)check;

    check_print(CHECK_TEST_NAME " runtime test failed: check ");
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) {
        --count;
        (void)write(STDERR_FILENO, &digits[count], 1);
    }
    check_print("\n");
    exit(check);
}

static inline void check_passed(void)
{
    check_print(CHECK_TEST_NAME " runtime test passed\n");
}

#define EXPECT(check, condition)                                         \
    do {                                                                 \
        _Static_assert((check) >= 1 && (check) <= 255,                   \
                       "check numbers must fit in an exit status");      \
        if (!(condition)) {                                              \
            check_fail(check);                                           \
        }                                                                \
    } while (0)

#endif
