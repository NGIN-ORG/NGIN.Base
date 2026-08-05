# Math and units

`NGIN/Units.hpp` provides compile-time dimensional quantities and unit
conversion. Arithmetic checks compatible dimensions at compile time and keeps
the representation type explicit. Prefer it at API boundaries where confusing
time, distance, rate, or storage units would be costly.

`NGIN/Math/Ratio.hpp` supplies ratio arithmetic used by conversion code.
`NGIN/Math/BigInt.hpp` provides large integer operations for workloads that
cannot fit the primitive integer domain.

Choose representation types according to range and precision requirements;
unit typing prevents dimensional mistakes but cannot prevent numeric overflow.
For timeouts and scheduling, follow the duration types accepted by the specific
Execution, IO, or Net API rather than converting through an untyped integer.
