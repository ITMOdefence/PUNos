/*
 * Runtime test of the libmath operations against DevDoc/SystemLibs/libmath.md.
 *
 * Each check has its own number; see System/SystemLibs/testing/check.h.
 * sin/cos/atan2/sqrt come from hardware transcendental instructions, so they
 * are compared with a small epsilon instead of exact equality.
 */
#define CHECK_TEST_NAME "libmath"
#include "../../testing/check.h"

#include <sys/math.h>

#define EPSILON ((f64)1e-9)

static bool approx_equal(f64 a, f64 b)
{
    return abs(a - b) <= EPSILON;
}

/* Tells +0.0 and -0.0 apart: division by zero keeps the sign in Linux's
 * default IEEE 754 rounding mode. */
static bool is_negative_zero(f64 x)
{
    return x == (f64)0.0 && (f64)1.0 / x < (f64)0.0;
}

static void test_number_checks(void)
{
    f64 nan_value = sqrt((f64)-1.0);
    f64 inf_value = (f64)1.0 / (f64)0.0;

    EXPECT(1, is_nan(nan_value) == true);
    EXPECT(2, is_nan((f64)0.0) == false);
    EXPECT(3, is_nan((f64)1.5) == false);
    EXPECT(4, is_finite((f64)0.0) == true);
    EXPECT(5, is_finite((f64)-42.5) == true);
    EXPECT(6, is_finite(inf_value) == false);
    EXPECT(7, is_finite(nan_value) == false);
    EXPECT(8, is_nan(inf_value) == false);
}

static void test_abs(void)
{
    EXPECT(10, abs((f64)0.0) == (f64)0.0);
    EXPECT(11, abs((f64)5.0) == (f64)5.0);
    EXPECT(12, abs((f64)-5.0) == (f64)5.0);
    EXPECT(13, abs((f64)-0.0) == (f64)0.0);
    EXPECT(14, is_nan(abs(sqrt((f64)-1.0))));
}

static void test_min_max(void)
{
    f64 nan_value = sqrt((f64)-1.0);

    EXPECT(20, min((f64)1.0, (f64)2.0) == (f64)1.0);
    EXPECT(21, min((f64)2.0, (f64)1.0) == (f64)1.0);
    EXPECT(22, max((f64)1.0, (f64)2.0) == (f64)2.0);
    EXPECT(23, max((f64)2.0, (f64)1.0) == (f64)2.0);
    EXPECT(24, min((f64)-1.0, (f64)-2.0) == (f64)-2.0);
    EXPECT(25, max((f64)-1.0, (f64)-2.0) == (f64)-1.0);
    /* One NaN operand: the other value wins. */
    EXPECT(26, min(nan_value, (f64)3.0) == (f64)3.0);
    EXPECT(27, min((f64)3.0, nan_value) == (f64)3.0);
    EXPECT(28, max(nan_value, (f64)3.0) == (f64)3.0);
    EXPECT(29, max((f64)3.0, nan_value) == (f64)3.0);
}

static void test_clamp(void)
{
    EXPECT(30, clamp((f64)5.0, (f64)0.0, (f64)10.0) == (f64)5.0);
    EXPECT(31, clamp((f64)-5.0, (f64)0.0, (f64)10.0) == (f64)0.0);
    EXPECT(32, clamp((f64)15.0, (f64)0.0, (f64)10.0) == (f64)10.0);
    EXPECT(33, clamp((f64)0.0, (f64)0.0, (f64)10.0) == (f64)0.0);
    EXPECT(34, clamp((f64)10.0, (f64)0.0, (f64)10.0) == (f64)10.0);
}

static void test_floor(void)
{
    EXPECT(40, floor((f64)2.0) == (f64)2.0);
    EXPECT(41, floor((f64)2.1) == (f64)2.0);
    EXPECT(42, floor((f64)2.9) == (f64)2.0);
    EXPECT(43, floor((f64)-2.1) == (f64)-3.0);
    EXPECT(44, floor((f64)-2.9) == (f64)-3.0);
    EXPECT(45, floor((f64)-2.0) == (f64)-2.0);
    EXPECT(46, floor((f64)0.5) == (f64)0.0);
    EXPECT(47, floor((f64)-0.5) == (f64)-1.0);
    EXPECT(48, floor((f64)1e300) == (f64)1e300); /* magnitude beyond the mantissa: no fraction to drop */
    EXPECT(49, !is_negative_zero(floor((f64)0.0)));
    EXPECT(50, is_negative_zero(floor((f64)-0.0)));
}

static void test_ceil(void)
{
    EXPECT(55, ceil((f64)2.0) == (f64)2.0);
    EXPECT(56, ceil((f64)2.1) == (f64)3.0);
    EXPECT(57, ceil((f64)2.9) == (f64)3.0);
    EXPECT(58, ceil((f64)-2.1) == (f64)-2.0);
    EXPECT(59, ceil((f64)-2.9) == (f64)-2.0);
    EXPECT(60, ceil((f64)-2.0) == (f64)-2.0);
    EXPECT(61, ceil((f64)0.5) == (f64)1.0);
    EXPECT(62, ceil((f64)-0.5) == (f64)0.0);
    EXPECT(63, is_negative_zero(ceil((f64)-0.5)));
    EXPECT(64, ceil((f64)1e300) == (f64)1e300);
    EXPECT(65, !is_negative_zero(ceil((f64)0.0)));
}

static void test_round(void)
{
    EXPECT(70, round((f64)2.4) == (f64)2.0);
    EXPECT(71, round((f64)2.5) == (f64)3.0);
    EXPECT(72, round((f64)2.6) == (f64)3.0);
    EXPECT(73, round((f64)-2.4) == (f64)-2.0);
    EXPECT(74, round((f64)-2.5) == (f64)-3.0);
    EXPECT(75, round((f64)-2.6) == (f64)-3.0);
    EXPECT(76, round((f64)0.5) == (f64)1.0);
    EXPECT(77, round((f64)-0.5) == (f64)-1.0);
    EXPECT(78, round((f64)0.4) == (f64)0.0);
    EXPECT(79, is_negative_zero(round((f64)-0.2)));
    EXPECT(80, !is_negative_zero(round((f64)0.2)));
    EXPECT(81, round((f64)1e300) == (f64)1e300);
}

static void test_sqrt(void)
{
    EXPECT(90, sqrt((f64)0.0) == (f64)0.0);
    EXPECT(91, sqrt((f64)4.0) == (f64)2.0);
    EXPECT(92, sqrt((f64)9.0) == (f64)3.0);
    EXPECT(93, approx_equal(sqrt((f64)2.0), (f64)1.4142135623730951));
    EXPECT(94, is_nan(sqrt((f64)-1.0)));
}

static void test_hypot(void)
{
    EXPECT(100, hypot((f64)3.0, (f64)4.0) == (f64)5.0);
    EXPECT(101, hypot((f64)0.0, (f64)0.0) == (f64)0.0);
    EXPECT(102, hypot((f64)-3.0, (f64)4.0) == (f64)5.0);
    EXPECT(103, hypot((f64)3.0, (f64)-4.0) == (f64)5.0);
    EXPECT(104, approx_equal(hypot((f64)1.0, (f64)1.0), sqrt((f64)2.0)));
}

static void test_trig(void)
{
    f64 angle = (f64)0.7;
    f64 s = sin(angle);
    f64 c = cos(angle);

    EXPECT(110, approx_equal(sin((f64)0.0), (f64)0.0));
    EXPECT(111, approx_equal(cos((f64)0.0), (f64)1.0));
    EXPECT(112, approx_equal(sin(PI / (f64)2.0), (f64)1.0));
    EXPECT(113, approx_equal(cos(PI), (f64)-1.0));
    EXPECT(114, approx_equal(sin(PI), (f64)0.0));
    EXPECT(115, approx_equal(s * s + c * c, (f64)1.0));
}

static void test_atan2(void)
{
    EXPECT(120, approx_equal(atan2((f64)0.0, (f64)1.0), (f64)0.0));
    EXPECT(121, approx_equal(atan2((f64)1.0, (f64)0.0), PI / (f64)2.0));
    EXPECT(122, approx_equal(atan2((f64)0.0, (f64)-1.0), PI));
    EXPECT(123, approx_equal(atan2((f64)-1.0, (f64)0.0), -(PI / (f64)2.0)));
    EXPECT(124, approx_equal(atan2((f64)1.0, (f64)1.0), PI / (f64)4.0));
}

static void test_degrees_radians(void)
{
    EXPECT(130, approx_equal(degrees_to_radians((f64)180.0), PI));
    EXPECT(131, approx_equal(degrees_to_radians((f64)360.0), TAU));
    EXPECT(132, approx_equal(radians_to_degrees(PI), (f64)180.0));
    EXPECT(133, approx_equal(radians_to_degrees(TAU), (f64)360.0));
    EXPECT(134, approx_equal(radians_to_degrees(degrees_to_radians((f64)37.0)), (f64)37.0));
}

static void test_nan_propagation(void)
{
    f64 nan_value = sqrt((f64)-1.0);

    EXPECT(140, is_nan(nan_value + (f64)1.0));
    EXPECT(141, is_nan(nan_value * (f64)0.0));
    EXPECT(142, is_nan(floor(nan_value)));
    EXPECT(143, is_nan(ceil(nan_value)));
    EXPECT(144, is_nan(round(nan_value)));
    EXPECT(145, is_nan(abs(nan_value)));
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    test_number_checks();
    test_abs();
    test_min_max();
    test_clamp();
    test_floor();
    test_ceil();
    test_round();
    test_sqrt();
    test_hypot();
    test_trig();
    test_atan2();
    test_degrees_radians();
    test_nan_propagation();

    check_passed();
    return 0;
}
