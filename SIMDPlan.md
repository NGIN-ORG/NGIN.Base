# NGIN SIMD Abstraction Roadmap

Goal: Deliver a portable, header-only SIMD façade under the `NGIN::SIMD` namespace that provides STL-like ergonomics, deterministic performance, and backend extensibility across desktop and mobile architectures.

The plan progresses through incremental phases, each yielding compilable artifacts and verification collateral. Phases are additive; defer optional features until prior stages are validated.

---

## Guiding Principles

- **Header-Only Core:** All public APIs live in headers under `include/NGIN/SIMD`. Source files (`*.cpp`) are reserved for tests, benchmarks, or examples.
- **Modern C++23:** Leverage `constexpr`, `consteval`, concepts, and `std::bit_cast` for zero-cost abstractions. Provide `noexcept` where guarantees hold.
- **Namespace Discipline:** Public types reside in `NGIN::SIMD`; implementation details use `NGIN::SIMD::detail`.
- **Backend Independence:** Keep algorithmic code backend-agnostic by delegating intrinsics to per-backend traits or policy objects.
- **Mask-First Design:** All lane-tail handling is explicit through mask objects to avoid hidden branches or scalar fallbacks.
- **Configurability without Chaos:** Expose compile-time knobs via macros or traits structures with sensible defaults; avoid runtime global state.
- **Deterministic Behavior:** No heap allocations, no exceptions, and no hidden global singletons. All UB resides behind documented preconditions.

---

## API Overview (Target State)

Types follow NGIN naming conventions:

- `ScalarTag`, `SSE2Tag`, `AVX2Tag`, `AVX512Tag`, `NeonTag` – backend tag types.
- `DefaultBackend` – alias resolved via `NGIN_SIMD_DEFAULT_BACKEND` (fallback per platform).
- `Vec<T, Backend = DefaultBackend, int Lanes = -1>` – fixed-size SIMD vector wrapper.
- `Mask<Lanes, Backend = DefaultBackend>` – per-lane boolean mask with bitwise operators and predicates.
- Free functions mirror STL naming: `Select`, `BitCast`, `ReduceAdd`, etc.
- Concepts (`SimdVecConcept`) ensure algorithm templates accept only SIMD-compatible types.

All math kernels (e.g., `Exp`, `Log`) plug into `MathPolicy<T, Backend, Lanes, Policy>` specializations, defaulting to `StrictMathPolicy`. Fast approximations opt in via `NGIN_SIMD_MATH_POLICY`.

---

## Implementation Phases

### Phase 0 – Scaffolding & Configuration

**Motivation:** Establish directory structure, configuration macros, and backend tags to unblock downstream work.

**Actions:**

- Create `include/NGIN/SIMD` tree with `Config.hpp`, `Tags.hpp`, and `Forward.hpp`.
- Define backend tags (`ScalarTag`, `SSE2Tag`, `AVX2Tag`, `AVX512Tag`, `NeonTag`, etc.).
- Implement macro-based selection (`NGIN_SIMD_DEFAULT_BACKEND`, `NGIN_SIMD_MATH_POLICY`).
- Provide `DefaultBackend` alias resolved via traits or platform defaults (`ScalarTag` fallback when features unavailable).
- Add README stub summarizing goals and referencing repository-wide guidelines.

**Verification:**

- Header self-checks compile under scalar configuration (`-c` dry run).
- Clang-format and clang-tidy clean.

---

### Phase 1 – Core Vector/Mask Abstractions (Scalar Backend)

**Motivation:** Deliver a portable API surface atop a scalar fallback to guarantee correctness everywhere before adding intrinsics.

**Actions:**

- Implement `Vec` and `Mask` templates with `Lanes == -1` resolved to backend-native lane counts (scalar -> 1).
- Provide constructors (`Vec(T)`, `Vec::Iota`), broadcasts, and storage operations (`Load`, `LoadAligned`, `Store`, `StoreAligned`, masked variants).
- Implement gather/scatter using scalar loops (checked at compile-time via concepts).
- Define mask operations (`~`, `&`, `|`, `^`, `Any`, `All`, `None`) using `std::array<bool, Lanes>` or bitset analog.
- Specialize `lane_count_v<Vec>` and compile-time constants.
- Add `BitCast`, `Select`, and arithmetic/comparison operators delegating to scalar implementation.

**Verification:**

- Unit tests comparing `Vec` vs scalar loops on representative kernels (SAXPY, mask tails).
- Static assertions for layout (`std::is_trivially_copyable_v`).

---

### Phase 2 – Backend Traits & Intrinsics Wiring (x86 SSE2/AVX2)

**Motivation:** Introduce high-performance backends while preserving scalar behavior as reference.

**Actions:**

- Create `detail::BackendTraits<Backend, T>` describing `native_lanes`, `register_type`, and `load/store` helpers.
- Implement SSE2 and AVX2 traits using compiler intrinsics (`<emmintrin.h>`, `<immintrin.h>`). Respect alignment and provide masked load/store using blend instructions or fallback loops.
- Extend `Vec` operations to forward to `detail` traits when backend supports vector widths >1.
- Ensure arithmetic, comparisons, bitwise, shifts, and horizontal reductions translate to intrinsic sequences.
- Provide compile-time detection macros for backend availability and static assertion if the TU requests unsupported backend without guards.

**Verification:**

- Unit tests under `-msse2` and `-mavx2`, validating numerical equivalence to scalar path.
- Microbench (Google Benchmark or existing harness) for loads/stores, arithmetic, reductions.
- Build farm matrix (if available) cross-checking GCC/Clang/MSVC.

---

### Phase 3 – ARM NEON Backend

**Motivation:** Supply parity for ARM64 and mobile targets.

**Actions:**

- Implement `detail::BackendTraits<NeonTag, T>` using `<arm_neon.h>`.
- Address lane count differences (e.g., 4-wide float, 2-wide double) via specialization.
- Handle masked operations via vector selects or fallback to scalar loops when hardware lacks direct mask support.
- Verify gather/scatter decisions (may remain scalar due to limited NEON support; document behavior).

**Verification:**

- Cross-compile or CI run with `-mfpu=neon` / `-march=armv8-a`.
- Differential tests vs scalar path.

---

### Phase 4 – Advanced Operations & Policies

**Motivation:** Enrich API with common SIMD utilities and configurable math kernels.

**Actions:**

- Implement fused multiply-add (`Fma`), `Min`, `Max`, `Abs`, `AndNot`, `Reverse`, `ZipLo`, `ZipHi`, and lane permutations (portable subset).
- Add `ReduceAdd`, `ReduceMin`, `ReduceMax` with optimized shuffle trees per backend.
- Provide math policy scaffolding: default strict policy defers to scalar `std::` algorithms; fast policy hooks left for future specialization.
- Introduce conversion routines with `Exact`, `Saturate`, `Truncate` modes.
- Deliver tail helpers (`FirstNMask`) and convenience algorithms (`ForEachSimd`) leveraging masks.

**Verification:**

- Expand test suite: arithmetic identities, reduction correctness, conversion edge cases (NaN, infinities, saturation).
- Benchmark tail-handling loops vs scalar to confirm minimal overhead.

---

### Phase 5 – Runtime Dispatch & Integration

**Motivation:** Ease adoption in NGIN components by providing detection and multi-version wrappers.

**Actions:**

- Implement `DetectBackend()` returning `CpuCaps` enum reflecting available ISA at runtime (platform-specific CPUID / `getauxval` handling).
- Provide helper macros or templates for dispatching algorithms (`DispatchSimd`).
- Add usage examples (SAXPY, ReLU, reductions) in `examples/` or documentation.
- Update `NGIN.SIMD` README with guidance on compile flags, preconditions, and extension hooks.

**Verification:**

- End-to-end example builds with runtime dispatch selecting expected backend.
- Stress tests in mixed backends (force scalar, SSE2, AVX2) to validate consistent outputs.

---

### Phase 6 – Optional Extensions (Future Work)

- **AVX-512 & SVE Backends:** Follow established traits pattern once host toolchains stabilize.
- **Math Kernels:** Provide tuned approximations (`Exp`, `Log`, `Sin`, `Cos`) under `FastMathPolicy`.
- **Complex Helpers:** Implement `Interleave2`/`Deinterleave2`, convolution-friendly shuffles.
- **WASM Backend:** For browser or sandboxed environments, using WASM SIMD intrinsics.

Each extension should ship with tests, benchmarks, and documentation updates.

---

## Testing & Tooling Checklist

- Positive & negative unit tests per feature (masked loads, gathers, reductions).
- Property-based tests comparing SIMD vs scalar reference for random inputs, covering NaN/Inf/denormal handling.
- Benchmarks capturing throughput per backend and operation category (loads/stores, arithmetic, reductions).
- Static analysis (clang-tidy) and sanitizers (UBSan) on scalar backend; ASan-friendly when using vectorized loads/stores.
- Continuous integration matrix covering GCC, Clang, and MSVC with appropriate ISA flags.

---

## Documentation Deliverables

- `include/NGIN/SIMD/README.md` summarizing purpose, API overview, backend support, and configuration macros.
- Doxygen comments for public types/functions in headers.
- Example snippets demonstrating tail masks, runtime dispatch, and conversions.
- CHANGELOG entries per phase summarizing new APIs and backend availability.

---

## Integration Notes

- Coordinate with `NGIN::Containers` roadmap (e.g., `ConcurrentHashMap`) to ensure SIMD helpers align with planned Phase 4/5 optimizations.
- Avoid leaking `<immintrin.h>` into consumer code by keeping includes within implementation headers.
- Maintain ABI safety by ensuring all inline functions remain in headers and template instantiations do not introduce hidden statics.

---

## Rollback Strategy

- Scalar backend serves as safe baseline; any backend can be disabled via configuration macro (`NGIN_SIMD_DISABLE_AVX2`, etc.) without breaking API compatibility.
- Major experimental features (e.g., fast math kernels) gated behind macros to allow rapid disablement if regressions occur.

---

## Next Immediate Steps

1. Prototype backend-aware `FastMathPolicy` implementations for SSE2/AVX2 floats (polynomial + intrinsic blend) using the new `MathPolicyLane<FastMathPolicy, Backend, T>` hook.
2. Extend the unit test matrix with targeted fast-math verification (relative error tolerances per function, NaN/inf propagation checks).
3. Document math policy usage in `include/NGIN/SIMD/README.md`, including guidance on selecting `NGIN_SIMD_MATH_POLICY` and expected accuracy/throughput trade-offs.
3. Schedule design review before embarking on Phase 1 implementation to confirm API surface aligns with NGIN expectations.

```cpp
#include <NGIN/SIMD/Vec.hpp>
#include <NGIN/SIMD/Mask.hpp>
#include <array>

namespace NGIN::SIMD {

void Saxpy(float alpha, const float* x, const float* y, float* out, std::size_t count) noexcept {
    using Vec = Vec<float>;                // chooses DefaultBackend + native lanes
    constexpr auto lanes = Vec::NativeLanes;
    const Vec alphaVec{alpha};

    std::size_t index = 0;
    for (; index + lanes <= count; index += lanes) {
        const Vec xVec = Vec::LoadUnaligned(x + index);
        const Vec yVec = Vec::LoadUnaligned(y + index);
        Vec::StoreUnaligned(out + index, alphaVec * xVec + yVec);
    }

    if (index < count) {
        const auto tailMask = Mask<lanes>::FirstN(count - index);
        const Vec xVec = Vec::MaskedLoad(x + index, tailMask, Vec{});
        const Vec yVec = Vec::MaskedLoad(y + index, tailMask, Vec{});
        Vec::MaskedStore(out + index, tailMask, alphaVec * xVec + yVec);
    }
}

} // namespace NGIN::SIMD
```
