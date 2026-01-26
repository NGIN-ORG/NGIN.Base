#include "catch2/catch_approx.hpp"
#include "catch2/catch_test_macros.hpp"

#include "NGIN/SIMD.hpp"
#include "NGIN/SIMD/detail/BackendTraits.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>
#include <type_traits>

using namespace NGIN::SIMD;

namespace
{

    template<class Backend, int Lanes>
    [[nodiscard]] constexpr auto MakeMaskFromInitializer(std::initializer_list<bool> values) noexcept
            -> Mask<Lanes, Backend>
    {
        Mask<Lanes, Backend> mask;
        int                  index = 0;
        for (const bool value: values)
        {
            if (index >= Lanes)
            {
                break;
            }
            mask.SetLane(index++, value);
        }
        return mask;
    }

    [[nodiscard]] constexpr auto RelativeError(float approx, float reference, float floor = 1e-6F) noexcept -> float
    {
        const auto absf = [](float x) constexpr noexcept { return x < 0.0F ? -x : x; };
        const auto maxf = [](float a, float b) constexpr noexcept { return a < b ? b : a; };

        const float denom = maxf(absf(reference), floor);
        return absf((approx - reference) / denom);
    }

    template<class Backend, std::size_t Count>
    [[nodiscard]] auto MakeInputVector(const std::array<float, Count>& source) noexcept -> std::array<float, Vec<float, Backend>::lanes>
    {
        using VecType = Vec<float, Backend>;
        std::array<float, VecType::lanes> values {};
        for (int lane = 0; lane < VecType::lanes; ++lane)
        {
            values[static_cast<std::size_t>(lane)] = source[static_cast<std::size_t>(lane % source.size())];
        }
        return values;
    }

    template<class Backend>
    void ValidateFastMathApproximation() noexcept
    {
        using Vecf = Vec<float, Backend>;
        using VecRef = Vec<float, ScalarTag, Vecf::lanes>;
        static_assert(VecRef::lanes == Vecf::lanes);

        constexpr std::array<float, 8> expSamples {-5.0F, -1.0F, -0.5F, 0.0F, 0.5F, 1.0F, 2.0F, 3.5F};
        constexpr std::array<float, 8> logSamples {0.125F, 0.5F, 0.75F, 1.0F, 1.5F, 2.0F, 4.0F, 10.0F};
        constexpr std::array<float, 8> trigSamples {-6.0F, -3.14159F, -1.0F, -0.5F, 0.25F, 1.0F, 2.5F, 6.0F};
        constexpr std::array<float, 8> sqrtSamples {0.0F, 0.25F, 0.5F, 1.0F, 2.0F, 4.0F, 9.0F, 16.0F};

        const auto expInputData  = MakeInputVector<Backend>(expSamples);
        const auto logInputData  = MakeInputVector<Backend>(logSamples);
        const auto trigInputData = MakeInputVector<Backend>(trigSamples);
        const auto sqrtInputData = MakeInputVector<Backend>(sqrtSamples);

        const auto expInput  = Vecf::Load(expInputData.data());
        const auto logInput  = Vecf::Load(logInputData.data());
        const auto trigInput = Vecf::Load(trigInputData.data());
        const auto sqrtInput = Vecf::Load(sqrtInputData.data());

        const auto expRefInput  = VecRef::Load(expInputData.data());
        const auto logRefInput  = VecRef::Load(logInputData.data());
        const auto trigRefInput = VecRef::Load(trigInputData.data());
        const auto sqrtRefInput = VecRef::Load(sqrtInputData.data());

        const auto fastExp    = Exp<FastMathPolicy>(expInput);
        const auto fastLog    = Log<FastMathPolicy>(logInput);
        const auto fastSin    = Sin<FastMathPolicy>(trigInput);
        const auto fastCos    = Cos<FastMathPolicy>(trigInput);
        const auto fastSqrt   = Sqrt<FastMathPolicy>(sqrtInput);

        const auto referenceExp  = Exp<StrictMathPolicy>(expRefInput);
        const auto referenceLog  = Log<StrictMathPolicy>(logRefInput);
        const auto referenceSin  = Sin<StrictMathPolicy>(trigRefInput);
        const auto referenceCos  = Cos<StrictMathPolicy>(trigRefInput);
        const auto referenceSqrt = Sqrt<StrictMathPolicy>(sqrtRefInput);

        constexpr float expTolerance  = 1e-3F;
        constexpr float logTolerance  = 1e-2F;
        constexpr float trigTolerance = 2e-3F;
        constexpr float sqrtTolerance = 5e-4F;

        for (int lane = 0; lane < Vecf::lanes; ++lane)
        {
            CHECK(RelativeError(fastExp.GetLane(lane), referenceExp.GetLane(lane)) <= expTolerance);
            CHECK(RelativeError(fastLog.GetLane(lane), referenceLog.GetLane(lane)) <= logTolerance);
            CHECK(RelativeError(fastSin.GetLane(lane), referenceSin.GetLane(lane)) <= trigTolerance);
            CHECK(RelativeError(fastCos.GetLane(lane), referenceCos.GetLane(lane)) <= trigTolerance);
            CHECK(RelativeError(fastSqrt.GetLane(lane), referenceSqrt.GetLane(lane)) <= sqrtTolerance);
        }
    }

    template<class Backend>
    void ValidateFastMathSpecialValues()
    {
        using Vecf = Vec<float, Backend>;

        Vecf expInput {};
        const auto positiveInf = std::numeric_limits<float>::infinity();
        const auto negativeInf = -std::numeric_limits<float>::infinity();
        const auto quietNan    = std::numeric_limits<float>::quiet_NaN();

        for (int lane = 0; lane < Vecf::lanes; ++lane)
        {
            if (lane == 0)
            {
                expInput.SetLane(lane, positiveInf);
            }
            else if (lane == 1)
            {
                expInput.SetLane(lane, negativeInf);
            }
            else if (lane == 2)
            {
                expInput.SetLane(lane, quietNan);
            }
            else
            {
                expInput.SetLane(lane, 0.0F);
            }
        }

        const auto fastExp = Exp<FastMathPolicy>(expInput);
        CHECK(std::isinf(fastExp.GetLane(0)));
        CHECK(fastExp.GetLane(1) == Catch::Approx(0.0F));
        CHECK(std::isnan(fastExp.GetLane(2)));

        Vecf logInput {};
        logInput.SetLane(0, -1.0F);
        logInput.SetLane(1, 0.0F);
        logInput.SetLane(2, positiveInf);
        logInput.SetLane(3, quietNan);
        const auto fastLog = Log<FastMathPolicy>(logInput);
        CHECK(std::isnan(fastLog.GetLane(0)));
        CHECK(std::isinf(fastLog.GetLane(1)));
        CHECK(fastLog.GetLane(1) < 0.0F);
        CHECK(std::isinf(fastLog.GetLane(2)));
        CHECK(std::isnan(fastLog.GetLane(3)));

        Vecf trigInput {};
        trigInput.SetLane(0, positiveInf);
        trigInput.SetLane(1, quietNan);
        trigInput.SetLane(2, 0.0F);
        const auto fastSin = Sin<FastMathPolicy>(trigInput);
        const auto fastCos = Cos<FastMathPolicy>(trigInput);
        CHECK(std::isnan(fastSin.GetLane(0)));
        CHECK(std::isnan(fastSin.GetLane(1)));
        CHECK(std::isnan(fastCos.GetLane(0)));
        CHECK(std::isnan(fastCos.GetLane(1)));
        CHECK(fastSin.GetLane(2) == Catch::Approx(0.0F));
        CHECK(fastCos.GetLane(2) == Catch::Approx(1.0F));

        Vecf sqrtInput {};
        sqrtInput.SetLane(0, -1.0F);
        sqrtInput.SetLane(1, 0.0F);
        sqrtInput.SetLane(2, positiveInf);
        sqrtInput.SetLane(3, 4.0F);
        const auto fastSqrt = Sqrt<FastMathPolicy>(sqrtInput);
        CHECK(std::isnan(fastSqrt.GetLane(0)));
        CHECK(fastSqrt.GetLane(1) == Catch::Approx(0.0F));
        CHECK(std::isinf(fastSqrt.GetLane(2)));
        CHECK(fastSqrt.GetLane(3) == Catch::Approx(2.0F));
    }

}// namespace

static_assert(detail::BackendTraits<ScalarTag, float>::native_lanes == 1);
static_assert(std::is_same_v<Vec<float, ScalarTag, 4>::storage_type,
                             detail::BackendTraits<ScalarTag, float>::template Storage<4>>);

TEST_CASE("Vec scalar load/store round trip")
{
    using Vec4f = Vec<float, ScalarTag, 4>;
    static_assert(Vec4f::lanes == 4);
    static_assert(std::is_trivially_copyable_v<Vec4f>);

    std::array<float, Vec4f::lanes> source {1.0F, 2.0F, -3.5F, 4.25F};
    const auto                      vector = Vec4f::Load(source.data());

    for (int lane = 0; lane < Vec4f::lanes; ++lane)
    {
        CHECK(vector.GetLane(lane) == source[static_cast<std::size_t>(lane)]);
    }

    std::array<float, Vec4f::lanes> roundTrip {};
    vector.Store(roundTrip.data());
    CHECK(roundTrip == source);
}

TEST_CASE("Vec scalar masked load/store")
{
    using Vec4f = Vec<float, ScalarTag, 4>;
    const std::array<float, Vec4f::lanes> source {5.0F, 6.0F, 7.0F, 8.0F};
    const auto                            mask = MakeMaskFromInitializer<ScalarTag, Vec4f::lanes>({true, true, false, false});

    const auto loaded = Vec4f::Load(source.data(), mask, -1.0F);
    CHECK(loaded.GetLane(0) == 5.0F);
    CHECK(loaded.GetLane(1) == 6.0F);
    CHECK(loaded.GetLane(2) == -1.0F);
    CHECK(loaded.GetLane(3) == -1.0F);

    std::array<float, Vec4f::lanes> destination {-1.0F, -1.0F, -1.0F, -1.0F};
    loaded.Store(destination.data(), mask);
    CHECK(destination[0] == 5.0F);
    CHECK(destination[1] == 6.0F);
    CHECK(destination[2] == -1.0F);
    CHECK(destination[3] == -1.0F);
}

TEST_CASE("Vec scalar arithmetic and reductions")
{
    using Vec4i      = Vec<int, ScalarTag, 4>;
    const auto left  = Vec4i::Iota(1, 1);// 1,2,3,4
    const auto right = Vec4i::Iota(5, 2);// 5,7,9,11

    const auto sum     = left + right;
    const auto diff    = right - left;
    const auto product = left * right;

    CHECK(sum.GetLane(0) == 6);
    CHECK(sum.GetLane(3) == 15);

    CHECK(diff.GetLane(0) == 4);
    CHECK(diff.GetLane(3) == 7);

    CHECK(product.GetLane(0) == 5);
    CHECK(product.GetLane(2) == 27);

    const auto fused = Fma(left, right, Vec4i {1});
    CHECK(fused.GetLane(0) == left.GetLane(0) * right.GetLane(0) + 1);

    CHECK(ReduceAdd(left) == 10);
    CHECK(ReduceMin(left) == 1);
    CHECK(ReduceMax(left) == 4);
}

TEST_CASE("Vec scalar gather/scatter with mask")
{
    using Vec4f    = Vec<float, ScalarTag, 4>;
    using IndexVec = Vec<int, ScalarTag, 4>;

    const std::array<float, 8> source {0.5F, -1.0F, 1.5F, 2.5F, 3.5F, 4.5F, 5.5F, 6.5F};
    const auto                 indices  = IndexVec::Iota(1, 2);// 1,3,5,7
    const auto                 gathered = Vec4f::Gather(source.data(), indices);

    CHECK(gathered.GetLane(0) == -1.0F);
    CHECK(gathered.GetLane(3) == 6.5F);

    std::array<float, 8> destination {};
    destination.fill(9.0F);

    const auto mask = MakeMaskFromInitializer<ScalarTag, Vec4f::lanes>({true, false, true, false});
    gathered.Scatter(destination.data(), indices, mask);

    CHECK(destination[1] == -1.0F);
    CHECK(destination[5] == 4.5F);
    CHECK(destination[3] == 9.0F);
    CHECK(destination[7] == 9.0F);
}

TEST_CASE("Mask operations and predicates")
{
    const auto anyMask = MakeMaskFromInitializer<ScalarTag, 4>({true, false, false, false});
    CHECK(Any(anyMask));
    CHECK_FALSE(All(anyMask));
    CHECK_FALSE(None(anyMask));

    const auto allMask = MakeMaskFromInitializer<ScalarTag, 4>({true, true, true, true});
    CHECK(All(allMask));

    const auto noneMask = MakeMaskFromInitializer<ScalarTag, 4>({false, false, false, false});
    CHECK(None(noneMask));

    const auto firstThree = FirstNMask<4, ScalarTag>(3);
    CHECK(firstThree.GetLane(0));
    CHECK(firstThree.GetLane(2));
    CHECK_FALSE(firstThree.GetLane(3));

    const auto inverted = ~firstThree;
    CHECK_FALSE(inverted.GetLane(0));
    CHECK(inverted.GetLane(3));

    const auto combined = (anyMask | firstThree) ^ allMask;
    CHECK_FALSE(combined.GetLane(0));
    CHECK_FALSE(combined.GetLane(1));
    CHECK_FALSE(combined.GetLane(2));
    CHECK(combined.GetLane(3));
}

TEST_CASE("MaskToBits encodes lane bits")
{
    const auto mask = MakeMaskFromInitializer<ScalarTag, 8>({true, false, true, true, false, false, false, true});
    CHECK(MaskToBits(mask) == 0x8D);
}

TEST_CASE("SIMD byte scan helpers")
{
    constexpr std::array<std::uint8_t, 16> data {
            static_cast<std::uint8_t>('a'),
            static_cast<std::uint8_t>('b'),
            static_cast<std::uint8_t>('c'),
            static_cast<std::uint8_t>('d'),
            static_cast<std::uint8_t>('e'),
            static_cast<std::uint8_t>('f'),
            static_cast<std::uint8_t>('g'),
            static_cast<std::uint8_t>('h'),
            static_cast<std::uint8_t>('i'),
            static_cast<std::uint8_t>('j'),
            static_cast<std::uint8_t>('k'),
            static_cast<std::uint8_t>('l'),
            static_cast<std::uint8_t>('m'),
            static_cast<std::uint8_t>('n'),
            static_cast<std::uint8_t>('o'),
            static_cast<std::uint8_t>('p'),
    };

    CHECK(FindEqByte<ScalarTag>(data.data(), data.size(), static_cast<std::uint8_t>('a')) == 0);
    CHECK(FindEqByte<ScalarTag>(data.data(), data.size(), static_cast<std::uint8_t>('m')) == 12);
    CHECK(FindEqByte<ScalarTag>(data.data(), data.size(), static_cast<std::uint8_t>('z')) == data.size());

    CHECK(FindAnyByte<ScalarTag>(data.data(), data.size(), static_cast<std::uint8_t>('q'), static_cast<std::uint8_t>('b')) == 1);
    CHECK(FindAnyByte<ScalarTag>(data.data(), data.size(),
                                 static_cast<std::uint8_t>('x'),
                                 static_cast<std::uint8_t>('y'),
                                 static_cast<std::uint8_t>('o'))
          == 14);
    CHECK(FindAnyByte<ScalarTag>(data.data(), data.size(),
                                 static_cast<std::uint8_t>('x'),
                                 static_cast<std::uint8_t>('y'),
                                 static_cast<std::uint8_t>('z'),
                                 static_cast<std::uint8_t>('p'))
          == 15);
}

TEST_CASE("Vec utilities Select Reverse Zip")
{
    using Vec4i     = Vec<int, ScalarTag, 4>;
    const auto a    = Vec4i::Iota(0, 1); // 0,1,2,3
    const auto b    = Vec4i::Iota(10, 1);// 10,11,12,13
    const auto mask = MakeMaskFromInitializer<ScalarTag, 4>({true, false, true, false});

    const auto selected = Select(mask, a, b);
    CHECK(selected.GetLane(0) == 0);
    CHECK(selected.GetLane(1) == 11);
    CHECK(selected.GetLane(2) == 2);
    CHECK(selected.GetLane(3) == 13);

    const auto reversed = Reverse(a);
    CHECK(reversed.GetLane(0) == 3);
    CHECK(reversed.GetLane(3) == 0);

    const auto zipLo = ZipLo(a, b);
    CHECK(zipLo.GetLane(0) == 0);
    CHECK(zipLo.GetLane(1) == 10);
    CHECK(zipLo.GetLane(3) == 11);

    const auto zipHi = ZipHi(a, b);
    CHECK(zipHi.GetLane(0) == 2);
    CHECK(zipHi.GetLane(1) == 12);
    CHECK(zipHi.GetLane(3) == 13);
}

TEST_CASE("Vec bitwise helpers")
{
    using Vec4u    = Vec<std::uint32_t, ScalarTag, 4>;
    const auto lhs = Vec4u::Iota(0x0F0F0F0Fu, 0x10101010u);
    const auto rhs = Vec4u::Iota(0x00FF00FFu, 0u);

    const auto band   = lhs & rhs;
    const auto bor    = lhs | rhs;
    const auto bxor   = lhs ^ rhs;
    const auto andNot = AndNot(lhs, rhs);

    CHECK((band.GetLane(0) & 0xFFFFFFFFu) == 0x000F000Fu);
    CHECK((bor.GetLane(1) & 0xFFFFFFFFu) == (lhs.GetLane(1) | rhs.GetLane(1)));
    CHECK((bxor.GetLane(2) & 0xFFFFFFFFu) == (lhs.GetLane(2) ^ rhs.GetLane(2)));
    CHECK((andNot.GetLane(3) & 0xFFFFFFFFu) == (lhs.GetLane(3) & ~rhs.GetLane(3)));

    const auto shiftedLeft  = Shl(lhs, 4);
    const auto shiftedRight = Shr(lhs, 4);
    CHECK(shiftedLeft.GetLane(0) == lhs.GetLane(0) << 4);
    CHECK(shiftedRight.GetLane(0) == lhs.GetLane(0) >> 4);
}

TEST_CASE("Vec scalar conversions")
{
    using FloatVec = Vec<float, ScalarTag, 4>;
    using IntVec   = Vec<int, ScalarTag, FloatVec::lanes>;

    const auto integerFloats = FloatVec::Iota(0.0F, 1.0F);
    const auto exactInts     = Convert<int>(integerFloats);

    for (int lane = 0; lane < IntVec::lanes; ++lane)
    {
        CHECK(exactInts.GetLane(lane) == lane);
    }

    const auto integers = IntVec::Iota(0, 2);
    const auto floats   = Convert<float>(integers);
    for (int lane = 0; lane < IntVec::lanes; ++lane)
    {
        CHECK(floats.GetLane(lane) == Catch::Approx(static_cast<float>(lane * 2)));
    }

    const std::array<float, FloatVec::lanes> fractionalValues {0.5F, 1.2F, -5.8F, 260.0F};
    const auto                               fractional = FloatVec::Load(fractionalValues.data());

    const auto saturated = Convert<int, SaturateConversion>(fractional);
    const auto truncated = Convert<int, TruncateConversion>(fractional);

    CHECK(saturated.GetLane(0) == 0);
    CHECK(saturated.GetLane(1) == 1);
    CHECK(saturated.GetLane(2) == -6);
    CHECK(saturated.GetLane(3) == 260);

    CHECK(truncated.GetLane(0) == 0);
    CHECK(truncated.GetLane(1) == 1);
    CHECK(truncated.GetLane(2) == -5);
    CHECK(truncated.GetLane(3) == 260);

    const std::array<float, FloatVec::lanes> extremeValues {
            -500.0F,
            500.0F,
            static_cast<float>(std::numeric_limits<int>::max()) + 1000.0F,
            std::numeric_limits<float>::infinity()};
    const auto extremes = FloatVec::Load(extremeValues.data());

    const auto saturatedExtremes = Convert<int, SaturateConversion>(extremes);
    CHECK(saturatedExtremes.GetLane(0) == -500);
    CHECK(saturatedExtremes.GetLane(1) == 500);
    CHECK(saturatedExtremes.GetLane(2) == std::numeric_limits<int>::max());
    CHECK(saturatedExtremes.GetLane(3) == std::numeric_limits<int>::max());

    const auto truncatedExtremes = Convert<int, TruncateConversion>(extremes);
    CHECK(truncatedExtremes.GetLane(0) == -500);
    CHECK(truncatedExtremes.GetLane(1) == 500);
    CHECK(truncatedExtremes.GetLane(2) == std::numeric_limits<int>::max());
    CHECK(truncatedExtremes.GetLane(3) == std::numeric_limits<int>::max());
}

TEST_CASE("Vec math policy functions")
{
    using Vec4f = Vec<float, ScalarTag, 4>;

    const auto expInput = Vec4f::Iota(0.0F, 0.5F);
    const auto expVec   = Exp(expInput);
    for (int lane = 0; lane < Vec4f::lanes; ++lane)
    {
        const auto laneValue = expInput.GetLane(lane);
        CHECK(expVec.GetLane(lane) == Catch::Approx(static_cast<float>(std::exp(static_cast<double>(laneValue)))));
    }

    const std::array<float, Vec4f::lanes> logValues {1.0F, 2.0F, 4.0F, 8.0F};
    const auto                            logInput = Vec4f::Load(logValues.data());
    const auto                            logVec   = Log(logInput);
    for (int lane = 0; lane < Vec4f::lanes; ++lane)
    {
        CHECK(logVec.GetLane(lane) == Catch::Approx(static_cast<float>(std::log(static_cast<double>(logValues[lane])))));
    }

    const auto                            pi = std::numbers::pi_v<float>;
    const std::array<float, Vec4f::lanes> angleValues {0.0F, pi / 2.0F, pi, 3.0F * pi / 2.0F};
    const auto                            angles = Vec4f::Load(angleValues.data());

    const auto sinVec = Sin(angles);
    CHECK(sinVec.GetLane(0) == Catch::Approx(0.0F).margin(1e-6F));
    CHECK(sinVec.GetLane(1) == Catch::Approx(1.0F));
    CHECK(sinVec.GetLane(2) == Catch::Approx(0.0F).margin(1e-6F));
    CHECK(sinVec.GetLane(3) == Catch::Approx(-1.0F));

    const auto cosVec = Cos(angles);
    CHECK(cosVec.GetLane(0) == Catch::Approx(1.0F));
    CHECK(cosVec.GetLane(1) == Catch::Approx(0.0F).margin(1e-6F));
    CHECK(cosVec.GetLane(2) == Catch::Approx(-1.0F));
    CHECK(cosVec.GetLane(3) == Catch::Approx(0.0F).margin(1e-6F));

    const std::array<float, Vec4f::lanes> sqrtValues {1.0F, 4.0F, 9.0F, 16.0F};
    const auto                            sqrtInput = Vec4f::Load(sqrtValues.data());
    const auto                            sqrtVec   = Sqrt(sqrtInput);
    for (int lane = 0; lane < Vec4f::lanes; ++lane)
    {
        CHECK(sqrtVec.GetLane(lane) == Catch::Approx(static_cast<float>(std::sqrt(static_cast<double>(sqrtValues[lane])))));
    }

    const auto fastExp = Exp<FastMathPolicy>(expInput);
    for (int lane = 0; lane < Vec4f::lanes; ++lane)
    {
        CHECK(fastExp.GetLane(lane) == Catch::Approx(expVec.GetLane(lane)).epsilon(1e-4F));
    }
}

TEST_CASE("ForEachSimd processes tails")
{
    using Vec4f = Vec<float, ScalarTag, 4>;
    std::array<float, 6> input {};
    std::iota(input.begin(), input.end(), 0.0F);

    std::array<float, 6> output {};
    output.fill(-100.0F);

    ForEachSimd<float, ScalarTag, Vec4f::lanes>(
            output.data(),
            input.data(),
            input.size(),
            [](Vec4f vector) {
                const auto increment = Vec4f {1.0F};
                return vector + increment;
            });

    for (std::size_t i = 0; i < input.size(); ++i)
    {
        CHECK(output[i] == Catch::Approx(input[i] + 1.0F));
    }
}

TEST_CASE("BitCast preserves representation")
{
    constexpr float value     = 3.1415926F;
    const auto      bits      = BitCast<std::uint32_t>(value);
    const auto      roundTrip = BitCast<float>(bits);
    CHECK(roundTrip == Catch::Approx(value));
}

#if defined(__SSE2__)
TEST_CASE("Vec SSE2 default lane resolution")
{
    using VecSse = Vec<float, SSE2Tag>;
    static_assert(VecSse::lanes == detail::BackendTraits<SSE2Tag, float>::native_lanes);

    const auto base  = VecSse::Iota(0.0F, 1.0F);
    const auto added = base + VecSse {1.0F};

    for (int lane = 0; lane < VecSse::lanes; ++lane)
    {
        CHECK(added.GetLane(lane) == Catch::Approx(static_cast<float>(lane + 1)));
    }
}
#endif

#if defined(__SSE2__)
TEST_CASE("Fast math policy SSE2 accuracy")
{
    ValidateFastMathApproximation<SSE2Tag>();
}

TEST_CASE("Fast math policy SSE2 special cases")
{
    ValidateFastMathSpecialValues<SSE2Tag>();
}
#endif

#if defined(__AVX2__)
TEST_CASE("Vec AVX2 default lane resolution")
{
    using VecAvx = Vec<float, AVX2Tag>;
    static_assert(VecAvx::lanes == detail::BackendTraits<AVX2Tag, float>::native_lanes);

    const auto base     = VecAvx::Iota(0.0F, 1.0F);
    const auto doubled  = base + base;
    const auto expected = VecAvx::Iota(0.0F, 2.0F);

    for (int lane = 0; lane < VecAvx::lanes; ++lane)
    {
        CHECK(doubled.GetLane(lane) == Catch::Approx(expected.GetLane(lane)));
    }
}
#endif

#if defined(__AVX2__)
TEST_CASE("Fast math policy AVX2 accuracy")
{
    ValidateFastMathApproximation<AVX2Tag>();
}

TEST_CASE("Fast math policy AVX2 special cases")
{
    ValidateFastMathSpecialValues<AVX2Tag>();
}
#endif

#if defined(__SSE2__)
TEST_CASE("Vec SSE2 masked load/store")
{
    using VecSse  = Vec<float, SSE2Tag>;
    using MaskSse = VecSse::mask_type;

    alignas(16) float source[VecSse::lanes] {10.0F, 20.0F, 30.0F, 40.0F};
    auto              mask = MaskSse {};
    mask.SetLane(0, true);
    mask.SetLane(1, false);
    mask.SetLane(2, true);
    mask.SetLane(3, false);

    const auto loaded = VecSse::Load(source, mask, -1.0F);
    CHECK(loaded.GetLane(0) == Catch::Approx(10.0F));
    CHECK(loaded.GetLane(1) == Catch::Approx(-1.0F));
    CHECK(loaded.GetLane(2) == Catch::Approx(30.0F));
    CHECK(loaded.GetLane(3) == Catch::Approx(-1.0F));

    alignas(16) float destination[VecSse::lanes] {100.0F, 100.0F, 100.0F, 100.0F};
    loaded.Store(destination, mask);
    CHECK(destination[0] == Catch::Approx(10.0F));
    CHECK(destination[1] == Catch::Approx(100.0F));
    CHECK(destination[2] == Catch::Approx(30.0F));
    CHECK(destination[3] == Catch::Approx(100.0F));
}

TEST_CASE("Vec SSE2 gather/scatter")
{
    using VecSse      = Vec<float, SSE2Tag>;
    using IndexVecSse = Vec<int, SSE2Tag, VecSse::lanes>;

    const float base[8] {0.5F, 1.5F, 2.5F, 3.5F, 4.5F, 5.5F, 6.5F, 7.5F};
    const auto  indices = IndexVecSse::Iota(1, 1);// 1,2,3,4

    const auto gathered = VecSse::Gather(base, indices);
    CHECK(gathered.GetLane(0) == Catch::Approx(1.5F));
    CHECK(gathered.GetLane(3) == Catch::Approx(4.5F));

    float scatterTarget[8] {};
    std::fill(std::begin(scatterTarget), std::end(scatterTarget), -3.0F);
    gathered.Scatter(scatterTarget, indices);
    CHECK(scatterTarget[1] == Catch::Approx(1.5F));
    CHECK(scatterTarget[3] == Catch::Approx(3.5F));
    CHECK(scatterTarget[0] == Catch::Approx(-3.0F));

    auto mask = VecSse::mask_type {};
    mask.SetLane(0, true);
    mask.SetLane(1, false);
    mask.SetLane(2, true);
    mask.SetLane(3, false);

    const auto maskedGather = VecSse::Gather(base, indices, mask, -9.0F);
    CHECK(maskedGather.GetLane(0) == Catch::Approx(1.5F));
    CHECK(maskedGather.GetLane(1) == Catch::Approx(-9.0F));

    float maskedScatter[8] {};
    std::fill(std::begin(maskedScatter), std::end(maskedScatter), 42.0F);
    maskedGather.Scatter(maskedScatter, indices, mask);
    CHECK(maskedScatter[1] == Catch::Approx(1.5F));
    CHECK(maskedScatter[2] == Catch::Approx(42.0F));
}
#endif

#if defined(__AVX2__)
TEST_CASE("Vec AVX2 masked load/store")
{
    using VecAvx  = Vec<float, AVX2Tag>;
    using MaskAvx = VecAvx::mask_type;

    alignas(32) float source[VecAvx::lanes];
    std::iota(std::begin(source), std::end(source), 1.0F);// 1..8

    MaskAvx mask {};
    for (int lane = 0; lane < VecAvx::lanes; ++lane)
    {
        mask.SetLane(lane, lane % 2 == 0);// even lanes true
    }

    const auto loaded = VecAvx::Load(source, mask, -5.0F);
    for (int lane = 0; lane < VecAvx::lanes; ++lane)
    {
        const float expected = mask.GetLane(lane) ? source[lane] : -5.0F;
        CHECK(loaded.GetLane(lane) == Catch::Approx(expected));
    }

    alignas(32) float destination[VecAvx::lanes];
    std::fill(std::begin(destination), std::end(destination), 99.0F);
    loaded.Store(destination, mask);

    for (int lane = 0; lane < VecAvx::lanes; ++lane)
    {
        const float expected = mask.GetLane(lane) ? loaded.GetLane(lane) : 99.0F;
        CHECK(destination[lane] == Catch::Approx(expected));
    }
}

TEST_CASE("Vec AVX2 gather/scatter")
{
    using VecAvx      = Vec<float, AVX2Tag>;
    using IndexVecAvx = Vec<int, AVX2Tag, VecAvx::lanes>;

    alignas(32) float base[16];
    std::iota(std::begin(base), std::end(base), 0.0F);

    const auto indices  = IndexVecAvx::Iota(0, 2);// even indices
    const auto gathered = VecAvx::Gather(base, indices);

    for (int lane = 0; lane < VecAvx::lanes; ++lane)
    {
        CHECK(gathered.GetLane(lane) == Catch::Approx(base[2 * lane]));
    }

    VecAvx::mask_type mask {};
    for (int lane = 0; lane < VecAvx::lanes; ++lane)
    {
        mask.SetLane(lane, lane % 2 == 0);
    }

    const auto maskedGather = VecAvx::Gather(base, indices, mask, -7.0F);
    for (int lane = 0; lane < VecAvx::lanes; ++lane)
    {
        const float expected = mask.GetLane(lane) ? base[2 * lane] : -7.0F;
        CHECK(maskedGather.GetLane(lane) == Catch::Approx(expected));
    }

    alignas(32) float scatterTarget[16];
    std::fill(std::begin(scatterTarget), std::end(scatterTarget), 11.0F);
    maskedGather.Scatter(scatterTarget, indices, mask);
    for (int lane = 0; lane < VecAvx::lanes; ++lane)
    {
        const int idx = indices.GetLane(lane);
        if (mask.GetLane(lane))
        {
            CHECK(scatterTarget[idx] == Catch::Approx(maskedGather.GetLane(lane)));
        }
        else
        {
            CHECK(scatterTarget[idx] == Catch::Approx(11.0F));
        }
    }
}
#endif

#if defined(__SSE2__)
TEST_CASE("Vec SSE2 int operations")
{
    using VecSseInt  = Vec<std::int32_t, SSE2Tag>;
    const auto left  = VecSseInt::Iota(1, 1);
    const auto right = VecSseInt::Iota(5, 1);

    const auto sum = left + right;
    CHECK(sum.GetLane(0) == 6);
    CHECK(sum.GetLane(3) == 12);

    const auto product = left * right;
    CHECK(product.GetLane(0) == 5);

    const auto andMask = left & right;
    CHECK(andMask.GetLane(0) == (1 & 5));

    const auto eqMask = (left == left);
    CHECK(All(eqMask));
    const auto ltMask = (left < right);
    CHECK(Any(ltMask));
    CHECK(All(ltMask));
}
#endif

#if defined(__AVX2__)
TEST_CASE("Vec AVX2 int operations")
{
    using VecAvxInt = Vec<std::int32_t, AVX2Tag>;
    const auto a    = VecAvxInt::Iota(0, 1);
    const auto b    = VecAvxInt::Iota(10, -1);

    const auto xorVec = a ^ b;
    CHECK(xorVec.GetLane(0) == (0 ^ 10));

    const auto greaterMask = (b > a);
    CHECK(Any(greaterMask));
    CHECK_FALSE(None(greaterMask));

    const auto sum  = a + b;
    const auto diff = b - a;
    CHECK(sum.GetLane(0) == 10);
    CHECK(diff.GetLane(0) == 10);
}
#endif

#if defined(__ARM_NEON)
TEST_CASE("Vec NEON smoke")
{
    using VecNeon = Vec<float, NeonTag>;
    alignas(16) float  data[VecNeon::lanes] {};
    const auto         loaded = VecNeon::Load(data);
    VecNeon::mask_type mask {};
    loaded.Store(data);
    loaded.Store(data, mask);

    using IndexVecNeon  = Vec<int, NeonTag, VecNeon::lanes>;
    const auto indices  = IndexVecNeon::Iota(0, 1);
    const auto gathered = VecNeon::Gather(data, indices);
    gathered.Scatter(data, indices);
    SUCCEED();
}

TEST_CASE("Vec NEON int operations")
{
    using VecNeonInt = Vec<std::int32_t, NeonTag>;
    const auto base  = VecNeonInt::Iota(0, 1);
    const auto other = VecNeonInt::Iota(10, -1);

    const auto sum = base + other;
    CHECK(sum.GetLane(0) == 10);

    const auto diff = other - base;
    CHECK(diff.GetLane(0) == 10);

    const auto mask = (other > base);
    CHECK(Any(mask));
    CHECK_FALSE(None(mask));
}
#endif

#if defined(__SSE2__)
TEST_CASE("Vec SSE2 comparisons")
{
    using VecSse = Vec<float, SSE2Tag>;

    const auto left  = VecSse::Iota(1.0F, 1.0F);// 1,2,3,4
    const auto right = VecSse::Iota(1.0F, 2.0F);// 1,3,5,7

    const auto eqMask = (left == right);
    CHECK(eqMask.GetLane(0));
    CHECK_FALSE(eqMask.GetLane(1));

    const auto ltMask = (left < right);
    CHECK_FALSE(ltMask.GetLane(0));
    CHECK(ltMask.GetLane(1));

    const auto geMask = (right >= left);
    CHECK(geMask.GetLane(0));
    CHECK(geMask.GetLane(3));

    CHECK(Any(ltMask));
    CHECK_FALSE(All(ltMask));
    const auto combined = eqMask | ltMask;
    CHECK(All(combined));
    const auto inverted = ~ltMask;
    CHECK_FALSE(inverted.GetLane(1));
    CHECK(Any(eqMask));
    CHECK_FALSE(None(eqMask));
}
#endif

#if defined(__SSE2__)
TEST_CASE("Vec SSE2 strict math matches scalar reference")
{
    using VecSse    = Vec<float, SSE2Tag>;
    using VecScalar = Vec<float, ScalarTag, VecSse::lanes>;

    const std::array<float, VecSse::lanes> expValues {-0.5F, -0.1F, 0.25F, 0.75F};
    const auto                            simdExp    = VecSse::Load(expValues.data());
    const auto                            scalarExp  = VecScalar::Load(expValues.data());

    const auto compareApprox = [](const auto& simdVec, const auto& scalarVec, float epsilon) {
        for (int lane = 0; lane < simdVec.lanes; ++lane)
        {
            const auto simdValue   = simdVec.GetLane(lane);
            const auto scalarValue = scalarVec.GetLane(lane);
            if (std::isnan(scalarValue))
            {
                CHECK(std::isnan(simdValue));
                continue;
            }
            if (std::isinf(scalarValue))
            {
                CHECK(std::isinf(simdValue));
                CHECK(std::signbit(simdValue) == std::signbit(scalarValue));
                continue;
            }
            CHECK(simdValue == Catch::Approx(scalarValue).epsilon(epsilon));
        }
    };

    compareApprox(Exp<StrictMathPolicy>(simdExp), Exp<StrictMathPolicy>(scalarExp), 1e-5F);

    const std::array<float, VecSse::lanes> logValues {0.125F, 0.5F, 1.5F, 4.0F};
    const auto                             simdLog    = VecSse::Load(logValues.data());
    const auto                             scalarLog  = VecScalar::Load(logValues.data());
    compareApprox(Log<StrictMathPolicy>(simdLog), Log<StrictMathPolicy>(scalarLog), 1e-5F);

    const std::array<float, VecSse::lanes> trigValues {-3.0F, -1.0F, 0.5F, 2.5F};
    const auto                             simdTrig    = VecSse::Load(trigValues.data());
    const auto                             scalarTrig  = VecScalar::Load(trigValues.data());
    compareApprox(Sin<StrictMathPolicy>(simdTrig), Sin<StrictMathPolicy>(scalarTrig), 1e-5F);
    compareApprox(Cos<StrictMathPolicy>(simdTrig), Cos<StrictMathPolicy>(scalarTrig), 1e-5F);

    const std::array<float, VecSse::lanes> sqrtValues {0.0F, 0.25F, 1.0F, 9.0F};
    const auto                             simdSqrt    = VecSse::Load(sqrtValues.data());
    const auto                             scalarSqrt  = VecScalar::Load(sqrtValues.data());
    compareApprox(Sqrt<StrictMathPolicy>(simdSqrt), Sqrt<StrictMathPolicy>(scalarSqrt), 1e-5F);
}
#endif

#if defined(__AVX2__)
TEST_CASE("Vec AVX2 comparisons")
{
    using VecAvx = Vec<float, AVX2Tag>;

    const auto base = VecAvx::Iota(0.0F, 1.0F);
    auto       other = base;
    for (int lane = VecAvx::lanes / 2; lane < VecAvx::lanes; ++lane)
    {
        other.SetLane(lane, other.GetLane(lane) - 1.0F);
    }

    const auto neMask = (base != other);
    CHECK_FALSE(neMask.GetLane(0));
    CHECK(neMask.GetLane(VecAvx::lanes - 1));

    const auto leMask = (base <= other);
    CHECK(leMask.GetLane(0));
    CHECK_FALSE(leMask.GetLane(VecAvx::lanes - 1));

    CHECK(Any(neMask));
    CHECK_FALSE(All(neMask));
    const auto xorMask = leMask ^ neMask;
    CHECK(Any(xorMask));
    CHECK_FALSE(None(xorMask));
}
#endif

#if defined(__AVX2__)
TEST_CASE("Vec AVX2 strict math matches scalar reference")
{
    using VecAvx    = Vec<float, AVX2Tag>;
    using VecScalar = Vec<float, ScalarTag, VecAvx::lanes>;

    std::array<float, VecAvx::lanes> baseValues {};
    for (int lane = 0; lane < VecAvx::lanes; ++lane)
    {
        baseValues[static_cast<std::size_t>(lane)] = -1.0F + 0.2F * static_cast<float>(lane);
    }

    const auto simdBase   = VecAvx::Load(baseValues.data());
    const auto scalarBase = VecScalar::Load(baseValues.data());

    const auto compareApprox = [](const auto& simdVec, const auto& scalarVec, float epsilon) {
        for (int lane = 0; lane < simdVec.lanes; ++lane)
        {
            const auto simdValue   = simdVec.GetLane(lane);
            const auto scalarValue = scalarVec.GetLane(lane);
            if (std::isnan(scalarValue))
            {
                CHECK(std::isnan(simdValue));
                continue;
            }
            if (std::isinf(scalarValue))
            {
                CHECK(std::isinf(simdValue));
                CHECK(std::signbit(simdValue) == std::signbit(scalarValue));
                continue;
            }
            CHECK(simdValue == Catch::Approx(scalarValue).epsilon(epsilon));
        }
    };

    compareApprox(Exp<StrictMathPolicy>(simdBase), Exp<StrictMathPolicy>(scalarBase), 1e-5F);

    std::array<float, VecAvx::lanes> logValues {};
    for (int lane = 0; lane < VecAvx::lanes; ++lane)
    {
        logValues[static_cast<std::size_t>(lane)] = 0.25F + 0.3F * static_cast<float>(lane);
    }
    const auto simdLog   = VecAvx::Load(logValues.data());
    const auto scalarLog = VecScalar::Load(logValues.data());
    compareApprox(Log<StrictMathPolicy>(simdLog), Log<StrictMathPolicy>(scalarLog), 1e-5F);

    std::array<float, VecAvx::lanes> trigValues {};
    for (int lane = 0; lane < VecAvx::lanes; ++lane)
    {
        trigValues[static_cast<std::size_t>(lane)] = -3.0F + 0.4F * static_cast<float>(lane);
    }
    const auto simdTrig   = VecAvx::Load(trigValues.data());
    const auto scalarTrig = VecScalar::Load(trigValues.data());
    compareApprox(Sin<StrictMathPolicy>(simdTrig), Sin<StrictMathPolicy>(scalarTrig), 1e-5F);
    compareApprox(Cos<StrictMathPolicy>(simdTrig), Cos<StrictMathPolicy>(scalarTrig), 1e-5F);

    std::array<float, VecAvx::lanes> sqrtValues {};
    for (int lane = 0; lane < VecAvx::lanes; ++lane)
    {
        sqrtValues[static_cast<std::size_t>(lane)] = 0.5F * static_cast<float>(lane);
    }
    const auto simdSqrt   = VecAvx::Load(sqrtValues.data());
    const auto scalarSqrt = VecScalar::Load(sqrtValues.data());
    compareApprox(Sqrt<StrictMathPolicy>(simdSqrt), Sqrt<StrictMathPolicy>(scalarSqrt), 1e-5F);
}
#endif
