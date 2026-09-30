/*
 * Compile-time checks of the libmath ABI against DevDoc/SystemLibs/libmath.md.
 * This file only has to compile; it produces no runnable program.
 */
#include <sys/math.h>

#define SAME_TYPE(a, b) _Generic((a), b: 1, default: 0)

/* Constants are f64, as the specification's type table requires. */
_Static_assert(SAME_TYPE(PI, f64), "PI type");
_Static_assert(SAME_TYPE(TAU, f64), "TAU type");
_Static_assert(SAME_TYPE(DEG_TO_RAD, f64), "DEG_TO_RAD type");
_Static_assert(SAME_TYPE(RAD_TO_DEG, f64), "RAD_TO_DEG type");

/* Values are pinned to the specification's table (16 significant digits). */
_Static_assert(PI > 3.141592653589792 && PI < 3.141592653589794, "PI value");
_Static_assert(TAU > 6.283185307179585 && TAU < 6.283185307179587, "TAU value");
_Static_assert(TAU == PI + PI, "TAU must be 2 * PI");
