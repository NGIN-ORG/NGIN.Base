# SIMD

`NGIN/SIMD.hpp` exposes the supported SIMD facade: configuration tags, vector
operations, and byte-scanning helpers. Public code should use these abstractions
instead of embedding architecture intrinsics when the operation is already
available.

The scalar path defines semantics and portability. Architecture-specialized
paths must produce the same results for empty input, unaligned input, tail
elements, NaNs, infinities, and signed zero where applicable. Callers must not
assume a particular instruction set unless their build explicitly establishes
that minimum.

SIMD code is verified through independent header compilation, scalar-focused
tests, and CI builds with baseline and advanced x86 flags. ARM/NEON validation
belongs on a native ARM runner rather than through an x86 compile-only claim.
