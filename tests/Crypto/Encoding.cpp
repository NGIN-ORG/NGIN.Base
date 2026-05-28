#include <NGIN/Crypto/Encoding/Base64.hpp>
#include <NGIN/Crypto/Encoding/Base64Url.hpp>
#include <NGIN/Crypto/Encoding/Hex.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

namespace
{
    [[nodiscard]] NGIN::Crypto::ByteBuffer Bytes(std::string_view text)
    {
        auto buffer = NGIN::Crypto::MakeByteBuffer(text.size());
        for (NGIN::UIntSize i = 0; i < text.size(); ++i)
        {
            buffer[i] = static_cast<NGIN::Byte>(text[i]);
        }
        return buffer;
    }

    void RequireBytesEqual(const NGIN::Crypto::ByteBuffer& bytes, std::string_view text)
    {
        REQUIRE(bytes.Size() == text.size());
        for (NGIN::UIntSize i = 0; i < text.size(); ++i)
        {
            REQUIRE(bytes[i] == static_cast<NGIN::Byte>(text[i]));
        }
    }
}// namespace

TEST_CASE("Hex encodes and decodes strict text", "[Crypto][Encoding]")
{
    const auto input = Bytes("hello");

    auto encoded = NGIN::Crypto::Encoding::EncodeHex(input);
    REQUIRE(encoded.HasValue());
    REQUIRE(encoded.Value() == "68656c6c6f");

    auto decoded = NGIN::Crypto::Encoding::DecodeHex(encoded.Value());
    REQUIRE(decoded.HasValue());
    RequireBytesEqual(decoded.Value(), "hello");
}

TEST_CASE("Hex rejects odd length and invalid characters", "[Crypto][Encoding]")
{
    auto odd = NGIN::Crypto::Encoding::DecodeHex("abc");
    REQUIRE_FALSE(odd.HasValue());
    REQUIRE(odd.Error().Code() == NGIN::Crypto::CryptoErrorCode::EncodingError);

    auto invalid = NGIN::Crypto::Encoding::DecodeHex("00xz");
    REQUIRE_FALSE(invalid.HasValue());
    REQUIRE(invalid.Error().Code() == NGIN::Crypto::CryptoErrorCode::EncodingError);
}

TEST_CASE("Hex preallocated APIs require exact output size", "[Crypto][Encoding]")
{
    const auto input = Bytes("a");

    std::array<char, 1> tooSmall {};
    auto                encode = NGIN::Crypto::Encoding::EncodeHexInto(input, tooSmall);
    REQUIRE_FALSE(encode.HasValue());
    REQUIRE(encode.Error().Code() == NGIN::Crypto::CryptoErrorCode::OutputBufferTooSmall);

    std::array<NGIN::Byte, 2> tooLarge {};
    auto                      decode = NGIN::Crypto::Encoding::DecodeHexInto("61", tooLarge);
    REQUIRE_FALSE(decode.HasValue());
    REQUIRE(decode.Error().Code() == NGIN::Crypto::CryptoErrorCode::OutputBufferTooSmall);
}

TEST_CASE("Base64 matches RFC 4648 vectors", "[Crypto][Encoding]")
{
    struct Vector
    {
        std::string_view plain;
        std::string_view encoded;
    };

    constexpr std::array vectors {
            Vector {"", ""},
            Vector {"f", "Zg=="},
            Vector {"fo", "Zm8="},
            Vector {"foo", "Zm9v"},
            Vector {"foob", "Zm9vYg=="},
            Vector {"fooba", "Zm9vYmE="},
            Vector {"foobar", "Zm9vYmFy"},
    };

    for (const auto& vector: vectors)
    {
        auto encoded = NGIN::Crypto::Encoding::EncodeBase64(Bytes(vector.plain));
        REQUIRE(encoded.HasValue());
        REQUIRE(encoded.Value() == vector.encoded);

        auto decoded = NGIN::Crypto::Encoding::DecodeBase64(vector.encoded);
        REQUIRE(decoded.HasValue());
        RequireBytesEqual(decoded.Value(), vector.plain);
    }
}

TEST_CASE("Base64 supports omitted padding", "[Crypto][Encoding]")
{
    auto encoded = NGIN::Crypto::Encoding::EncodeBase64(Bytes("fo"), NGIN::Crypto::Encoding::Base64Padding::Omit);
    REQUIRE(encoded.HasValue());
    REQUIRE(encoded.Value() == "Zm8");

    auto decoded = NGIN::Crypto::Encoding::DecodeBase64(encoded.Value());
    REQUIRE(decoded.HasValue());
    RequireBytesEqual(decoded.Value(), "fo");
}

TEST_CASE("Base64 rejects malformed strict input", "[Crypto][Encoding]")
{
    for (std::string_view text: {"Z", "Z g==", "Zg===", "Z=g=", "Zh==", "Zm9="})
    {
        auto decoded = NGIN::Crypto::Encoding::DecodeBase64(text);
        REQUIRE_FALSE(decoded.HasValue());
        REQUIRE(decoded.Error().Code() == NGIN::Crypto::CryptoErrorCode::EncodingError);
    }
}

TEST_CASE("Base64Url encodes without padding by default", "[Crypto][Encoding]")
{
    const std::array<NGIN::Byte, 3> input {
            NGIN::Byte {0xfb},
            NGIN::Byte {0xff},
            NGIN::Byte {0xff},
    };

    auto encoded = NGIN::Crypto::Encoding::EncodeBase64Url(input);
    REQUIRE(encoded.HasValue());
    REQUIRE(encoded.Value() == "-___");

    auto decoded = NGIN::Crypto::Encoding::DecodeBase64Url(encoded.Value());
    REQUIRE(decoded.HasValue());
    REQUIRE(decoded.Value().Size() == input.size());
    for (NGIN::UIntSize i = 0; i < input.size(); ++i)
    {
        REQUIRE(decoded.Value()[i] == input[i]);
    }
}

TEST_CASE("Base64Url rejects standard Base64 alphabet characters", "[Crypto][Encoding]")
{
    auto decoded = NGIN::Crypto::Encoding::DecodeBase64Url("+///");

    REQUIRE_FALSE(decoded.HasValue());
    REQUIRE(decoded.Error().Code() == NGIN::Crypto::CryptoErrorCode::EncodingError);
}
