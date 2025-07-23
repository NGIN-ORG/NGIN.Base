/// @file test_lsb_flag.cpp
/// @brief Enhanced unit tests for NGIN::Utilities::LSBFlag

#include <NGIN/Utilities/LSBFlag.hpp>
#include <boost/ut.hpp>
#include <sstream>
#include <cstdint>
#include <limits>

using namespace boost::ut;
using namespace NGIN::Utilities;

suite<"LSBFlag"> lsbflag_tests = [] {
    "DefaultConstructedFlag_IsZeroAndFalse"_test = [] {
        LSBFlag<uint32_t> f;
        expect(f.GetValue() == 0_u32);
        expect(!f.GetFlag());
        expect(f.GetRaw() == 0_u32);
    };

    "Construct_WithValueAndTrueFlag_RetainsValueAndFlag"_test = [] {
        constexpr uint32_t input = 703710u;// decimal literal
        LSBFlag<uint32_t> f(input, true);
        expect(f.GetValue() == input);
        expect(f.GetFlag());
        expect(f.GetRaw() == ((input << 1) | LSBFlag<uint32_t>::flagMask));
    };

    "Construct_WithValueAndFalseFlag_RetainsValue"_test = [] {
        constexpr uint16_t input = 4660u;// decimal literal
        LSBFlag<uint16_t> f(input, false);
        expect(f.GetValue() == input);
        expect(!f.GetFlag());
        expect(f.GetRaw() == static_cast<uint16_t>(input << 1));
    };

    "SetValue_PreservesFlag"_test = [] {
        LSBFlag<uint32_t> f(7u, true);
        f.SetValue(42u);
        expect(f.GetValue() == 42_u32);
        expect(f.GetFlag());
    };

    "SetFlag_PreservesValue"_test = [] {
        LSBFlag<uint32_t> f(99u, false);
        f.SetFlag(true);
        expect(f.GetFlag());
        expect(f.GetValue() == 99u);
        f.SetFlag(false);
        expect(!f.GetFlag());
        expect(f.GetValue() == 99u);
    };

    "Set_BothValueAndFlag_UpdatesRaw"_test = [] {
        LSBFlag<uint32_t> f;
        f.Set(77u, true);
        expect(f.GetValue() == 77u);
        expect(f.GetFlag());
        expect(f.GetRaw() == ((77u << 1) | LSBFlag<uint32_t>::flagMask));

        f.Set(88u, false);
        expect(f.GetValue() == 88_u32);
        expect(!f.GetFlag());
    };

    "SetRaw_ClearsAndSetsFlag"_test = [] {
        LSBFlag<uint32_t> f;
        // Use an odd raw value: flag bit = 1
        uint32_t raw1 = 2021u * 2u + 1u;// ensures odd
        f.SetRaw(raw1);
        expect(f.GetRaw() == raw1);
        expect(f.GetFlag());
        expect(f.GetValue() == (raw1 >> 1));

        // Use an even raw value: flag bit = 0
        uint32_t raw2 = 2022u * 2u;// ensures even
        f.SetRaw(raw2);
        expect(!f.GetFlag());
        expect(f.GetValue() == (raw2 >> 1));
    };

    "Equality_Inequality_WorksOnRawData"_test = [] {
        LSBFlag<uint16_t> a(100u, true);
        LSBFlag<uint16_t> b(100u, true);
        LSBFlag<uint16_t> c(100u, false);
        expect(a == b);
        expect(a != c);
        expect(b != c);
    };

    "MaxValue_ReturnsHalfMax"_test = [] {
        constexpr auto maxVal   = LSBFlag<uint32_t>::MaxValue();
        constexpr auto expected = (std::numeric_limits<uint32_t>::max() >> 1);
        expect(maxVal == expected);
    };

    "StreamInsertion_FormatsValueAndFlag"_test = [] {
        LSBFlag<uint32_t> f(42u, true);
        std::ostringstream oss;
        oss << f;
        expect(oss.str() == std::string("Value=42, Flag=true"));
    };

    "DifferentTypes_WorksForUint8AndUint64"_test = [] {
        LSBFlag<uint8_t> f8(5u, true);
        LSBFlag<uint64_t> f64(12345ull, false);
        expect(f8.GetValue() == 5_u8);
        expect(f8.GetFlag());
        expect(f64.GetValue() == 12345_u64);
        expect(!f64.GetFlag());
    };

    // No runtime tests for overflow: assert-protected
};
