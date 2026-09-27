# libmath

System Lib for numeric, geometric and trigonometric operations (`sys/math`).

Specification: `DevDoc/SystemLibs/libmath.md`. Depends on
[libcore](../libcore/README.md) for `f64`/`bool` only: `libpunos_math.a` calls
no libcore function.

## Build and test

From the repository root:

```bash
make -C System/SystemLibs/libmath check
```

Outputs:

```text
System/SystemLibs/libmath/build/libpunos_math.a   # the library
System/SystemLibs/libmath/build/math_runtime      # runtime test / QEMU self-test
```

## Usage

```c
#include <sys/math.h>

f64 clamp_and_round(f64 value)
{
    return round(clamp(value, (f64)0.0, (f64)100.0));
}
```

Compile with `-I System/SystemLibs/libmath/include` and
`-I System/SystemLibs/libcore/include`, and link `libpunos_math.a` before
`libpunos_crt.a` and `libpunos_syscall.a`. Nothing here makes a syscall, so
`libpunos_math.a` can also be linked into a program that has no Syscall
Wrappers dependency of its own.

## What the library provides

Every operation is a plain function of its arguments: no `ErrorCode`, no
`out_*` parameters, no syscalls, no mutable global state. This is different
from every other System Lib and matches the specification's operation table,
which lists bare names (`abs`, `sqrt`, `sin`, ...) rather than a `math_`
prefix.

- **Basic:** `abs`, `min`, `max`, `clamp`, `floor`, `ceil`, `round`.
- **Geometry:** `sqrt`, `hypot`, `sin`, `cos`, `atan2`, `degrees_to_radians`,
  `radians_to_degrees`.
- **Checks:** `is_nan`, `is_finite`.
- **Constants:** `PI`, `TAU`, `DEG_TO_RAD`, `RAD_TO_DEG`.

## Implementation decisions

These points are not spelled out in the specification or needed a choice;
they are fixed here and pinned by the tests.

- **Bare function names are the contract, not an oversight.** Every other
  System Lib prefixes its operations with the library's short name
  (`core_*`, `memory_*`, `time_*`, ...). `DevDoc/SystemLibs/libmath.md`
  spells out `abs(x)`, `min(a, b)`, `sqrt(x)`, etc. directly, so the header
  declares exactly those names. `-fno-builtin` (already required by every
  System Lib) keeps GCC from treating them as recognised builtins with a
  fixed signature, so `f64 abs(f64)` compiles cleanly next to a hypothetical
  future C library that might otherwise define `int abs(int)`. `clamp()`'s
  own parameters are named `min_value`/`max_value` instead of the
  specification's `min`/`max`, since those names would otherwise shadow the
  library's own `min`/`max` functions inside `clamp()`'s body; this is a
  parameter-naming choice only; the signature and order are unchanged.
- **`floor()`/`ceil()`/`round()` work on the IEEE 754 bit pattern directly**
  (sign, exponent, mantissa via a `union { f64; u64; }`), the same technique
  fdlibm uses, instead of calling into a soft-float library that does not
  exist in PUNos. `round()` breaks ties away from zero (`round(2.5) == 3`,
  `round(-2.5) == -3`), the common meaning of "round to nearest" when a tie
  rule is not specified. Signed zero is preserved end to end: `floor(-0.0)`,
  `ceil(-0.3)` and `round(-0.2)` all return `-0.0`, matched by comparing the
  sign of `1.0 / result` in the tests (`result == 0.0` alone cannot tell
  `+0.0` from `-0.0`).
- **`sqrt()` uses the SSE2 `sqrtsd` instruction** through inline assembly:
  exact per IEEE 754, needs no soft-float support, and already returns NaN
  for a negative operand without extra code.
- **`sin()`/`cos()`/`atan2()` use the legacy x87 `FSIN`/`FCOS`/`FPATAN`
  instructions** through inline assembly, following the same
  no-soft-float-library reasoning. Intel's SDM documents these as accurate
  for arguments within their internal range; this library does not perform
  extra argument range reduction before calling them. Values used within a
  handful of turns of the circle (the expected use here: angles, not
  arbitrary large numbers) are accurate to the epsilon the test uses
  (`1e-9`). A future caller that needs `sin`/`cos` of very large angles
  should reduce the argument modulo `TAU` itself first.
- **`hypot(x, y)`** factors out `max(|x|, |y|)` before squaring
  (`hypot(x,y) = m * sqrt(1 + (min/m)^2)`) instead of computing
  `sqrt(x*x + y*y)` directly, so a large but finite `x` or `y` does not
  overflow the intermediate square needlessly.
- **`min()`/`max()` propagate the non-NaN operand** when exactly one
  argument is NaN (`min(NaN, 3) == 3`), matching the common `fmin`/`fmax`
  convention; the specification does not say, so this is a choice pinned by
  the tests. Both return NaN only if both operands are NaN.
- **No dependency on wrapper or libcore code.** `libpunos_math.a` has no
  undefined symbols: it includes `<sys/core.h>` only for `f64`, `bool` and
  `NULL`, and calls no libcore function.

## Tests

- `tests/abi_compile.c`: `PI`/`TAU`/`DEG_TO_RAD`/`RAD_TO_DEG` are `f64` and
  match the specification's values; `TAU == PI + PI`.
- `tests/math_runtime.c`, check groups: `1–8` `is_nan`/`is_finite`, `10–14`
  `abs`, `20–29` `min`/`max` (including NaN propagation), `30–34` `clamp`,
  `40–50` `floor` (including signed zero and a magnitude beyond the
  mantissa), `55–65` `ceil`, `70–81` `round` (ties away from zero), `90–94`
  `sqrt`, `100–104` `hypot`, `110–115` `sin`/`cos` (including
  `sin^2 + cos^2 == 1`), `120–124` `atan2` in all four quadrants, `130–134`
  degree/radian conversions, `140–145` NaN flowing through arithmetic and
  every rounding operation unchanged.
- The test was checked against a deliberately broken version of `round()`
  (tie-breaking changed from `>=` to `>` on the fractional bit): check 71
  (`round(2.5) == 3.0`) failed as expected, and passed again once reverted.
