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
tests, baseline-safe separately compiled ISA kernels, x86 ISA builds, and a
native ARM/NEON CI runner. An AVX-512 kernel test executes when the host supports
the complete required feature set and reports a skip otherwise.

The supported native backends are SSE2, AVX2, AVX-512F/BW/DQ/VL, and NEON.
Backend operation sets expose a compile-time
`has_native_overrides` marker so ISA builds fail when an enabled native-width
type silently inherits the complete scalar operation set. Individual operations
may still use scalar semantics; performance-sensitive call sites must be checked
with equal-workload benchmarks and generated-code inspection.

`RuntimeDispatchTable` resolves separately compiled function variants after CPU
and OS-state detection. The Foundation library ships scalar plus applicable x86
or Neon variants of the runtime byte scans. Compile-time `Vec<T>` remains
explicitly fixed-width; runtime dispatch occurs at function boundaries.
