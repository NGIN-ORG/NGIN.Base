# NGIN SIMD Facade (Preview)

This directory will host the header-only SIMD abstraction described in
`SIMDPlan.md`. The implementation is staged in phases:

1. **Scaffolding:** configuration macros, backend tags, and forward declarations.
2. **Scalar Baseline:** functional vector and mask types with a scalar backend.
3. **Vector Backends:** SSE2/AVX2/NEON traits wiring.
4. **Advanced Operations:** reductions, permutations, conversions, and math policies.

All public APIs will live directly in headers (no `*.cpp`), following the general
NGIN guidelines:

- Namespace: `NGIN::SIMD` for public surface, `NGIN::SIMD::detail` for internals.
- No hidden global state, no exceptions, no heap allocations.
- Prefer `constexpr`/`noexcept` where semantics allow.

Consumers are expected to include the top-level façade header (to be added in the
next phases) rather than backend-specific detail headers.

