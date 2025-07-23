#include <NGIN/Utilities/MSBFlag.hpp>
#include <boost/ut.hpp>
#include <sstream>
#include <cstdint>
#include <limits>

using namespace boost::ut;
using namespace NGIN::Utilities;

suite<"MSBFlag"> msbflag_tests = [] {
    "DefaultConstructedFlag_IsZeroAndFalse"_test = [] {
        MSBFlag<uint32_t> flag;
        expect(flag.GetValue() == 0_u32);
        expect(flag.GetFlag() == false);
        expect(flag.GetRaw() == 0_u32);
    };

    "Construct_WithValueAndTrueFlag_RetainsValueAndFlag"_test = [] {
        constexpr uint32_t input = 703710u;
        MSBFlag<uint32_t> f(input, true);
        expect(f.GetValue() == input);
        expect(f.GetFlag());
        expect(f.GetRaw() == (input | MSBFlag<uint32_t>::flagMask));
    };

    "Construct_WithValueAndFalseFlag_RetainsValue"_test = [] {
        constexpr uint16_t input = 4660u;// decimal for hex 0x1234
        MSBFlag<uint16_t> f(input, false);
        expect(f.GetValue() == input);
        expect(!f.GetFlag());
        expect(f.GetRaw() == input);
    };

    "SetValue_PreservesFlag"_test = [] {
        MSBFlag<uint32_t> f(5u, true);
        f.SetValue(42u);
        expect(f.GetValue() == 42_u32);
        expect(f.GetFlag());
    };

    "SetFlag_PreservesValue"_test = [] {
        MSBFlag<uint32_t> f(123u, false);
        f.SetFlag(true);
        expect(f.GetFlag());
        expect(f.GetValue() == 123_u32);

        f.SetFlag(false);
        expect(!f.GetFlag());
        expect(f.GetValue() == 123_u32);
    };

    "Set_BothValueAndFlag_UpdatesRaw"_test = [] {
        MSBFlag<uint32_t> f;
        f.Set(85u, true);
        expect(f.GetValue() == 85_u32);
        expect(f.GetFlag());
        expect(f.GetRaw() == (0x55u | MSBFlag<uint32_t>::flagMask));

        f.Set(170u, false);
        expect(f.GetValue() == 170_u32);
        expect(!f.GetFlag());
    };

    "SetRaw_ClearsAndSetsFlag"_test = [] {
        MSBFlag<uint32_t> f;
        uint32_t raw = MSBFlag<uint32_t>::flagMask | 3405691582u;// uses decimal representation for original hex CAFEBABE
        f.SetRaw(raw);
        expect(f.GetRaw() == raw);
        expect(f.GetFlag());
        expect(f.GetValue() == (raw & MSBFlag<uint32_t>::valueMask));

        f.SetRaw(0u);
        expect(!f.GetFlag());
        expect(f.GetValue() == 0_u32);
    };

    "Equality_Inequality_WorksOnRawData"_test = [] {
        MSBFlag<uint16_t> a(31u, true);
        MSBFlag<uint16_t> b(31u, true);
        MSBFlag<uint16_t> c(31u, false);
        MSBFlag<uint16_t> d(241u, true);

        expect(a == b);
        expect(a != c);
        expect(a != d);
        expect(c != d);
    };

    "MaxValue_ReturnsAllOnesExceptMSB"_test = [] {
        constexpr auto maxVal   = MSBFlag<uint32_t>::MaxValue();
        constexpr auto expected = (std::numeric_limits<uint32_t>::max() >> 1);
        expect(maxVal == expected);
    };

    "StreamInsertion_FormatsValueAndFlag"_test = [] {
        MSBFlag<uint32_t> f(66u, true);
        std::ostringstream oss;
        oss << f;
        expect(oss.str() == std::string("Value=66, Flag=true"));
    };

    "DifferentTypes_WorksForUint8AndUint64"_test = [] {
        MSBFlag<uint8_t> f8(127u, true);
        MSBFlag<uint64_t> f64(4095ull, false);
        expect(f8.GetValue() == 127_u8);// decimal for hex 0x7F
        expect(f8.GetFlag());
        expect(f64.GetValue() == 4095_u64);// decimal for hex 0x0FFF
        expect(!f64.GetFlag());
    };

    // Boundary overflow behavior is assert-protected; no runtime test
};
