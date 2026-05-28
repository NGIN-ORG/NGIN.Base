#include <NGIN/Crypto/Memory/ConstantTime.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>

TEST_CASE("ConstantTimeEqual compares equal buffers", "[Crypto][ConstantTime]")
{
    const std::array<NGIN::Byte, 4> left {
            NGIN::Byte {0x01},
            NGIN::Byte {0x02},
            NGIN::Byte {0x03},
            NGIN::Byte {0x04},
    };
    const std::array<NGIN::Byte, 4> right = left;

    REQUIRE(NGIN::Crypto::Memory::ConstantTimeEqual(left, right));
}

TEST_CASE("ConstantTimeEqual rejects different content and size", "[Crypto][ConstantTime]")
{
    const std::array<NGIN::Byte, 4> left {
            NGIN::Byte {0x01},
            NGIN::Byte {0x02},
            NGIN::Byte {0x03},
            NGIN::Byte {0x04},
    };
    const std::array<NGIN::Byte, 4> different {
            NGIN::Byte {0x01},
            NGIN::Byte {0x02},
            NGIN::Byte {0xff},
            NGIN::Byte {0x04},
    };
    const std::array<NGIN::Byte, 3> shorter {
            NGIN::Byte {0x01},
            NGIN::Byte {0x02},
            NGIN::Byte {0x03},
    };

    REQUIRE_FALSE(NGIN::Crypto::Memory::ConstantTimeEqual(left, different));
    REQUIRE_FALSE(NGIN::Crypto::Memory::ConstantTimeEqual(left, shorter));
}
