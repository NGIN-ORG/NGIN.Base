#include <NGIN/Crypto/Crypto.hpp>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

TEST_CASE("CryptoError stores code and platform code", "[Crypto][Error]")
{
    NGIN::Crypto::CryptoError error {NGIN::Crypto::CryptoErrorCode::EntropyUnavailable, 5};

    REQUIRE(error.HasError());
    REQUIRE(error.Code() == NGIN::Crypto::CryptoErrorCode::EntropyUnavailable);
    REQUIRE(error.PlatformCode() == 5);
    REQUIRE(error.Message() == NGIN::Crypto::ToString(NGIN::Crypto::CryptoErrorCode::EntropyUnavailable));
}

TEST_CASE("CryptoError default is success", "[Crypto][Error]")
{
    NGIN::Crypto::CryptoError error;

    REQUIRE_FALSE(error.HasError());
    REQUIRE(error.Code() == NGIN::Crypto::CryptoErrorCode::None);
    REQUIRE(error.PlatformCode() == 0);
}

TEST_CASE("Crypto core value types are cheap to pass", "[Crypto][Types]")
{
    static_assert(std::is_trivially_copyable_v<NGIN::Crypto::CryptoError>);
    static_assert(std::is_same_v<NGIN::Crypto::ByteBuffer::Value, NGIN::Byte>);

    auto bytes = NGIN::Crypto::MakeByteBuffer(4);
    REQUIRE(bytes.Size() == 4);
    for (auto byte: bytes)
    {
        REQUIRE(byte == NGIN::Byte {0});
    }
}
