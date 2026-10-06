#define CHECK_TEST_NAME "librandom"
#include "../../testing/check.h"

#include <sys/random.h>

static bool differs(const u8 *left, const u8 *right, usize size)
{
    usize index;
    for (index = 0; index < size; ++index) {
        if (left[index] != right[index]) {
            return true;
        }
    }
    return false;
}

int main(int argc, char **argv, char **envp)
{
    u8 first[32] = { 0 };
    u8 second[32] = { 0 };

    (void)argc;
    (void)argv;
    (void)envp;

    EXPECT(1, random_fill((Bytes){ first, sizeof(first) }) == CORE_OK);
    EXPECT(2, random_fill((Bytes){ second, sizeof(second) }) == CORE_OK);
    EXPECT(3, differs(first, second, sizeof(first)));
    EXPECT(4, random_fill((Bytes){ NULL, 0 }) == CORE_OK);
    EXPECT(5, random_fill((Bytes){ NULL, 1 }) == CORE_INVALID_ARGUMENT);

    check_passed();
    return 0;
}
