#include <NGIN/Crypto/Encoding/Base64.hpp>
#include <NGIN/Crypto/Encoding/Base64Url.hpp>
#include <NGIN/Crypto/Encoding/Der.hpp>
#include <NGIN/Crypto/Encoding/Hex.hpp>
#include <NGIN/Crypto/Encoding/Pem.hpp>

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

    [[nodiscard]] NGIN::Crypto::ByteBuffer Bytes(std::initializer_list<NGIN::UInt32> values)
    {
        auto buffer = NGIN::Crypto::MakeByteBuffer(values.size());

        NGIN::UIntSize index = 0;
        for (auto value: values)
        {
            buffer[index++] = static_cast<NGIN::Byte>(value);
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

    void RequireBytesEqual(const NGIN::Crypto::ByteBuffer& bytes, std::initializer_list<NGIN::UInt32> expected)
    {
        REQUIRE(bytes.Size() == expected.size());

        NGIN::UIntSize index = 0;
        for (auto value: expected)
        {
            REQUIRE(bytes[index++] == static_cast<NGIN::Byte>(value));
        }
    }

    void RequireSpanEqual(NGIN::Crypto::ConstByteSpan bytes, std::initializer_list<NGIN::UInt32> expected)
    {
        REQUIRE(bytes.size() == expected.size());

        NGIN::UIntSize index = 0;
        for (auto value: expected)
        {
            REQUIRE(bytes[index++] == static_cast<NGIN::Byte>(value));
        }
    }
}// namespace

TEST_CASE("Hex encodes and decodes strict text", "[Crypto][Encoding]")
{
    const auto input = Bytes("hello");

    auto encoded = NGIN::Crypto::Encoding::EncodeHex(input);
    REQUIRE(encoded.has_value());
    REQUIRE(encoded.value() == "68656c6c6f");

    auto decoded = NGIN::Crypto::Encoding::DecodeHex(encoded.value());
    REQUIRE(decoded.has_value());
    RequireBytesEqual(decoded.value(), "hello");
}

TEST_CASE("Hex rejects odd length and invalid characters", "[Crypto][Encoding]")
{
    auto odd = NGIN::Crypto::Encoding::DecodeHex("abc");
    REQUIRE_FALSE(odd.has_value());
    REQUIRE(odd.error().Code() == NGIN::Crypto::CryptoErrorCode::EncodingError);

    auto invalid = NGIN::Crypto::Encoding::DecodeHex("00xz");
    REQUIRE_FALSE(invalid.has_value());
    REQUIRE(invalid.error().Code() == NGIN::Crypto::CryptoErrorCode::EncodingError);
}

TEST_CASE("Hex preallocated APIs require exact output size", "[Crypto][Encoding]")
{
    const auto input = Bytes("a");

    std::array<char, 1> tooSmall {};
    auto                encode = NGIN::Crypto::Encoding::EncodeHexInto(input, tooSmall);
    REQUIRE_FALSE(encode.has_value());
    REQUIRE(encode.error().Code() == NGIN::Crypto::CryptoErrorCode::OutputBufferTooSmall);

    std::array<NGIN::Byte, 2> tooLarge {};
    auto                      decode = NGIN::Crypto::Encoding::DecodeHexInto("61", tooLarge);
    REQUIRE_FALSE(decode.has_value());
    REQUIRE(decode.error().Code() == NGIN::Crypto::CryptoErrorCode::OutputBufferTooSmall);
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
        REQUIRE(encoded.has_value());
        REQUIRE(encoded.value() == vector.encoded);

        auto decoded = NGIN::Crypto::Encoding::DecodeBase64(vector.encoded);
        REQUIRE(decoded.has_value());
        RequireBytesEqual(decoded.value(), vector.plain);
    }
}

TEST_CASE("Base64 supports omitted padding", "[Crypto][Encoding]")
{
    auto encoded = NGIN::Crypto::Encoding::EncodeBase64(Bytes("fo"), NGIN::Crypto::Encoding::Base64Padding::Omit);
    REQUIRE(encoded.has_value());
    REQUIRE(encoded.value() == "Zm8");

    auto decoded = NGIN::Crypto::Encoding::DecodeBase64(encoded.value());
    REQUIRE(decoded.has_value());
    RequireBytesEqual(decoded.value(), "fo");
}

TEST_CASE("Base64 rejects malformed strict input", "[Crypto][Encoding]")
{
    for (std::string_view text: {"Z", "Z g==", "Zg===", "Z=g=", "Zh==", "Zm9="})
    {
        auto decoded = NGIN::Crypto::Encoding::DecodeBase64(text);
        REQUIRE_FALSE(decoded.has_value());
        REQUIRE(decoded.error().Code() == NGIN::Crypto::CryptoErrorCode::EncodingError);
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
    REQUIRE(encoded.has_value());
    REQUIRE(encoded.value() == "-___");

    auto decoded = NGIN::Crypto::Encoding::DecodeBase64Url(encoded.value());
    REQUIRE(decoded.has_value());
    REQUIRE(decoded.value().Size() == input.size());
    for (NGIN::UIntSize i = 0; i < input.size(); ++i)
    {
        REQUIRE(decoded.value()[i] == input[i]);
    }
}

TEST_CASE("Base64Url rejects standard Base64 alphabet characters", "[Crypto][Encoding]")
{
    auto decoded = NGIN::Crypto::Encoding::DecodeBase64Url("+///");

    REQUIRE_FALSE(decoded.has_value());
    REQUIRE(decoded.error().Code() == NGIN::Crypto::CryptoErrorCode::EncodingError);
}

TEST_CASE("PEM parses strict blocks and normalizes line endings", "[Crypto][Encoding]")
{
    auto blocks = NGIN::Crypto::Encoding::ParsePem(
            "-----BEGIN CERTIFICATE-----\r\n"
            "aGVs\r\n"
            "bG8=\r\n"
            "-----END CERTIFICATE-----\r\n",
            {
                    .allowedLabels   = {"CERTIFICATE"},
                    .maxDecodedBytes = 16,
            });

    REQUIRE(blocks.has_value());
    REQUIRE(blocks.value().Size() == 1);
    REQUIRE(blocks.value()[0].label == "CERTIFICATE");
    RequireBytesEqual(blocks.value()[0].decoded, "hello");
}

TEST_CASE("PEM parses multiple allowlisted blocks", "[Crypto][Encoding]")
{
    auto blocks = NGIN::Crypto::Encoding::ParsePem(
            "-----BEGIN PUBLIC KEY-----\n"
            "Zm8=\n"
            "-----END PUBLIC KEY-----\n"
            "-----BEGIN PRIVATE KEY-----\n"
            "YmFy\n"
            "-----END PRIVATE KEY-----\n",
            {
                    .allowedLabels = {"PUBLIC KEY", "PRIVATE KEY"},
            });

    REQUIRE(blocks.has_value());
    REQUIRE(blocks.value().Size() == 2);
    REQUIRE(blocks.value()[0].label == "PUBLIC KEY");
    RequireBytesEqual(blocks.value()[0].decoded, "fo");
    REQUIRE(blocks.value()[1].label == "PRIVATE KEY");
    RequireBytesEqual(blocks.value()[1].decoded, "bar");

    auto singleOnly = NGIN::Crypto::Encoding::ParsePem(
            "-----BEGIN PUBLIC KEY-----\n"
            "Zm8=\n"
            "-----END PUBLIC KEY-----\n"
            "-----BEGIN PRIVATE KEY-----\n"
            "YmFy\n"
            "-----END PRIVATE KEY-----\n",
            {
                    .allowedLabels       = {"PUBLIC KEY", "PRIVATE KEY"},
                    .allowMultipleBlocks = false,
            });
    REQUIRE_FALSE(singleOnly.has_value());
    REQUIRE(singleOnly.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
}

TEST_CASE("PEM rejects malformed structure", "[Crypto][Encoding]")
{
    for (std::string_view text: {
                 "-----BEGIN CERTIFICATE-----\n"
                 "-----BEGIN PUBLIC KEY-----\n"
                 "Zm8=\n"
                 "-----END PUBLIC KEY-----\n"
                 "-----END CERTIFICATE-----\n",
                 "-----BEGIN CERTIFICATE-----\n"
                 "Zm8=\n"
                 "-----END PUBLIC KEY-----\n",
                 "leading text\n"
                 "-----BEGIN CERTIFICATE-----\n"
                 "Zm8=\n"
                 "-----END CERTIFICATE-----\n",
         })
    {
        auto blocks = NGIN::Crypto::Encoding::ParsePem(text, {.allowedLabels = {"CERTIFICATE", "PUBLIC KEY"}});
        REQUIRE_FALSE(blocks.has_value());
        REQUIRE(blocks.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
    }
}

TEST_CASE("PEM enforces allowlist and decoded size limits", "[Crypto][Encoding]")
{
    auto disallowed = NGIN::Crypto::Encoding::ParsePem(
            "-----BEGIN PRIVATE KEY-----\n"
            "Zm8=\n"
            "-----END PRIVATE KEY-----\n",
            {.allowedLabels = {"PUBLIC KEY"}});
    REQUIRE_FALSE(disallowed.has_value());
    REQUIRE(disallowed.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

    auto tooLarge = NGIN::Crypto::Encoding::ParsePem(
            "-----BEGIN CERTIFICATE-----\n"
            "Zm8=\n"
            "-----END CERTIFICATE-----\n",
            {
                    .allowedLabels   = {"CERTIFICATE"},
                    .maxDecodedBytes = 1,
            });
    REQUIRE_FALSE(tooLarge.has_value());
    REQUIRE(tooLarge.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
}

TEST_CASE("PEM rejects malformed Base64 payloads", "[Crypto][Encoding]")
{
    for (std::string_view text: {
                 "-----BEGIN CERTIFICATE-----\n"
                 "Z g=\n"
                 "-----END CERTIFICATE-----\n",
                 "-----BEGIN CERTIFICATE-----\n"
                 "Zm8\n"
                 "-----END CERTIFICATE-----\n",
         })
    {
        auto blocks = NGIN::Crypto::Encoding::ParsePem(text, {.allowedLabels = {"CERTIFICATE"}});
        REQUIRE_FALSE(blocks.has_value());
        REQUIRE(blocks.error().Code() == NGIN::Crypto::CryptoErrorCode::EncodingError);
    }
}

TEST_CASE("DER reads and writes primitive universal elements", "[Crypto][Encoding]")
{
    namespace Der = NGIN::Crypto::Encoding;

    const auto     integerBytes = Bytes({0x02, 0x01, 0x05});
    Der::DerReader reader {integerBytes};
    auto           element = reader.ReadElement();
    REQUIRE(element.has_value());
    REQUIRE(reader.IsAtEnd());
    REQUIRE(Der::IsDerUniversalElement(element.value(), Der::DerUniversalTag::Integer));

    auto integer = Der::ReadDerInteger(element.value());
    REQUIRE(integer.has_value());
    RequireSpanEqual(integer.value(), {0x05});

    auto encodedInteger = Der::EncodeDerInteger(integer.value());
    REQUIRE(encodedInteger.has_value());
    RequireBytesEqual(encodedInteger.value(), {0x02, 0x01, 0x05});

    const auto oidArcs = std::array<NGIN::UInt32, 7> {1, 2, 840, 113549, 1, 1, 1};
    auto       oid     = Der::EncodeDerObjectIdentifier(oidArcs);
    REQUIRE(oid.has_value());
    RequireBytesEqual(oid.value(), {0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01});

    Der::DerReader oidReader {NGIN::Crypto::ConstByteSpan {oid.value().data(), oid.value().Size()}};
    auto           oidElement = oidReader.ReadElement();
    REQUIRE(oidElement.has_value());

    auto decodedOid = Der::ReadDerObjectIdentifier(oidElement.value());
    REQUIRE(decodedOid.has_value());
    REQUIRE(decodedOid.value().Size() == oidArcs.size());
    for (NGIN::UIntSize i = 0; i < oidArcs.size(); ++i)
    {
        REQUIRE(decodedOid.value()[i] == oidArcs[i]);
    }

    auto bitString = Der::EncodeDerBitString(3, Bytes({0xa0}));
    REQUIRE(bitString.has_value());
    RequireBytesEqual(bitString.value(), {0x03, 0x02, 0x03, 0xa0});

    Der::DerReader bitStringReader {NGIN::Crypto::ConstByteSpan {bitString.value().data(), bitString.value().Size()}};
    auto           bitStringElement = bitStringReader.ReadElement();
    REQUIRE(bitStringElement.has_value());

    auto decodedBitString = Der::ReadDerBitString(bitStringElement.value());
    REQUIRE(decodedBitString.has_value());
    REQUIRE(decodedBitString.value().unusedBitCount == 3);
    RequireSpanEqual(decodedBitString.value().bytes, {0xa0});
}

TEST_CASE("DER reads nested SEQUENCE values with bounded readers", "[Crypto][Encoding]")
{
    namespace Der = NGIN::Crypto::Encoding;

    auto integer = Der::EncodeDerInteger(Bytes({0x05}));
    REQUIRE(integer.has_value());

    auto octetString = Der::EncodeDerOctetString(Bytes("ngin"));
    REQUIRE(octetString.has_value());

    auto children = NGIN::Crypto::ByteBuffer {};
    children.Reserve(integer.value().Size() + octetString.value().Size());
    for (NGIN::Byte byte: integer.value())
    {
        children.PushBack(byte);
    }
    for (NGIN::Byte byte: octetString.value())
    {
        children.PushBack(byte);
    }

    auto sequence = Der::EncodeDerSequence(NGIN::Crypto::ConstByteSpan {children.data(), children.Size()});
    REQUIRE(sequence.has_value());

    Der::DerReader reader {NGIN::Crypto::ConstByteSpan {sequence.value().data(), sequence.value().Size()}};
    auto           sequenceElement = reader.ReadElement();
    REQUIRE(sequenceElement.has_value());

    auto childReader = Der::ReadDerSequence(reader, sequenceElement.value());
    REQUIRE(childReader.has_value());

    auto parsedInteger = childReader.value().ReadElement();
    REQUIRE(parsedInteger.has_value());
    REQUIRE(Der::ReadDerInteger(parsedInteger.value()).has_value());

    auto parsedOctetString = childReader.value().ReadElement();
    REQUIRE(parsedOctetString.has_value());
    auto decodedOctets = Der::ReadDerOctetString(parsedOctetString.value());
    REQUIRE(decodedOctets.has_value());
    RequireSpanEqual(decodedOctets.value(), {0x6e, 0x67, 0x69, 0x6e});
    REQUIRE(childReader.value().IsAtEnd());
}

TEST_CASE("DER rejects BER and non-minimal encodings", "[Crypto][Encoding]")
{
    namespace Der = NGIN::Crypto::Encoding;

    for (auto bytes: {
                 Bytes({0x04, 0x80, 0x00, 0x00}),
                 Bytes({0x04, 0x81, 0x7f}),
                 Bytes({0x04, 0x82, 0x00, 0x80}),
                 Bytes({0x1f, 0x80, 0x01, 0x00}),
         })
    {
        Der::DerReader reader {NGIN::Crypto::ConstByteSpan {bytes.data(), bytes.Size()}};
        auto           element = reader.ReadElement();
        REQUIRE_FALSE(element.has_value());
        REQUIRE(element.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
    }
}

TEST_CASE("DER malformed corpus rejects truncated and oversized TLV forms", "[Crypto][Encoding]")
{
    namespace Der = NGIN::Crypto::Encoding;

    for (auto bytes: {
                 Bytes({}),
                 Bytes({0x04}),
                 Bytes({0x04, 0x02, 0x01}),
                 Bytes({0x04, 0x81}),
                 Bytes({0x04, 0x82, 0x01}),
                 Bytes({0x04, 0x84, 0x00, 0x10, 0x00, 0x00}),
                 Bytes({0x30, 0x03, 0x02, 0x01}),
                 Bytes({0x1f}),
         })
    {
        Der::DerReader reader {NGIN::Crypto::ConstByteSpan {bytes.data(), bytes.Size()}};
        auto           element = reader.ReadElement();
        REQUIRE_FALSE(element.has_value());
        REQUIRE(element.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
    }
}

TEST_CASE("DER validates primitive helper invariants", "[Crypto][Encoding]")
{
    namespace Der = NGIN::Crypto::Encoding;

    for (auto bytes: {
                 Bytes({0x02, 0x02, 0x00, 0x7f}),
                 Bytes({0x03, 0x02, 0x08, 0x00}),
                 Bytes({0x03, 0x02, 0x03, 0xa1}),
                 Bytes({0x06, 0x02, 0x80, 0x01}),
         })
    {
        Der::DerReader reader {NGIN::Crypto::ConstByteSpan {bytes.data(), bytes.Size()}};
        auto           element = reader.ReadElement();
        REQUIRE(element.has_value());

        if (Der::IsDerUniversalElement(element.value(), Der::DerUniversalTag::Integer))
        {
            auto integer = Der::ReadDerInteger(element.value());
            REQUIRE_FALSE(integer.has_value());
            REQUIRE(integer.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
        }
        else if (Der::IsDerUniversalElement(element.value(), Der::DerUniversalTag::BitString))
        {
            auto bitString = Der::ReadDerBitString(element.value());
            REQUIRE_FALSE(bitString.has_value());
            REQUIRE(bitString.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
        }
        else
        {
            auto oid = Der::ReadDerObjectIdentifier(element.value());
            REQUIRE_FALSE(oid.has_value());
            REQUIRE(oid.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
        }
    }

    auto nonMinimalInteger = Der::EncodeDerInteger(Bytes({0x00, 0x7f}));
    REQUIRE_FALSE(nonMinimalInteger.has_value());
    REQUIRE(nonMinimalInteger.error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidArgument);

    auto invalidBitString = Der::EncodeDerBitString(3, Bytes({0xa1}));
    REQUIRE_FALSE(invalidBitString.has_value());
    REQUIRE(invalidBitString.error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidArgument);

    const auto invalidOidArcs = std::array<NGIN::UInt32, 2> {1, 40};
    auto       invalidOid     = Der::EncodeDerObjectIdentifier(invalidOidArcs);
    REQUIRE_FALSE(invalidOid.has_value());
    REQUIRE(invalidOid.error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidArgument);
}
