# NGIN SIMD Façade

The NGIN SIMD layer delivers a header-only, backend-agnostic vector API that keeps
the ergonomics of ordinary C++ value types while letting each translation unit
select an available instruction set. All public types live in `NGIN::SIMD`.
Native-width operations use SSE2, AVX2, AVX-512, or NEON overrides where
implemented; other lane counts and operations intentionally inherit the scalar
reference path. The linked Foundation library also provides runtime CPU/OS
detection and separately compiled byte-scan kernels.

---

## Quick Start

```cpp
#include <NGIN/SIMD.hpp>

using namespace NGIN::SIMD;

void Saxpy(float alpha, const float* x, const float* y, float* out, std::size_t count) noexcept
{
    using VecF    = Vec<float>;                 // resolves to DefaultBackend + native lanes
    using MaskF   = VecF::mask_type;
    const auto ax = VecF {alpha};

    std::size_t index = 0;
    for (; index + VecF::lanes <= count; index += VecF::lanes)
    {
        const auto xv = VecF::Load(x + index);
        const auto yv = VecF::Load(y + index);
        (ax * xv + yv).Store(out + index);
    }

    if (index < count)
    {
        MaskF tail {};
        for (int lane = 0; lane < VecF::lanes; ++lane)
        {
            tail.SetLane(lane, index + static_cast<std::size_t>(lane) < count);
        }
        const auto xv = VecF::Load(x + index, tail, 0.0F);
        const auto yv = VecF::Load(y + index, tail, 0.0F);
        (ax * xv + yv).Store(out + index, tail);
    }
}
```

Key points:

- `Vec<T>` automatically picks the compile-time default backend if `Lanes == -1`.
- Masks are explicit values; backend operations without a native override use
  the documented scalar reference implementation.
- All operations are pure, constexpr-friendly, and avoid heap allocations.

---

## Backend Matrix

| Tag          | Typical float lanes | Availability                          | Notes |
|--------------|---------------------|----------------------------------------|-------|
| `ScalarTag`  | 1                   | Always                                 | Reference implementation used for tests and fallback |
| `SSE2Tag`    | 4 (float) / 2 (double) | Enabled when the compiler target exposes SSE2 and not disabled | Masks use packed compares; supports integer vectors |
| `AVX2Tag`    | 8 (float) / 4 (double) | Enabled when the compiler target exposes AVX2 and not disabled | Native-width float, double, 32-bit integer, and byte overrides |
| `AVX512Tag`  | 16 (float) / 8 (double) | Requires AVX-512F/BW/DQ/VL and not disabled | Native-width float, double, 32-bit integer, and 64-byte overrides |
| `NeonTag`    | 4 (float)            | Enabled when the compiler target exposes NEON and not disabled | Focused on AArch64/AArch32 little-endian |

Backends can be disabled via macros (see below) without breaking the API. Each
translation unit may explicitly select a backend by instantiating
`Vec<T, Backend>` with a concrete tag.

---

## Configuration Macros

All knobs live in `NGIN/SIMD/Config.hpp` and should be defined before including
`NGIN/SIMD.hpp`:

| Macro | Default | Purpose |
|-------|---------|---------|
| `NGIN_SIMD_DEFAULT_BACKEND` | Auto-detected (`AVX512`, `AVX2`, `SSE2`, `Neon`, else `Scalar`) | Controls the backend chosen by `Vec<T>` when lane count is `-1`. Override to force scalar code or a specific ISA. |
| `NGIN_SIMD_MATH_POLICY` | `::NGIN::SIMD::StrictMathPolicy` | Sets the default math policy used by `Exp`, `Log`, `Sin`, `Cos`, `Sqrt`, etc. |
| `NGIN_SIMD_DISABLE_AVX512` / `NGIN_SIMD_DISABLE_AVX2` / `NGIN_SIMD_DISABLE_SSE2` / `NGIN_SIMD_DISABLE_NEON` | `0` | Hard-disable a supported backend even if the compiler exposes the intrinsics. Useful for A/B testing or toolchain workarounds. |

The AVX-512 backend deliberately requires F, BW, DQ, and VL as one contract so
all advertised float, integer, and byte native-width operations are available.
Compiling a translation unit for AVX-512 still sets a hard minimum ISA for that
translation unit.

---

## Runtime Dispatch

`Runtime.hpp` exposes CPU and operating-system feature detection plus
`RuntimeDispatchTable<FunctionPointer>`. Put each ISA implementation in a
translation unit compiled with its own ISA flags, then resolve the table at the
function boundary. Null variants are skipped and the scalar entry is the
portable fallback.

```cpp
RuntimeDispatchTable<Kernel> kernels {scalar, sse2, avx2, avx512, neon};
Kernel selected = kernels.Resolve();
selected(input, output, count);
```

This avoids a dynamic-width vector ABI: `Vec<T, Backend>` remains a normal
compile-time type inside each kernel. Runtime detection accounts for x86 OS
save/restore state as well as CPU feature bits. AArch64 Advanced SIMD is
mandatory; Linux AArch32 uses HWCAP detection.

The byte-search helpers have ready-made `FindEqByteRuntime` and
`FindAnyByteRuntime` overloads. They resolve once and call the best kernel linked
into Foundation.

You can also override policies per call site:

```cpp
const auto fastExp  = Exp<FastMathPolicy>(vec);
const auto strictLn = Log<StrictMathPolicy>(vec);
```

---

## Math Policies

Math policies govern transcendental functions lane-by-lane. Today the façade
exposes two policies:

- **`StrictMathPolicy` (default):** Promotes to long double (or the closest
  equivalent) and forwards to `<cmath>`. Maximizes accuracy and determinism.
- **`FastMathPolicy`:** Provides approximation-oriented scalar lane kernels for
  SSE2, AVX2, and AVX-512 float vectors. The compiler may vectorize them, but
  callers should verify generated code for performance-critical kernels. Other
  backends currently fall back to strict semantics.

Accuracy expectations for the x86 implementation:

| Function | Relative error (max, vs strict) |
|----------|----------------------------------|
| `Exp`    | ≤ 1e-3                           |
| `Log`    | ≤ 1e-2                           |
| `Sin/Cos`| ≤ 2e-3                           |
| `Sqrt`   | ≤ 5e-4                           |

Special values (NaN, ±Inf, negative inputs for `Sqrt`, etc.) follow IEEE-754
expectations. Unit tests under `tests/SIMD/VecScalarTests.cpp` include accuracy
and propagation coverage for both policies.

---

## Recommended Workflow

1. **Pick a backend policy:** either rely on compile-time target detection or
   define `NGIN_SIMD_DEFAULT_BACKEND` for consistency across translation units.
2. **Prototype with the scalar backend:** the scalar baseline is trivial to
   step through in a debugger and simplifies testing.
3. **Gate platform-specific code:** isolate any intrinsic-suspicious logic in
   `NGIN::SIMD::detail` specializations to keep public headers orthogonal.
4. **Test with multiple instruction sets:** the test suite can be compiled with
   `-msse2`, `-mavx2`, AVX-512F/BW/DQ/VL, or NEON flags to validate
   equivalence. Execute ISA-specific binaries only on compatible hardware.

---

## Benchmarks & Telemetry

- `benchmarks/SIMDFastMathBench.cpp` compares equal-size plain loops, scalar
  vectors, and strict/fast policies for `Exp`, `Log`, `Sin`, `Cos`, and `Sqrt`
  across available backends.
- `benchmarks/SIMDRuntimeDispatchBench.cpp` compares equal-work scalar and
  runtime-selected scans and prints the selected backend.
- Results integrate with the existing `NGIN::Benchmark` harness so you can run
  `SIMDFastMathBench` alongside other microbenchmarks.

When sharing benchmark data, include the compiler version, target ISA flags, and
the configured math policy. The benchmark is diagnostic and does not impose a
portable performance threshold.

---

## Further Reading

- `tests/SIMD/VecScalarTests.cpp` – comprehensive examples covering loads,
  gathers, comparisons, conversions, and math policies.

Feel free to extend this README as new operations, policies, or backends are
introduced.
