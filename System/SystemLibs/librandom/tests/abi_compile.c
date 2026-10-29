#include <sys/random.h>

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)

_Static_assert(SAME_TYPE(random_fill((Bytes){ 0 }), ErrorCode),
               "random_fill return type");
