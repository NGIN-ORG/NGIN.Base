#include <NGIN/Crypto/Random/Random.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Secure random fills caller-provided buffers", "[Crypto][Random]")
{
    auto bytes = NGIN::Crypto::MakeByteBuffer(32);

    auto result = NGIN::Crypto::Random::Fill(NGIN::Crypto::ByteSpan {bytes.data(), bytes.Size()});

    REQUIRE(result.HasValue());
    REQUIRE(bytes.Size() == 32);
}

TEST_CASE("Secure random returns owned dynamic bytes", "[Crypto][Random]")
{
    auto result = NGIN::Crypto::Random::RandomBytes(48);

    REQUIRE(result.HasValue());
    REQUIRE(result.Value().Size() == 48);
}

TEST_CASE("Secure random returns fixed-size bytes", "[Crypto][Random]")
{
    auto result = NGIN::Crypto::Random::RandomBytes<16>();

    REQUIRE(result.HasValue());
    REQUIRE(result.Value().size() == 16);
}

TEST_CASE("Secure random accepts empty output", "[Crypto][Random]")
{
    auto result = NGIN::Crypto::Random::Fill(NGIN::Crypto::ByteSpan {});

    REQUIRE(result.HasValue());
}
