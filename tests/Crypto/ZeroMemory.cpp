#include <NGIN/Crypto/Memory/ZeroMemory.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>

TEST_CASE("SecureZero clears mutable bytes", "[Crypto][ZeroMemory]")
{
    std::array<NGIN::Byte, 4> bytes {
            NGIN::Byte {0x11},
            NGIN::Byte {0x22},
            NGIN::Byte {0x33},
            NGIN::Byte {0x44},
    };

    NGIN::Crypto::Memory::SecureZero(bytes);

    for (auto byte: bytes)
    {
        REQUIRE(byte == NGIN::Byte {0});
    }
}

TEST_CASE("SecureZero accepts null and empty inputs", "[Crypto][ZeroMemory]")
{
    NGIN::Crypto::Memory::SecureZero(nullptr, 0);
    NGIN::Crypto::Memory::SecureZero(NGIN::Crypto::ByteSpan {});
}
