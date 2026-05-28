#include <NGIN/Crypto/Encoding/Hex.hpp>
#include <NGIN/Crypto/Symmetric/Aead.hpp>
#include <NGIN/Crypto/Symmetric/AesGcm.hpp>
#include <NGIN/Crypto/Symmetric/ChaCha20Poly1305.hpp>
#include <NGIN/Crypto/Symmetric/XChaCha20Poly1305.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace
{
    template<NGIN::UIntSize Size>
    [[nodiscard]] NGIN::Crypto::Memory::FixedSecret<Size> ZeroSecret()
    {
        NGIN::Crypto::FixedBytes<Size> bytes {};
        return NGIN::Crypto::Memory::FixedSecret<Size>::FromValue(bytes);
    }

    template<NGIN::UIntSize Size>
    [[nodiscard]] constexpr NGIN::Crypto::FixedBytes<Size> ZeroBytes() noexcept
    {
        return {};
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer DecodeHex(std::string_view text)
    {
        auto decoded = NGIN::Crypto::Encoding::DecodeHex(text);
        REQUIRE(decoded.HasValue());
        return decoded.Value();
    }

    void RequireBytesEqual(NGIN::Crypto::ConstByteSpan actual, NGIN::Crypto::ConstByteSpan expected)
    {
        REQUIRE(actual.size() == expected.size());
        REQUIRE(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
    }
}// namespace

TEST_CASE("AEAD size metadata reports fixed key nonce and tag sizes", "[Crypto][Aead]")
{
    REQUIRE(NGIN::Crypto::Symmetric::AeadKeySize(NGIN::Crypto::AeadAlgorithm::Aes128Gcm) == 16);
    REQUIRE(NGIN::Crypto::Symmetric::AeadKeySize(NGIN::Crypto::AeadAlgorithm::Aes256Gcm) == 32);
    REQUIRE(NGIN::Crypto::Symmetric::AeadKeySize(NGIN::Crypto::AeadAlgorithm::ChaCha20Poly1305) == 32);
    REQUIRE(NGIN::Crypto::Symmetric::AeadKeySize(NGIN::Crypto::AeadAlgorithm::XChaCha20Poly1305) == 32);

    REQUIRE(NGIN::Crypto::Symmetric::AeadNonceSize(NGIN::Crypto::AeadAlgorithm::Aes128Gcm) == 12);
    REQUIRE(NGIN::Crypto::Symmetric::AeadNonceSize(NGIN::Crypto::AeadAlgorithm::Aes256Gcm) == 12);
    REQUIRE(NGIN::Crypto::Symmetric::AeadNonceSize(NGIN::Crypto::AeadAlgorithm::ChaCha20Poly1305) == 12);
    REQUIRE(NGIN::Crypto::Symmetric::AeadNonceSize(NGIN::Crypto::AeadAlgorithm::XChaCha20Poly1305) == 24);

    REQUIRE(NGIN::Crypto::Symmetric::AeadTagSize(NGIN::Crypto::AeadAlgorithm::Aes256Gcm) == 16);
    REQUIRE(NGIN::Crypto::Symmetric::AeadTagSize(NGIN::Crypto::AeadAlgorithm::ChaCha20Poly1305) == 16);
}

TEST_CASE("AEAD algorithm headers expose typed key nonce and tag aliases", "[Crypto][Aead]")
{
    auto aes128Key  = ZeroSecret<16>();
    auto aes256Key  = ZeroSecret<32>();
    auto chachaKey  = ZeroSecret<32>();
    auto xchachaKey = ZeroSecret<32>();

    NGIN::Crypto::Symmetric::AesGcmNonce            aesNonce {};
    NGIN::Crypto::Symmetric::AesGcmTag              aesTag {};
    NGIN::Crypto::Symmetric::ChaCha20Poly1305Nonce  chachaNonce {};
    NGIN::Crypto::Symmetric::ChaCha20Poly1305Tag    chachaTag {};
    NGIN::Crypto::Symmetric::XChaCha20Poly1305Nonce xchachaNonce {};
    NGIN::Crypto::Symmetric::XChaCha20Poly1305Tag   xchachaTag {};
    NGIN::Crypto::Symmetric::Aes128GcmKey           typedAes128Key {std::move(aes128Key)};
    NGIN::Crypto::Symmetric::Aes256GcmKey           typedAes256Key {std::move(aes256Key)};
    NGIN::Crypto::Symmetric::ChaCha20Poly1305Key    typedChachaKey {std::move(chachaKey)};
    NGIN::Crypto::Symmetric::XChaCha20Poly1305Key   typedXchachaKey {std::move(xchachaKey)};

    REQUIRE(typedAes128Key.Bytes().size() == 16);
    REQUIRE(typedAes256Key.Bytes().size() == 32);
    REQUIRE(typedChachaKey.Bytes().size() == 32);
    REQUIRE(typedXchachaKey.Bytes().size() == 32);
    REQUIRE(aesNonce.size() == 12);
    REQUIRE(aesTag.size() == 16);
    REQUIRE(chachaNonce.size() == 12);
    REQUIRE(chachaTag.size() == 16);
    REQUIRE(xchachaNonce.size() == 24);
    REQUIRE(xchachaTag.size() == 16);
}

TEST_CASE("SealInto checks output size before backend support", "[Crypto][Aead]")
{
    auto key = ZeroSecret<32>();

    auto nonce    = ZeroBytes<12>();
    auto plain    = ZeroBytes<8>();
    auto tag      = ZeroBytes<16>();
    auto tooSmall = ZeroBytes<7>();

    NGIN::Crypto::Symmetric::AeadSealInput input {
            .key            = NGIN::Crypto::Memory::SecretView {key.Bytes()},
            .nonce          = nonce,
            .plaintext      = plain,
            .associatedData = NGIN::Crypto::ConstByteSpan {},
    };

    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };

    auto result = NGIN::Crypto::Symmetric::SealInto(
            context,
            NGIN::Crypto::AeadAlgorithm::Aes256Gcm,
            input,
            tooSmall,
            tag);

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::OutputBufferTooSmall);
}

TEST_CASE("SealInto rejects invalid key and nonce sizes", "[Crypto][Aead]")
{
    auto shortKey = ZeroSecret<16>();
    auto key      = ZeroSecret<32>();

    auto nonce      = ZeroBytes<12>();
    auto shortNonce = ZeroBytes<8>();
    auto plain      = ZeroBytes<8>();
    auto cipher     = ZeroBytes<8>();
    auto tag        = ZeroBytes<16>();

    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };

    NGIN::Crypto::Symmetric::AeadSealInput invalidKeyInput {
            .key            = NGIN::Crypto::Memory::SecretView {shortKey.Bytes()},
            .nonce          = nonce,
            .plaintext      = plain,
            .associatedData = NGIN::Crypto::ConstByteSpan {},
    };
    auto invalidKeyResult = NGIN::Crypto::Symmetric::SealInto(
            context,
            NGIN::Crypto::AeadAlgorithm::Aes256Gcm,
            invalidKeyInput,
            cipher,
            tag);

    REQUIRE_FALSE(invalidKeyResult.HasValue());
    REQUIRE(invalidKeyResult.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidKey);

    NGIN::Crypto::Symmetric::AeadSealInput invalidNonceInput {
            .key            = NGIN::Crypto::Memory::SecretView {key.Bytes()},
            .nonce          = shortNonce,
            .plaintext      = plain,
            .associatedData = NGIN::Crypto::ConstByteSpan {},
    };
    auto invalidNonceResult = NGIN::Crypto::Symmetric::SealInto(
            context,
            NGIN::Crypto::AeadAlgorithm::Aes256Gcm,
            invalidNonceInput,
            cipher,
            tag);

    REQUIRE_FALSE(invalidNonceResult.HasValue());
    REQUIRE(invalidNonceResult.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidNonce);
}

TEST_CASE("OpenInto rejects invalid tag size", "[Crypto][Aead]")
{
    auto key = ZeroSecret<32>();

    auto nonce    = ZeroBytes<12>();
    auto cipher   = ZeroBytes<8>();
    auto plain    = ZeroBytes<8>();
    auto shortTag = ZeroBytes<8>();

    NGIN::Crypto::Symmetric::AeadOpenInput input {
            .key            = NGIN::Crypto::Memory::SecretView {key.Bytes()},
            .nonce          = nonce,
            .ciphertext     = cipher,
            .associatedData = NGIN::Crypto::ConstByteSpan {},
            .tag            = shortTag,
    };
    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };

    auto result = NGIN::Crypto::Symmetric::OpenInto(
            context,
            NGIN::Crypto::AeadAlgorithm::Aes256Gcm,
            input,
            plain);

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::InvalidTag);
}

TEST_CASE("AEAD returns unsupported algorithm when context lacks capability", "[Crypto][Aead]")
{
    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };

    auto key = ZeroSecret<32>();

    auto nonce  = ZeroBytes<12>();
    auto plain  = ZeroBytes<8>();
    auto cipher = ZeroBytes<8>();
    auto tag    = ZeroBytes<16>();

    NGIN::Crypto::Symmetric::AeadSealInput input {
            .key            = NGIN::Crypto::Memory::SecretView {key.Bytes()},
            .nonce          = nonce,
            .plaintext      = plain,
            .associatedData = NGIN::Crypto::ConstByteSpan {},
    };

    auto result = NGIN::Crypto::Symmetric::SealInto(
            context,
            NGIN::Crypto::AeadAlgorithm::Aes256Gcm,
            input,
            cipher,
            tag);

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}

TEST_CASE("AEAD owned helpers preserve validation and unsupported errors", "[Crypto][Aead]")
{
    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "empty-test"},
            NGIN::Crypto::Backend::BackendCapabilities {},
    };

    auto key = ZeroSecret<32>();

    auto nonce  = ZeroBytes<12>();
    auto plain  = ZeroBytes<8>();
    auto cipher = ZeroBytes<8>();
    auto tag    = ZeroBytes<16>();

    NGIN::Crypto::Symmetric::AeadSealInput sealInput {
            .key            = NGIN::Crypto::Memory::SecretView {key.Bytes()},
            .nonce          = nonce,
            .plaintext      = plain,
            .associatedData = NGIN::Crypto::ConstByteSpan {},
    };
    NGIN::Crypto::Symmetric::AeadOpenInput openInput {
            .key            = NGIN::Crypto::Memory::SecretView {key.Bytes()},
            .nonce          = nonce,
            .ciphertext     = cipher,
            .associatedData = NGIN::Crypto::ConstByteSpan {},
            .tag            = tag,
    };

    auto sealResult = NGIN::Crypto::Symmetric::Seal(
            context,
            NGIN::Crypto::AeadAlgorithm::Aes256Gcm,
            sealInput);
    auto openResult = NGIN::Crypto::Symmetric::Open(
            context,
            NGIN::Crypto::AeadAlgorithm::Aes256Gcm,
            openInput);

    REQUIRE_FALSE(sealResult.HasValue());
    REQUIRE(sealResult.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
    REQUIRE_FALSE(openResult.HasValue());
    REQUIRE(openResult.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}

TEST_CASE("AES-GCM matches NIST vectors when backend supports it", "[Crypto][Aead]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());
    if (!context.Value().Supports(NGIN::Crypto::AeadAlgorithm::Aes128Gcm) ||
        !context.Value().Supports(NGIN::Crypto::AeadAlgorithm::Aes256Gcm))
    {
        return;
    }

    const auto key128            = DecodeHex("00000000000000000000000000000000");
    const auto key256            = DecodeHex("0000000000000000000000000000000000000000000000000000000000000000");
    const auto nonce             = DecodeHex("000000000000000000000000");
    const auto plaintext         = DecodeHex("00000000000000000000000000000000");
    const auto expectedCipher128 = DecodeHex("0388dace60b6a392f328c2b971b2fe78");
    const auto expectedTag128    = DecodeHex("ab6e47d42cec13bdf53a67b21257bddf");
    const auto expectedCipher256 = DecodeHex("cea7403d4d606b6e074ec5d3baf39d18");
    const auto expectedTag256    = DecodeHex("d0d1c8a799996bf0265b98b5d48ab919");

    NGIN::Crypto::Symmetric::AeadSealInput aes128Input {
            .key            = NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {key128.data(), key128.Size()}},
            .nonce          = NGIN::Crypto::ConstByteSpan {nonce.data(), nonce.Size()},
            .plaintext      = NGIN::Crypto::ConstByteSpan {plaintext.data(), plaintext.Size()},
            .associatedData = NGIN::Crypto::ConstByteSpan {},
    };
    NGIN::Crypto::Symmetric::AeadSealInput aes256Input {
            .key            = NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {key256.data(), key256.Size()}},
            .nonce          = NGIN::Crypto::ConstByteSpan {nonce.data(), nonce.Size()},
            .plaintext      = NGIN::Crypto::ConstByteSpan {plaintext.data(), plaintext.Size()},
            .associatedData = NGIN::Crypto::ConstByteSpan {},
    };

    auto aes128 = NGIN::Crypto::Symmetric::Seal(
            context.Value(),
            NGIN::Crypto::AeadAlgorithm::Aes128Gcm,
            aes128Input);
    auto aes256 = NGIN::Crypto::Symmetric::Seal(
            context.Value(),
            NGIN::Crypto::AeadAlgorithm::Aes256Gcm,
            aes256Input);

    REQUIRE(aes128.HasValue());
    REQUIRE(aes256.HasValue());
    RequireBytesEqual(
            NGIN::Crypto::ConstByteSpan {aes128.Value().ciphertext.data(), aes128.Value().ciphertext.Size()},
            NGIN::Crypto::ConstByteSpan {expectedCipher128.data(), expectedCipher128.Size()});
    RequireBytesEqual(
            NGIN::Crypto::ConstByteSpan {aes128.Value().tag.data(), aes128.Value().tag.size()},
            NGIN::Crypto::ConstByteSpan {expectedTag128.data(), expectedTag128.Size()});
    RequireBytesEqual(
            NGIN::Crypto::ConstByteSpan {aes256.Value().ciphertext.data(), aes256.Value().ciphertext.Size()},
            NGIN::Crypto::ConstByteSpan {expectedCipher256.data(), expectedCipher256.Size()});
    RequireBytesEqual(
            NGIN::Crypto::ConstByteSpan {aes256.Value().tag.data(), aes256.Value().tag.size()},
            NGIN::Crypto::ConstByteSpan {expectedTag256.data(), expectedTag256.Size()});
}

TEST_CASE("AES-GCM opens valid ciphertext and rejects invalid tags when backend supports it", "[Crypto][Aead]")
{
    auto context = NGIN::Crypto::Backend::CreateContext();
    REQUIRE(context.HasValue());
    if (!context.Value().Supports(NGIN::Crypto::AeadAlgorithm::Aes256Gcm))
    {
        return;
    }

    const auto key       = DecodeHex("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
    const auto nonce     = DecodeHex("cafebabefacedbaddecaf888");
    const auto plaintext = DecodeHex(
            "d9313225f88406e5a55909c5aff5269a"
            "86a7a9531534f7da2e4c303d8a318a72");
    const auto aad = DecodeHex("feedfacedeadbeeffeedfacedeadbeefabaddad2");

    NGIN::Crypto::Symmetric::AeadSealInput sealInput {
            .key            = NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {key.data(), key.Size()}},
            .nonce          = NGIN::Crypto::ConstByteSpan {nonce.data(), nonce.Size()},
            .plaintext      = NGIN::Crypto::ConstByteSpan {plaintext.data(), plaintext.Size()},
            .associatedData = NGIN::Crypto::ConstByteSpan {aad.data(), aad.Size()},
    };

    auto sealed = NGIN::Crypto::Symmetric::Seal(
            context.Value(),
            NGIN::Crypto::AeadAlgorithm::Aes256Gcm,
            sealInput);
    REQUIRE(sealed.HasValue());

    NGIN::Crypto::Symmetric::AeadOpenInput openInput {
            .key            = NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {key.data(), key.Size()}},
            .nonce          = NGIN::Crypto::ConstByteSpan {nonce.data(), nonce.Size()},
            .ciphertext     = NGIN::Crypto::ConstByteSpan {sealed.Value().ciphertext.data(), sealed.Value().ciphertext.Size()},
            .associatedData = NGIN::Crypto::ConstByteSpan {aad.data(), aad.Size()},
            .tag            = NGIN::Crypto::ConstByteSpan {sealed.Value().tag.data(), sealed.Value().tag.size()},
    };

    auto opened = NGIN::Crypto::Symmetric::Open(
            context.Value(),
            NGIN::Crypto::AeadAlgorithm::Aes256Gcm,
            openInput);
    REQUIRE(opened.HasValue());
    RequireBytesEqual(
            NGIN::Crypto::ConstByteSpan {opened.Value().data(), opened.Value().Size()},
            NGIN::Crypto::ConstByteSpan {plaintext.data(), plaintext.Size()});

    auto badTag = sealed.Value().tag;
    badTag[0] ^= NGIN::Byte {0x01};
    openInput.tag = NGIN::Crypto::ConstByteSpan {badTag.data(), badTag.size()};

    auto rejected = NGIN::Crypto::Symmetric::Open(
            context.Value(),
            NGIN::Crypto::AeadAlgorithm::Aes256Gcm,
            openInput);
    REQUIRE_FALSE(rejected.HasValue());
    REQUIRE(rejected.Error().Code() == NGIN::Crypto::CryptoErrorCode::AuthenticationFailed);
}

TEST_CASE("AEAD contract does not fake implementation even if capability is manually enabled", "[Crypto][Aead]")
{
    NGIN::Crypto::Backend::BackendCapabilities capabilities;
    capabilities.Enable(NGIN::Crypto::AeadAlgorithm::Aes256Gcm);

    NGIN::Crypto::Backend::CryptoContext context {
            NGIN::Crypto::Backend::BackendInfo {NGIN::Crypto::Backend::BackendKind::Test, "aead-capable-test"},
            capabilities,
    };
    auto key = ZeroSecret<32>();

    auto nonce  = ZeroBytes<12>();
    auto plain  = ZeroBytes<8>();
    auto cipher = ZeroBytes<8>();
    auto tag    = ZeroBytes<16>();

    NGIN::Crypto::Symmetric::AeadSealInput input {
            .key            = NGIN::Crypto::Memory::SecretView {key.Bytes()},
            .nonce          = nonce,
            .plaintext      = plain,
            .associatedData = NGIN::Crypto::ConstByteSpan {},
    };

    auto result = NGIN::Crypto::Symmetric::SealInto(
            context,
            NGIN::Crypto::AeadAlgorithm::Aes256Gcm,
            input,
            cipher,
            tag);

    REQUIRE_FALSE(result.HasValue());
    REQUIRE(result.Error().Code() == NGIN::Crypto::CryptoErrorCode::UnsupportedAlgorithm);
}
