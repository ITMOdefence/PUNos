#include <sys/math.h>

/*
 * IEEE 754 binary64 layout: 1 sign bit, 11 exponent bits (bias 1023), 52
 * mantissa bits. floor/ceil/round work directly on this layout instead of
 * relying on a soft-float library that does not exist in PUNos.
 */
#define F64_SIGN_MASK ((u64)0x8000000000000000ULL)
#define F64_EXPONENT_MASK ((u64)0x7ffULL)
#define F64_EXPONENT_BIAS 1023
#define F64_MANTISSA_BITS 52

typedef union {
    f64 value;
    u64 bits;
} F64Bits;

/* Unbiased binary exponent: 0 for [1,2), -1 for [0.5,1), 1024 for Inf/NaN. */
static i32 raw_exponent(u64 bits)
{
    return (i32)((bits >> F64_MANTISSA_BITS) & F64_EXPONENT_MASK) - F64_EXPONENT_BIAS;
}

static bool bits_is_zero(u64 bits)
{
    return (bits & ~F64_SIGN_MASK) == 0;
}

static f64 signed_zero(bool negative)
{
    F64Bits zero;

    zero.bits = negative ? F64_SIGN_MASK : (u64)0;
    return zero.value;
}

/*
 * Clears the mantissa bits below the integer part of a value whose exponent
 * is in [0, 51], i.e. truncates it toward zero. The caller has already ruled
 * out |x| < 1 and |x| already integral.
 */
static f64 truncate_at(u64 bits, i32 exponent)
{
    u64 mantissa_mask = ((u64)1 << (F64_MANTISSA_BITS - exponent)) - 1;
    F64Bits v;

    v.bits = bits & ~mantissa_mask;
    return v.value;
}

/* ------------------------------------------------------------------------ */
/* Number checks                                                            */
/* ------------------------------------------------------------------------ */

bool is_nan(f64 x)
{
    F64Bits v;
    u64 mantissa_mask = ((u64)1 << F64_MANTISSA_BITS) - 1;

    v.value = x;
    return ((v.bits >> F64_MANTISSA_BITS) & F64_EXPONENT_MASK) == F64_EXPONENT_MASK &&
           (v.bits & mantissa_mask) != 0;
}

bool is_finite(f64 x)
{
    F64Bits v;

    v.value = x;
    return ((v.bits >> F64_MANTISSA_BITS) & F64_EXPONENT_MASK) != F64_EXPONENT_MASK;
}

/* ------------------------------------------------------------------------ */
/* Basic operations                                                         */
/* ------------------------------------------------------------------------ */

f64 abs(f64 x)
{
    F64Bits v;

    v.value = x;
    v.bits &= ~F64_SIGN_MASK;
    return v.value;
}

f64 min(f64 a, f64 b)
{
    if (is_nan(a)) {
        return b;
    }
    if (is_nan(b)) {
        return a;
    }
    return a < b ? a : b;
}

f64 max(f64 a, f64 b)
{
    if (is_nan(a)) {
        return b;
    }
    if (is_nan(b)) {
        return a;
    }
    return a > b ? a : b;
}

f64 clamp(f64 value, f64 min_value, f64 max_value)
{
    return min(max(value, min_value), max_value);
}

f64 floor(f64 x)
{
    F64Bits v;
    u64 bits;
    bool negative;
    i32 exponent;
    u64 mantissa_mask;
    f64 truncated;

    v.value = x;
    bits = v.bits;
    negative = (bits & F64_SIGN_MASK) != 0;
    exponent = raw_exponent(bits);

    if (exponent >= F64_MANTISSA_BITS) {
        return x; /* already integral, or NaN/Inf: nothing to round */
    }
    if (exponent < 0) {
        if (bits_is_zero(bits)) {
            return x; /* preserve the sign of zero */
        }
        return negative ? (f64)-1.0 : (f64)0.0;
    }

    mantissa_mask = ((u64)1 << (F64_MANTISSA_BITS - exponent)) - 1;
    if ((bits & mantissa_mask) == 0) {
        return x; /* already integral */
    }

    truncated = truncate_at(bits, exponent);
    return negative ? truncated - (f64)1.0 : truncated;
}

f64 ceil(f64 x)
{
    F64Bits v;
    u64 bits;
    bool negative;
    i32 exponent;
    u64 mantissa_mask;
    f64 truncated;

    v.value = x;
    bits = v.bits;
    negative = (bits & F64_SIGN_MASK) != 0;
    exponent = raw_exponent(bits);

    if (exponent >= F64_MANTISSA_BITS) {
        return x;
    }
    if (exponent < 0) {
        if (bits_is_zero(bits)) {
            return x;
        }
        return negative ? signed_zero(true) : (f64)1.0;
    }

    mantissa_mask = ((u64)1 << (F64_MANTISSA_BITS - exponent)) - 1;
    if ((bits & mantissa_mask) == 0) {
        return x;
    }

    truncated = truncate_at(bits, exponent);
    return negative ? truncated : truncated + (f64)1.0;
}

f64 round(f64 x)
{
    F64Bits v;
    u64 bits;
    bool negative;
    i32 exponent;
    u64 mantissa_mask;
    u64 half_bit;
    f64 truncated;

    v.value = x;
    bits = v.bits;
    negative = (bits & F64_SIGN_MASK) != 0;
    exponent = raw_exponent(bits);

    if (exponent >= F64_MANTISSA_BITS) {
        return x;
    }
    if (exponent < -1) {
        if (bits_is_zero(bits)) {
            return x;
        }
        return signed_zero(negative); /* |x| < 0.5 rounds to 0 */
    }
    if (exponent == -1) {
        return negative ? (f64)-1.0 : (f64)1.0; /* 0.5 <= |x| < 1 */
    }

    mantissa_mask = ((u64)1 << (F64_MANTISSA_BITS - exponent)) - 1;
    half_bit = (u64)1 << (F64_MANTISSA_BITS - exponent - 1);
    if ((bits & mantissa_mask) == 0) {
        return x;
    }

    truncated = truncate_at(bits, exponent);
    if ((bits & mantissa_mask) >= half_bit) {
        return negative ? truncated - (f64)1.0 : truncated + (f64)1.0;
    }
    return truncated;
}

/* ------------------------------------------------------------------------ */
/* Geometry                                                                 */
/* ------------------------------------------------------------------------ */

/*
 * SSE2 sqrtsd is exact per IEEE 754 and, being hardware, needs no soft-float
 * support: it already returns NaN for a negative operand.
 */
f64 sqrt(f64 x)
{
    f64 result;

    __asm__("sqrtsd %1, %0" : "=x"(result) : "x"(x));
    return result;
}

f64 hypot(f64 x, f64 y)
{
    f64 largest = max(abs(x), abs(y));
    f64 smallest = min(abs(x), abs(y));
    f64 ratio;

    if (largest == (f64)0.0) {
        return (f64)0.0;
    }
    /* Scale by the largest component first so intermediate squares do not
     * overflow for large but finite x and y. */
    ratio = smallest / largest;
    return largest * sqrt((f64)1.0 + ratio * ratio);
}

/*
 * sin/cos/atan2 use the x87 transcendental instructions directly: PUNos has
 * no soft-float math library to call instead. FSIN/FCOS/FPATAN are accurate
 * for the angle ranges this library is used for (a handful of turns around
 * the circle); see README.md for the precision trade-off.
 */
f64 sin(f64 angle)
{
    f64 result;

    __asm__("fsin" : "=t"(result) : "0"(angle));
    return result;
}

f64 cos(f64 angle)
{
    f64 result;

    __asm__("fcos" : "=t"(result) : "0"(angle));
    return result;
}

f64 atan2(f64 y, f64 x)
{
    f64 result;

    __asm__("fpatan" : "=t"(result) : "0"(x), "u"(y) : "st(1)");
    return result;
}

f64 degrees_to_radians(f64 value)
{
    return value * DEG_TO_RAD;
}

f64 radians_to_degrees(f64 value)
{
    return value * RAD_TO_DEG;
}
