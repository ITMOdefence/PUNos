#ifndef PUNOS_SYS_MATH_H
#define PUNOS_SYS_MATH_H

/*
 * libmath (sys/math) - numeric, geometric and trigonometric operations.
 *
 * Specification: DevDoc/SystemLibs/libmath.md
 *
 * Every operation is a pure function of its arguments: no syscalls, no
 * mutable global state. Angles are in radians. An operation with no valid
 * numeric result returns NaN, which then flows unchanged through further
 * arithmetic instead of being coerced into a number.
 *
 * Unlike every other System Lib, operations here are not prefixed with the
 * library name and do not return ErrorCode: the specification's operation
 * table lists the bare names (abs, sqrt, sin, ...) as the contract.
 */

#include <sys/core.h>

#define PI ((f64)3.141592653589793)
#define TAU ((f64)6.283185307179586)
#define DEG_TO_RAD (PI / (f64)180.0)
#define RAD_TO_DEG ((f64)180.0 / PI)

/* ------------------------------------------------------------------------ */
/* Basic operations                                                         */
/* ------------------------------------------------------------------------ */

/* Absolute value. */
f64 abs(f64 x);

/* Smaller of the two; if exactly one argument is NaN, returns the other. */
f64 min(f64 a, f64 b);

/* Larger of the two; if exactly one argument is NaN, returns the other. */
f64 max(f64 a, f64 b);

/* Clamps value to [min_value, max_value]. */
f64 clamp(f64 value, f64 min_value, f64 max_value);

/* Rounds down to the nearest integer. */
f64 floor(f64 x);

/* Rounds up to the nearest integer. */
f64 ceil(f64 x);

/* Rounds to the nearest integer, ties away from zero. */
f64 round(f64 x);

/* ------------------------------------------------------------------------ */
/* Geometry                                                                 */
/* ------------------------------------------------------------------------ */

/* Square root; NaN for a negative argument. */
f64 sqrt(f64 x);

/* Length of the vector (x, y). */
f64 hypot(f64 x, f64 y);

/* Sine of an angle in radians. */
f64 sin(f64 angle);

/* Cosine of an angle in radians. */
f64 cos(f64 angle);

/* Angle of (x, y) in radians, in the range [-PI, PI]. */
f64 atan2(f64 y, f64 x);

f64 degrees_to_radians(f64 value);
f64 radians_to_degrees(f64 value);

/* ------------------------------------------------------------------------ */
/* Number checks                                                            */
/* ------------------------------------------------------------------------ */

bool is_nan(f64 x);
bool is_finite(f64 x);

#endif
