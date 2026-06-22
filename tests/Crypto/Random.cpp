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

TEST_CASE("Platform entropy source forwards to secure random", "[Crypto][Random]")
{
    auto source = NGIN::Crypto::Random::PlatformEntropySource();
    auto bytes  = NGIN::Crypto::MakeByteBuffer(24);

    auto result = source.Fill(NGIN::Crypto::ByteSpan {bytes.data(), bytes.Size()});

    REQUIRE(source.IsAvailable());
    REQUIRE(source.IsCryptographicallySecure());
    REQUIRE(result.HasValue());
}

TEST_CASE("Entropy source can be deterministic for tests", "[Crypto][Random]")
{
    struct CounterState
    {
        NGIN::UInt8 next {0};
    };

    auto fill = [](void* state, NGIN::Crypto::ByteSpan output) noexcept -> NGIN::Crypto::CryptoExpected<void> {
        auto& counter = *static_cast<CounterState*>(state);
        for (auto& byte: output)
        {
            byte = static_cast<NGIN::Byte>(counter.next++);
        }
        return {};
    };

    CounterState                state {};
    auto                        source = NGIN::Crypto::Random::EntropySource {&state, fill, false};
    NGIN::Crypto::FixedBytes<4> output {};

    auto result = source.Fill(NGIN::Crypto::ByteSpan {output.data(), output.size()});

    REQUIRE(source.IsAvailable());
    REQUIRE_FALSE(source.IsCryptographicallySecure());
    REQUIRE(result.HasValue());
    REQUIRE(output[0] == static_cast<NGIN::Byte>(0));
    REQUIRE(output[1] == static_cast<NGIN::Byte>(1));
    REQUIRE(output[2] == static_cast<NGIN::Byte>(2));
    REQUIRE(output[3] == static_cast<NGIN::Byte>(3));
}

TEST_CASE("Empty entropy source reports entropy unavailable", "[Crypto][Random]")
{
    NGIN::Crypto::Random::EntropySource source;
    NGIN::Crypto::FixedBytes<1>         output {};

    auto result = source.Fill(NGIN::Crypto::ByteSpan {output.data(), output.size()});

    REQUIRE_FALSE(source.IsAvailable());
    REQUIRE_FALSE(source.IsCryptographicallySecure());
    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::EntropyUnavailable);
}
