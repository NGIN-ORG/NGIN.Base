# Math and units

`NGIN/Units.hpp` provides compile-time dimensional quantities and unit
conversion. Arithmetic checks compatible dimensions at compile time and keeps
the representation type explicit. Prefer it at API boundaries where confusing
time, distance, rate, or storage units would be costly.

`NGIN/Math/Ratio.hpp` supplies ratio arithmetic used by conversion code.
`NGIN/Math/BigInt.hpp` provides large integer operations for workloads that
cannot fit the primitive integer domain. Use `BigInt::DivRem()` when both the
quotient and remainder are needed so division is performed once.

`NGIN/Math/Vector.hpp` and `NGIN/Math/Matrix.hpp` provide allocation-free,
fixed-size linear algebra values. Vectors support component arithmetic, dot and
three-dimensional cross products, lengths, distances, and checked
normalization. Matrices use row-major storage and support rectangular
multiplication, matrix-vector transforms, transpose, trace, determinant, and
checked inversion:

```cpp
using namespace NGIN::Math;

const Matrix3F transform {
        2.0F, 0.0F, 4.0F,
        0.0F, 3.0F, 5.0F,
        0.0F, 0.0F, 1.0F,
};
const Vector3F point {1.0F, 2.0F, 1.0F};
const Vector3F transformed = transform * point;

const auto direction = TryNormalize(Vector3F {3.0F, 4.0F, 0.0F});
const auto inverse = TryInverse(transform);
```

`TryNormalize()` returns no value for vectors whose magnitude is within the
requested tolerance of zero. `TryInverse()` similarly returns no value for
singular or tolerance-degenerate matrices. Matrix inversion requires a
field-like component type; integer matrices support element arithmetic,
multiplication, and fraction-free determinant evaluation but generally cannot
represent an inverse.

`NGIN/Math/BigFloat.hpp` provides deterministic arbitrary-precision binary
floating point with a compile-time significand precision. It is intended for
research, simulation, and other numerical work that needs more precision than
the primitive floating-point types while retaining familiar arithmetic. Its
normalized binary exponent range is -1,000,000 through +1,000,000:

```cpp
using Real = NGIN::Math::BigFloat<256>;

const Real position("1.0");
const Real velocity("0.125");
const Real timestep("0.001");
const Real next = NGIN::Math::Fma(velocity, timestep, position);
```

The default policy rounds to nearest with ties to even. Alternative directed
rounding policies are selected as the second template argument. A value carries
signed zero, infinity, and quiet NaN, but rounding state is never global or
thread-local. `Sqrt()` and `Fma()` perform one final rounding, and
`ToHexString()` provides an exact, parseable representation for diagnostics.
Decimal `ToString()` falls back to this hexadecimal representation for binary
exponents beyond its bounded decimal-conversion window, avoiding unexpectedly
large temporary allocations.

Choose representation types according to range and precision requirements;
unit typing prevents dimensional mistakes but cannot prevent numeric overflow.
For timeouts and scheduling, follow the duration types accepted by the specific
Execution, IO, or Net API rather than converting through an untyped integer.
