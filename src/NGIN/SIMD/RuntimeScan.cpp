// SPDX-License-Identifier: Apache-2.0

#include "NGIN/SIMD/Scan.hpp"

#include "NGIN/SIMD/Runtime.hpp"

namespace NGIN::SIMD
{
    namespace detail
    {
        using FindEqFunction   = auto (*)(const std::uint8_t*, std::size_t, std::uint8_t) noexcept -> std::size_t;
        using FindAny2Function = auto (*)(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;
        using FindAny3Function = auto (*)(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;
        using FindAny4Function = auto (*)(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;

        auto RuntimeFindEqByteScalar(const std::uint8_t*, std::size_t, std::uint8_t) noexcept -> std::size_t;
        auto RuntimeFindAny2ByteScalar(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;
        auto RuntimeFindAny3ByteScalar(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;
        auto RuntimeFindAny4ByteScalar(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;

#if defined(NGIN_SIMD_RUNTIME_COMPILED_SSE2)
        auto RuntimeFindEqByteSSE2(const std::uint8_t*, std::size_t, std::uint8_t) noexcept -> std::size_t;
        auto RuntimeFindAny2ByteSSE2(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;
        auto RuntimeFindAny3ByteSSE2(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;
        auto RuntimeFindAny4ByteSSE2(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;
#endif
#if defined(NGIN_SIMD_RUNTIME_COMPILED_AVX2)
        auto RuntimeFindEqByteAVX2(const std::uint8_t*, std::size_t, std::uint8_t) noexcept -> std::size_t;
        auto RuntimeFindAny2ByteAVX2(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;
        auto RuntimeFindAny3ByteAVX2(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;
        auto RuntimeFindAny4ByteAVX2(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;
#endif
#if defined(NGIN_SIMD_RUNTIME_COMPILED_AVX512)
        auto RuntimeFindEqByteAVX512(const std::uint8_t*, std::size_t, std::uint8_t) noexcept -> std::size_t;
        auto RuntimeFindAny2ByteAVX512(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;
        auto RuntimeFindAny3ByteAVX512(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;
        auto RuntimeFindAny4ByteAVX512(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;
#endif
#if defined(NGIN_SIMD_RUNTIME_COMPILED_NEON)
        auto RuntimeFindEqByteNeon(const std::uint8_t*, std::size_t, std::uint8_t) noexcept -> std::size_t;
        auto RuntimeFindAny2ByteNeon(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;
        auto RuntimeFindAny3ByteNeon(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;
        auto RuntimeFindAny4ByteNeon(const std::uint8_t*, std::size_t, std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) noexcept -> std::size_t;
#endif

        template<class Function>
        [[nodiscard]] constexpr auto MakeScanTable(Function scalar,
                                                   Function sse2,
                                                   Function avx2,
                                                   Function avx512,
                                                   Function neon) noexcept -> RuntimeDispatchTable<Function>
        {
            return RuntimeDispatchTable<Function> {scalar, sse2, avx2, avx512, neon};
        }

#if defined(NGIN_SIMD_RUNTIME_COMPILED_SSE2)
#define NGIN_SIMD_SSE2_VARIANT(Name) Name##SSE2
#else
#define NGIN_SIMD_SSE2_VARIANT(Name) nullptr
#endif
#if defined(NGIN_SIMD_RUNTIME_COMPILED_AVX2)
#define NGIN_SIMD_AVX2_VARIANT(Name) Name##AVX2
#else
#define NGIN_SIMD_AVX2_VARIANT(Name) nullptr
#endif
#if defined(NGIN_SIMD_RUNTIME_COMPILED_AVX512)
#define NGIN_SIMD_AVX512_VARIANT(Name) Name##AVX512
#else
#define NGIN_SIMD_AVX512_VARIANT(Name) nullptr
#endif
#if defined(NGIN_SIMD_RUNTIME_COMPILED_NEON)
#define NGIN_SIMD_NEON_VARIANT(Name) Name##Neon
#else
#define NGIN_SIMD_NEON_VARIANT(Name) nullptr
#endif

        const RuntimeDispatchTable<FindEqFunction> s_findEqTable =
                MakeScanTable<FindEqFunction>(RuntimeFindEqByteScalar,
                                              NGIN_SIMD_SSE2_VARIANT(RuntimeFindEqByte),
                                              NGIN_SIMD_AVX2_VARIANT(RuntimeFindEqByte),
                                              NGIN_SIMD_AVX512_VARIANT(RuntimeFindEqByte),
                                              NGIN_SIMD_NEON_VARIANT(RuntimeFindEqByte));
        const RuntimeDispatchTable<FindAny2Function> s_findAny2Table =
                MakeScanTable<FindAny2Function>(RuntimeFindAny2ByteScalar,
                                                NGIN_SIMD_SSE2_VARIANT(RuntimeFindAny2Byte),
                                                NGIN_SIMD_AVX2_VARIANT(RuntimeFindAny2Byte),
                                                NGIN_SIMD_AVX512_VARIANT(RuntimeFindAny2Byte),
                                                NGIN_SIMD_NEON_VARIANT(RuntimeFindAny2Byte));
        const RuntimeDispatchTable<FindAny3Function> s_findAny3Table =
                MakeScanTable<FindAny3Function>(RuntimeFindAny3ByteScalar,
                                                NGIN_SIMD_SSE2_VARIANT(RuntimeFindAny3Byte),
                                                NGIN_SIMD_AVX2_VARIANT(RuntimeFindAny3Byte),
                                                NGIN_SIMD_AVX512_VARIANT(RuntimeFindAny3Byte),
                                                NGIN_SIMD_NEON_VARIANT(RuntimeFindAny3Byte));
        const RuntimeDispatchTable<FindAny4Function> s_findAny4Table =
                MakeScanTable<FindAny4Function>(RuntimeFindAny4ByteScalar,
                                                NGIN_SIMD_SSE2_VARIANT(RuntimeFindAny4Byte),
                                                NGIN_SIMD_AVX2_VARIANT(RuntimeFindAny4Byte),
                                                NGIN_SIMD_AVX512_VARIANT(RuntimeFindAny4Byte),
                                                NGIN_SIMD_NEON_VARIANT(RuntimeFindAny4Byte));

#undef NGIN_SIMD_SSE2_VARIANT
#undef NGIN_SIMD_AVX2_VARIANT
#undef NGIN_SIMD_AVX512_VARIANT
#undef NGIN_SIMD_NEON_VARIANT
    }// namespace detail

    auto FindEqByteRuntime(const std::uint8_t* data, std::size_t length, std::uint8_t value) noexcept -> std::size_t
    {
        static const detail::FindEqFunction function = detail::s_findEqTable.Resolve();
        return function(data, length, value);
    }

    auto FindAnyByteRuntime(const std::uint8_t* data,
                            std::size_t         length,
                            std::uint8_t        a,
                            std::uint8_t        b) noexcept -> std::size_t
    {
        static const detail::FindAny2Function function = detail::s_findAny2Table.Resolve();
        return function(data, length, a, b);
    }

    auto FindAnyByteRuntime(const std::uint8_t* data,
                            std::size_t         length,
                            std::uint8_t        a,
                            std::uint8_t        b,
                            std::uint8_t        c) noexcept -> std::size_t
    {
        static const detail::FindAny3Function function = detail::s_findAny3Table.Resolve();
        return function(data, length, a, b, c);
    }

    auto FindAnyByteRuntime(const std::uint8_t* data,
                            std::size_t         length,
                            std::uint8_t        a,
                            std::uint8_t        b,
                            std::uint8_t        c,
                            std::uint8_t        d) noexcept -> std::size_t
    {
        static const detail::FindAny4Function function = detail::s_findAny4Table.Resolve();
        return function(data, length, a, b, c, d);
    }
}// namespace NGIN::SIMD
