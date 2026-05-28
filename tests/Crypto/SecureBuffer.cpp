#include <NGIN/Crypto/Memory/SecureBuffer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <utility>

TEST_CASE("SecureBuffer owns mutable bytes", "[Crypto][SecureBuffer]")
{
    NGIN::Crypto::Memory::SecureBuffer buffer {8};

    REQUIRE(buffer.Size() == 8);
    REQUIRE_FALSE(buffer.Empty());

    buffer.Data()[0] = NGIN::Byte {0x42};
    REQUIRE(buffer.AsBytes()[0] == NGIN::Byte {0x42});
}

TEST_CASE("SecureBuffer copies from input span", "[Crypto][SecureBuffer]")
{
    const std::array<NGIN::Byte, 3> bytes {
            NGIN::Byte {0x01},
            NGIN::Byte {0x02},
            NGIN::Byte {0x03},
    };

    NGIN::Crypto::Memory::SecureBuffer buffer {bytes};

    REQUIRE(buffer.Size() == bytes.size());
    REQUIRE(buffer.AsBytes()[0] == bytes[0]);
    REQUIRE(buffer.AsBytes()[1] == bytes[1]);
    REQUIRE(buffer.AsBytes()[2] == bytes[2]);
}

TEST_CASE("SecureBuffer is move-only and movable", "[Crypto][SecureBuffer]")
{
    NGIN::Crypto::Memory::SecureBuffer first {4};
    first.Data()[0] = NGIN::Byte {0x7f};

    NGIN::Crypto::Memory::SecureBuffer second {std::move(first)};

    REQUIRE(second.Size() == 4);
    REQUIRE(second.AsBytes()[0] == NGIN::Byte {0x7f});
    REQUIRE(first.Size() == 0);
}

TEST_CASE("SecureBuffer resize preserves prefix and zero-initializes growth", "[Crypto][SecureBuffer]")
{
    NGIN::Crypto::Memory::SecureBuffer buffer {2};
    buffer.Data()[0] = NGIN::Byte {0xaa};
    buffer.Data()[1] = NGIN::Byte {0xbb};

    buffer.Resize(4);

    REQUIRE(buffer.Size() == 4);
    REQUIRE(buffer.AsBytes()[0] == NGIN::Byte {0xaa});
    REQUIRE(buffer.AsBytes()[1] == NGIN::Byte {0xbb});
    REQUIRE(buffer.AsBytes()[2] == NGIN::Byte {0});
    REQUIRE(buffer.AsBytes()[3] == NGIN::Byte {0});

    buffer.Clear();
    REQUIRE(buffer.Empty());
}
