#include <NGIN/Crypto/Tokens/Paseto.hpp>

#include <NGIN/Crypto/Encoding/Base64Url.hpp>
#include <NGIN/Crypto/Errors/CryptoError.hpp>
#include <NGIN/Crypto/Memory/ConstantTime.hpp>
#include <NGIN/Crypto/Memory/ZeroMemory.hpp>
#include <NGIN/Crypto/Signatures/Verify.hpp>
#include <NGIN/Serialization/JSON/JsonParser.hpp>

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
#include "../Backends/LibsodiumBackend.hpp"
#endif

#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace NGIN::Crypto::Tokens
{
    namespace
    {
        constexpr std::string_view PASETO_V4_PUBLIC_HEADER {"v4.public."};
        constexpr std::string_view PASETO_V4_LOCAL_HEADER {"v4.local."};
#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        constexpr std::string_view PASETO_V4_LOCAL_ENCRYPTION_INFO {"paseto-encryption-key"};
        constexpr std::string_view PASETO_V4_LOCAL_AUTH_INFO {"paseto-auth-key-for-aead"};
#endif
        constexpr NGIN::UIntSize ED25519_PUBLIC_KEY_BYTES    = 32;
        constexpr NGIN::UIntSize ED25519_SIGNATURE_BYTES     = 64;
        constexpr NGIN::UIntSize PASETO_V4_LOCAL_KEY_BYTES   = 32;
        constexpr NGIN::UIntSize PASETO_V4_LOCAL_NONCE_BYTES = 32;
#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        constexpr NGIN::UIntSize PASETO_V4_LOCAL_COUNTER_NONCE_BYTES = 24;
#endif
        constexpr NGIN::UIntSize PASETO_V4_LOCAL_TAG_BYTES = 32;

        [[nodiscard]] constexpr CryptoError ParseError() noexcept
        {
            return CryptoError {CryptoErrorCode::ParseError};
        }

        [[nodiscard]] constexpr CryptoError PolicyRejected() noexcept
        {
            return CryptoError {CryptoErrorCode::PolicyRejected};
        }

        [[nodiscard]] constexpr CryptoError InvalidArgument() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidArgument};
        }

        [[nodiscard]] constexpr CryptoError InvalidKey() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidKey};
        }

        [[nodiscard]] bool StartsWith(std::string_view text, std::string_view prefix) noexcept
        {
            return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
        }

        [[nodiscard]] std::string CopyToString(ConstByteSpan bytes)
        {
            std::string output;
            output.resize(bytes.size());
            for (NGIN::UIntSize i = 0; i < bytes.size(); ++i)
            {
                output[i] = static_cast<char>(std::to_integer<NGIN::UInt8>(bytes[i]));
            }
            return output;
        }

        [[nodiscard]] ConstByteSpan StringBytes(std::string_view text) noexcept
        {
            return ConstByteSpan {reinterpret_cast<const NGIN::Byte*>(text.data()), text.size()};
        }

        [[nodiscard]] CryptoExpected<NGIN::Serialization::JSON::Document>
        ParseJsonObject(std::string_view json)
        {
            auto document = NGIN::Serialization::JSON::Parser::Parse(
                    json);
            if (!document.has_value() || !document.value().Root().IsObject())
            {
                return std::unexpected(ParseError());
            }

            return std::move(document.value());
        }

        [[nodiscard]] CryptoExpected<NGIN::Int64>
        JsonNumberToInt64(NGIN::Serialization::JSON::ValueView value) noexcept
        {
            if (const auto integer = value.TryInt64())
                return *integer;

            const auto number = value.TryDouble();
            if (!number || !std::isfinite(*number))
            {
                return std::unexpected(ParseError());
            }

            const auto whole = std::trunc(*number);
            if (*number != whole || whole < static_cast<NGIN::F64>(std::numeric_limits<NGIN::Int64>::min()) ||
                whole > static_cast<NGIN::F64>(std::numeric_limits<NGIN::Int64>::max()))
            {
                return std::unexpected(ParseError());
            }

            return static_cast<NGIN::Int64>(whole);
        }

        [[nodiscard]] bool HasClaim(
                NGIN::Serialization::JSON::ObjectView object,
                std::string_view                      claim) noexcept
        {
            return object.Find(claim).has_value();
        }

        [[nodiscard]] CryptoExpected<void> ValidateRequiredClaims(std::string_view payloadJson, PasetoValidationPolicy policy)
        {
            auto document = ParseJsonObject(payloadJson);
            if (!document.has_value())
            {
                return std::unexpected(std::move(document).error());
            }

            const auto object = *document.value().Root().TryObject();
            for (std::string_view claim: policy.requiredClaims)
            {
                if (!HasClaim(object, claim))
                {
                    return std::unexpected(PolicyRejected());
                }
            }

            return {};
        }

        void AppendLe64(ByteBuffer& output, NGIN::UInt64 value)
        {
            for (NGIN::UIntSize i = 0; i < 8; ++i)
            {
                output.PushBack(static_cast<NGIN::Byte>(static_cast<NGIN::UInt8>(value & 0xffu)));
                value >>= 8u;
            }
        }

        void AppendBytes(ByteBuffer& output, ConstByteSpan bytes)
        {
            for (NGIN::Byte byte: bytes)
            {
                output.PushBack(byte);
            }
        }

        [[nodiscard]] CryptoExpected<ByteBuffer> Pae(std::initializer_list<ConstByteSpan> pieces)
        {
            if (pieces.size() > std::numeric_limits<NGIN::UInt64>::max())
            {
                return std::unexpected(ParseError());
            }

            NGIN::UIntSize payloadSize = 0;
            for (ConstByteSpan piece: pieces)
            {
                if (piece.size() > std::numeric_limits<NGIN::UInt64>::max() ||
                    payloadSize > std::numeric_limits<NGIN::UIntSize>::max() - piece.size())
                {
                    return std::unexpected(ParseError());
                }
                payloadSize += piece.size();
            }

            ByteBuffer output;
            output.Reserve(8 * (pieces.size() + 1) + payloadSize);
            AppendLe64(output, static_cast<NGIN::UInt64>(pieces.size()));
            for (ConstByteSpan piece: pieces)
            {
                AppendLe64(output, static_cast<NGIN::UInt64>(piece.size()));
                AppendBytes(output, piece);
            }

            return output;
        }

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        [[nodiscard]] CryptoExpected<ByteBuffer> Concat(ConstByteSpan first, ConstByteSpan second)
        {
            if (first.size() > std::numeric_limits<NGIN::UIntSize>::max() - second.size())
            {
                return std::unexpected(ParseError());
            }

            ByteBuffer output;
            output.Reserve(first.size() + second.size());
            AppendBytes(output, first);
            AppendBytes(output, second);
            return output;
        }

        [[nodiscard]] CryptoExpected<void> DerivePasetoV4LocalKeys(
                NGIN::Crypto::Memory::SecretView key,
                ConstByteSpan                    nonce,
                ByteSpan                         encryptionMaterial,
                ByteSpan                         authKey)
        {
            if (encryptionMaterial.size() != PASETO_V4_LOCAL_KEY_BYTES + PASETO_V4_LOCAL_COUNTER_NONCE_BYTES ||
                authKey.size() != PASETO_V4_LOCAL_TAG_BYTES)
            {
                return std::unexpected(InvalidArgument());
            }

            auto encryptionInput = Concat(StringBytes(PASETO_V4_LOCAL_ENCRYPTION_INFO), nonce);
            if (!encryptionInput.has_value())
            {
                return std::unexpected(std::move(encryptionInput).error());
            }

            auto split = NGIN::Crypto::Backend::detail::Blake2bLibsodium(
                    key,
                    ConstByteSpan {encryptionInput.value().data(), encryptionInput.value().Size()},
                    encryptionMaterial);
            if (!split.has_value())
            {
                return std::unexpected(std::move(split).error());
            }

            auto authInput = Concat(StringBytes(PASETO_V4_LOCAL_AUTH_INFO), nonce);
            if (!authInput.has_value())
            {
                return std::unexpected(std::move(authInput).error());
            }

            return NGIN::Crypto::Backend::detail::Blake2bLibsodium(
                    key,
                    ConstByteSpan {authInput.value().data(), authInput.value().Size()},
                    authKey);
        }

        [[nodiscard]] CryptoExpected<void> ComputePasetoV4LocalTag(
                NGIN::Crypto::Memory::SecretView authKey,
                ConstByteSpan                    nonce,
                ConstByteSpan                    ciphertext,
                ConstByteSpan                    footer,
                ConstByteSpan                    implicitAssertion,
                ByteSpan                         tag)
        {
            auto preAuth = Pae({
                    StringBytes(PASETO_V4_LOCAL_HEADER),
                    nonce,
                    ciphertext,
                    footer,
                    implicitAssertion,
            });
            if (!preAuth.has_value())
            {
                return std::unexpected(std::move(preAuth).error());
            }

            return NGIN::Crypto::Backend::detail::Blake2bLibsodium(
                    authKey,
                    ConstByteSpan {preAuth.value().data(), preAuth.value().Size()},
                    tag);
        }
#endif

        [[nodiscard]] constexpr bool IsLibsodiumContext(
                const NGIN::Crypto::Backend::CryptoContext& context) noexcept
        {
            return context.Info().Kind() == NGIN::Crypto::Backend::BackendKind::ExternalPackage &&
                   context.Info().Name() == "libsodium";
        }

        [[nodiscard]] CryptoExpected<bool> HasPasetoClaimInPayload(
                std::string_view payloadJson,
                std::string_view name)
        {
            auto document = ParseJsonObject(payloadJson);
            if (!document.has_value())
            {
                return std::unexpected(std::move(document).error());
            }

            return document.value().Root().TryObject()->Find(name).has_value();
        }

        [[nodiscard]] CryptoExpected<std::string> GetPasetoStringClaimInPayload(
                std::string_view payloadJson,
                std::string_view name)
        {
            auto document = ParseJsonObject(payloadJson);
            if (!document.has_value())
            {
                return std::unexpected(std::move(document).error());
            }

            const auto value = document.value().Root().TryObject()->Find(name);
            if (!value || !value->IsString())
            {
                return std::unexpected(InvalidArgument());
            }

            return std::string {*value->TryString()};
        }

        [[nodiscard]] CryptoExpected<NGIN::Int64> GetPasetoInt64ClaimInPayload(
                std::string_view payloadJson,
                std::string_view name)
        {
            auto document = ParseJsonObject(payloadJson);
            if (!document.has_value())
            {
                return std::unexpected(std::move(document).error());
            }

            const auto value = document.value().Root().TryObject()->Find(name);
            if (!value || !value->IsNumber())
            {
                return std::unexpected(InvalidArgument());
            }

            return JsonNumberToInt64(*value);
        }

        [[nodiscard]] CryptoExpected<bool> GetPasetoBoolClaimInPayload(
                std::string_view payloadJson,
                std::string_view name)
        {
            auto document = ParseJsonObject(payloadJson);
            if (!document.has_value())
            {
                return std::unexpected(std::move(document).error());
            }

            const auto value = document.value().Root().TryObject()->Find(name);
            if (!value || !value->IsBool())
            {
                return std::unexpected(InvalidArgument());
            }

            return *value->TryBool();
        }
    }// namespace

    CryptoExpected<PasetoV4PublicToken> ParsePasetoV4Public(std::string_view token, PasetoParseOptions options)
    {
        if (!StartsWith(token, PASETO_V4_PUBLIC_HEADER))
        {
            return std::unexpected(ParseError());
        }

        const auto bodyStart = PASETO_V4_PUBLIC_HEADER.size();
        const auto footerDot = token.find('.', bodyStart);

        auto payloadAndSignatureText = footerDot == std::string_view::npos ? token.substr(bodyStart)
                                                                           : token.substr(bodyStart, footerDot - bodyStart);
        auto footerText              = footerDot == std::string_view::npos ? std::string_view {} : token.substr(footerDot + 1);

        if (payloadAndSignatureText.empty())
        {
            return std::unexpected(ParseError());
        }

        auto payloadAndSignature = NGIN::Crypto::Encoding::DecodeBase64Url(payloadAndSignatureText);
        if (!payloadAndSignature.has_value())
        {
            return std::unexpected(std::move(payloadAndSignature).error());
        }
        if (payloadAndSignature.value().Size() < ED25519_SIGNATURE_BYTES)
        {
            return std::unexpected(ParseError());
        }

        const auto payloadSize = payloadAndSignature.value().Size() - ED25519_SIGNATURE_BYTES;
        if (payloadSize > options.maxPayloadBytes)
        {
            return std::unexpected(ParseError());
        }

        auto footer = NGIN::Crypto::Encoding::DecodeBase64Url(footerText);
        if (!footer.has_value())
        {
            return std::unexpected(std::move(footer).error());
        }
        if (footer.value().Size() > options.maxFooterBytes)
        {
            return std::unexpected(ParseError());
        }

        auto payloadJson = CopyToString(ConstByteSpan {payloadAndSignature.value().data(), payloadSize});
        auto parsedJson  = ParseJsonObject(payloadJson);
        if (!parsedJson.has_value())
        {
            return std::unexpected(std::move(parsedJson).error());
        }

        auto signature = MakeByteBuffer(ED25519_SIGNATURE_BYTES);
        for (NGIN::UIntSize i = 0; i < ED25519_SIGNATURE_BYTES; ++i)
        {
            signature[i] = payloadAndSignature.value()[payloadSize + i];
        }

        return PasetoV4PublicToken {
                .payloadJson = std::move(payloadJson),
                .footer      = CopyToString(ConstByteSpan {footer.value().data(), footer.value().Size()}),
                .signature   = std::move(signature),
        };
    }

    CryptoExpected<bool> HasPasetoClaim(const PasetoV4PublicToken& token, std::string_view name)
    {
        return HasPasetoClaimInPayload(token.payloadJson, name);
    }

    CryptoExpected<bool> HasPasetoClaim(const PasetoV4LocalToken& token, std::string_view name)
    {
        return HasPasetoClaimInPayload(token.payloadJson, name);
    }

    CryptoExpected<std::string> GetPasetoStringClaim(const PasetoV4PublicToken& token, std::string_view name)
    {
        return GetPasetoStringClaimInPayload(token.payloadJson, name);
    }

    CryptoExpected<std::string> GetPasetoStringClaim(const PasetoV4LocalToken& token, std::string_view name)
    {
        return GetPasetoStringClaimInPayload(token.payloadJson, name);
    }

    CryptoExpected<NGIN::Int64> GetPasetoInt64Claim(const PasetoV4PublicToken& token, std::string_view name)
    {
        return GetPasetoInt64ClaimInPayload(token.payloadJson, name);
    }

    CryptoExpected<NGIN::Int64> GetPasetoInt64Claim(const PasetoV4LocalToken& token, std::string_view name)
    {
        return GetPasetoInt64ClaimInPayload(token.payloadJson, name);
    }

    CryptoExpected<bool> GetPasetoBoolClaim(const PasetoV4PublicToken& token, std::string_view name)
    {
        return GetPasetoBoolClaimInPayload(token.payloadJson, name);
    }

    CryptoExpected<bool> GetPasetoBoolClaim(const PasetoV4LocalToken& token, std::string_view name)
    {
        return GetPasetoBoolClaimInPayload(token.payloadJson, name);
    }

    CryptoExpected<PasetoV4PublicToken> ValidatePasetoV4Public(
            const NGIN::Crypto::Backend::CryptoContext& context,
            std::string_view                            token,
            ConstByteSpan                               publicKey,
            const PasetoValidationPolicy&               policy)
    {
        if (publicKey.size() != ED25519_PUBLIC_KEY_BYTES)
        {
            return std::unexpected(InvalidKey());
        }
        if (policy.implicitAssertion.size() > policy.parseOptions.maxImplicitBytes)
        {
            return std::unexpected(ParseError());
        }

        auto parsed = ParsePasetoV4Public(token, policy.parseOptions);
        if (!parsed.has_value())
        {
            return std::unexpected(std::move(parsed).error());
        }

        if (!policy.expectedFooter.empty())
        {
            if (!NGIN::Crypto::Memory::ConstantTimeEqual(StringBytes(parsed.value().footer), policy.expectedFooter))
            {
                return std::unexpected(PolicyRejected());
            }
        }

        auto claims = ValidateRequiredClaims(parsed.value().payloadJson, policy);
        if (!claims.has_value())
        {
            return std::unexpected(std::move(claims).error());
        }

        auto message = Pae({
                StringBytes(PASETO_V4_PUBLIC_HEADER),
                StringBytes(parsed.value().payloadJson),
                StringBytes(parsed.value().footer),
                policy.implicitAssertion,
        });
        if (!message.has_value())
        {
            return std::unexpected(std::move(message).error());
        }

        auto verified = NGIN::Crypto::Signatures::Verify(
                context,
                SignatureAlgorithm::Ed25519,
                NGIN::Crypto::Signatures::VerifyInput {
                        .publicKey = publicKey,
                        .message   = ConstByteSpan {message.value().data(), message.value().Size()},
                        .signature = ConstByteSpan {parsed.value().signature.data(), parsed.value().signature.Size()},
                });
        if (!verified.has_value())
        {
            return std::unexpected(std::move(verified).error());
        }

        return parsed;
    }

    CryptoExpected<PasetoV4LocalToken> OpenPasetoV4Local(
            const NGIN::Crypto::Backend::CryptoContext& context,
            std::string_view                            token,
            NGIN::Crypto::Memory::SecretView            key,
            const PasetoValidationPolicy&               policy)
    {
        if (key.Size() != PASETO_V4_LOCAL_KEY_BYTES)
        {
            return std::unexpected(InvalidKey());
        }
        if (policy.implicitAssertion.size() > policy.parseOptions.maxImplicitBytes)
        {
            return std::unexpected(ParseError());
        }
        if (!StartsWith(token, PASETO_V4_LOCAL_HEADER))
        {
            return std::unexpected(ParseError());
        }

        const auto bodyStart = PASETO_V4_LOCAL_HEADER.size();
        const auto footerDot = token.find('.', bodyStart);

        auto payloadText =
                footerDot == std::string_view::npos ? token.substr(bodyStart) : token.substr(bodyStart, footerDot - bodyStart);
        auto footerText = footerDot == std::string_view::npos ? std::string_view {} : token.substr(footerDot + 1);
        if (payloadText.empty())
        {
            return std::unexpected(ParseError());
        }

        auto payload = NGIN::Crypto::Encoding::DecodeBase64Url(payloadText);
        if (!payload.has_value())
        {
            return std::unexpected(std::move(payload).error());
        }
        if (payload.value().Size() < PASETO_V4_LOCAL_NONCE_BYTES + PASETO_V4_LOCAL_TAG_BYTES)
        {
            return std::unexpected(ParseError());
        }

        auto footer = NGIN::Crypto::Encoding::DecodeBase64Url(footerText);
        if (!footer.has_value())
        {
            return std::unexpected(std::move(footer).error());
        }
        if (footer.value().Size() > policy.parseOptions.maxFooterBytes)
        {
            return std::unexpected(ParseError());
        }

        const ConstByteSpan footerBytes {footer.value().data(), footer.value().Size()};
        if (!policy.expectedFooter.empty() &&
            !NGIN::Crypto::Memory::ConstantTimeEqual(footerBytes, policy.expectedFooter))
        {
            return std::unexpected(PolicyRejected());
        }

        const auto ciphertextSize = payload.value().Size() - PASETO_V4_LOCAL_NONCE_BYTES - PASETO_V4_LOCAL_TAG_BYTES;
        if (ciphertextSize > policy.parseOptions.maxPayloadBytes)
        {
            return std::unexpected(ParseError());
        }

        if (!IsLibsodiumContext(context))
        {
            return std::unexpected(CryptoError {CryptoErrorCode::UnsupportedAlgorithm});
        }

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        const ConstByteSpan nonce {
                payload.value().data(),
                PASETO_V4_LOCAL_NONCE_BYTES,
        };
        const ConstByteSpan ciphertext {
                payload.value().data() + PASETO_V4_LOCAL_NONCE_BYTES,
                ciphertextSize,
        };
        const ConstByteSpan tag {
                payload.value().data() + PASETO_V4_LOCAL_NONCE_BYTES + ciphertextSize,
                PASETO_V4_LOCAL_TAG_BYTES,
        };

        auto encryptionMaterial = MakeByteBuffer(PASETO_V4_LOCAL_KEY_BYTES + PASETO_V4_LOCAL_COUNTER_NONCE_BYTES);
        auto authKey            = MakeByteBuffer(PASETO_V4_LOCAL_TAG_BYTES);
        auto derived            = DerivePasetoV4LocalKeys(
                key,
                nonce,
                ByteSpan {encryptionMaterial.data(), encryptionMaterial.Size()},
                ByteSpan {authKey.data(), authKey.Size()});
        if (!derived.has_value())
        {
            return std::unexpected(std::move(derived).error());
        }

        const auto encryptionKeyBytes = ConstByteSpan {encryptionMaterial.data(), PASETO_V4_LOCAL_KEY_BYTES};
        const auto counterNonceBytes  = ConstByteSpan {
                encryptionMaterial.data() + PASETO_V4_LOCAL_KEY_BYTES,
                PASETO_V4_LOCAL_COUNTER_NONCE_BYTES,
        };

        auto computedTag = MakeByteBuffer(PASETO_V4_LOCAL_TAG_BYTES);
        auto tagResult   = ComputePasetoV4LocalTag(
                NGIN::Crypto::Memory::SecretView {ConstByteSpan {authKey.data(), authKey.Size()}},
                nonce,
                ciphertext,
                footerBytes,
                policy.implicitAssertion,
                ByteSpan {computedTag.data(), computedTag.Size()});
        NGIN::Crypto::Memory::SecureZero(ByteSpan {authKey.data(), authKey.Size()});
        if (!tagResult.has_value())
        {
            NGIN::Crypto::Memory::SecureZero(ByteSpan {encryptionMaterial.data(), encryptionMaterial.Size()});
            return std::unexpected(std::move(tagResult).error());
        }

        if (!NGIN::Crypto::Memory::ConstantTimeEqual(ConstByteSpan {computedTag.data(), computedTag.Size()}, tag))
        {
            NGIN::Crypto::Memory::SecureZero(ByteSpan {encryptionMaterial.data(), encryptionMaterial.Size()});
            NGIN::Crypto::Memory::SecureZero(ByteSpan {computedTag.data(), computedTag.Size()});
            return std::unexpected(CryptoError {CryptoErrorCode::AuthenticationFailed});
        }
        NGIN::Crypto::Memory::SecureZero(ByteSpan {computedTag.data(), computedTag.Size()});

        auto plaintext = MakeByteBuffer(ciphertextSize);
        auto opened    = NGIN::Crypto::Backend::detail::XChaCha20XorLibsodium(
                NGIN::Crypto::Memory::SecretView {encryptionKeyBytes},
                counterNonceBytes,
                ciphertext,
                ByteSpan {plaintext.data(), plaintext.Size()});
        NGIN::Crypto::Memory::SecureZero(ByteSpan {encryptionMaterial.data(), encryptionMaterial.Size()});
        if (!opened.has_value())
        {
            NGIN::Crypto::Memory::SecureZero(ByteSpan {plaintext.data(), plaintext.Size()});
            return std::unexpected(std::move(opened).error());
        }

        auto payloadJson = CopyToString(ConstByteSpan {plaintext.data(), plaintext.Size()});
        NGIN::Crypto::Memory::SecureZero(ByteSpan {plaintext.data(), plaintext.Size()});
        auto parsedJson = ParseJsonObject(payloadJson);
        if (!parsedJson.has_value())
        {
            return std::unexpected(std::move(parsedJson).error());
        }

        auto claims = ValidateRequiredClaims(payloadJson, policy);
        if (!claims.has_value())
        {
            return std::unexpected(std::move(claims).error());
        }

        auto nonceCopy = MakeByteBuffer(PASETO_V4_LOCAL_NONCE_BYTES);
        for (NGIN::UIntSize i = 0; i < PASETO_V4_LOCAL_NONCE_BYTES; ++i)
        {
            nonceCopy[i] = nonce[i];
        }

        return PasetoV4LocalToken {
                .payloadJson = std::move(payloadJson),
                .footer      = CopyToString(footerBytes),
                .nonce       = std::move(nonceCopy),
        };
#else
        (void) context;
        return std::unexpected(CryptoError {CryptoErrorCode::UnsupportedAlgorithm});
#endif
    }

    CryptoExpected<std::string> SealPasetoV4Local(
            const NGIN::Crypto::Backend::CryptoContext& context,
            std::string_view                            payloadJson,
            NGIN::Crypto::Memory::SecretView            key,
            const PasetoSealOptions&                    options)
    {
        if (key.Size() != PASETO_V4_LOCAL_KEY_BYTES)
        {
            return std::unexpected(InvalidKey());
        }
        if (payloadJson.size() > options.limits.maxPayloadBytes ||
            options.footer.size() > options.limits.maxFooterBytes ||
            options.implicitAssertion.size() > options.limits.maxImplicitBytes)
        {
            return std::unexpected(ParseError());
        }
        if (!IsLibsodiumContext(context))
        {
            return std::unexpected(CryptoError {CryptoErrorCode::UnsupportedAlgorithm});
        }

        auto parsedJson = ParseJsonObject(payloadJson);
        if (!parsedJson.has_value())
        {
            return std::unexpected(std::move(parsedJson).error());
        }

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        auto nonce  = MakeByteBuffer(PASETO_V4_LOCAL_NONCE_BYTES);
        auto random = context.FillRandom(ByteSpan {nonce.data(), nonce.Size()});
        if (!random.has_value())
        {
            return std::unexpected(std::move(random).error());
        }

        auto encryptionMaterial = MakeByteBuffer(PASETO_V4_LOCAL_KEY_BYTES + PASETO_V4_LOCAL_COUNTER_NONCE_BYTES);
        auto authKey            = MakeByteBuffer(PASETO_V4_LOCAL_TAG_BYTES);
        auto derived            = DerivePasetoV4LocalKeys(
                key,
                ConstByteSpan {nonce.data(), nonce.Size()},
                ByteSpan {encryptionMaterial.data(), encryptionMaterial.Size()},
                ByteSpan {authKey.data(), authKey.Size()});
        if (!derived.has_value())
        {
            return std::unexpected(std::move(derived).error());
        }

        const auto encryptionKeyBytes = ConstByteSpan {encryptionMaterial.data(), PASETO_V4_LOCAL_KEY_BYTES};
        const auto counterNonceBytes  = ConstByteSpan {
                encryptionMaterial.data() + PASETO_V4_LOCAL_KEY_BYTES,
                PASETO_V4_LOCAL_COUNTER_NONCE_BYTES,
        };

        auto ciphertext = MakeByteBuffer(payloadJson.size());
        auto encrypted  = NGIN::Crypto::Backend::detail::XChaCha20XorLibsodium(
                NGIN::Crypto::Memory::SecretView {encryptionKeyBytes},
                counterNonceBytes,
                StringBytes(payloadJson),
                ByteSpan {ciphertext.data(), ciphertext.Size()});
        NGIN::Crypto::Memory::SecureZero(ByteSpan {encryptionMaterial.data(), encryptionMaterial.Size()});
        if (!encrypted.has_value())
        {
            NGIN::Crypto::Memory::SecureZero(ByteSpan {authKey.data(), authKey.Size()});
            NGIN::Crypto::Memory::SecureZero(ByteSpan {ciphertext.data(), ciphertext.Size()});
            return std::unexpected(std::move(encrypted).error());
        }

        auto tag       = MakeByteBuffer(PASETO_V4_LOCAL_TAG_BYTES);
        auto tagResult = ComputePasetoV4LocalTag(
                NGIN::Crypto::Memory::SecretView {ConstByteSpan {authKey.data(), authKey.Size()}},
                ConstByteSpan {nonce.data(), nonce.Size()},
                ConstByteSpan {ciphertext.data(), ciphertext.Size()},
                options.footer,
                options.implicitAssertion,
                ByteSpan {tag.data(), tag.Size()});
        NGIN::Crypto::Memory::SecureZero(ByteSpan {authKey.data(), authKey.Size()});
        if (!tagResult.has_value())
        {
            NGIN::Crypto::Memory::SecureZero(ByteSpan {ciphertext.data(), ciphertext.Size()});
            return std::unexpected(std::move(tagResult).error());
        }

        ByteBuffer body;
        body.Reserve(nonce.Size() + ciphertext.Size() + tag.Size());
        AppendBytes(body, ConstByteSpan {nonce.data(), nonce.Size()});
        AppendBytes(body, ConstByteSpan {ciphertext.data(), ciphertext.Size()});
        AppendBytes(body, ConstByteSpan {tag.data(), tag.Size()});
        NGIN::Crypto::Memory::SecureZero(ByteSpan {ciphertext.data(), ciphertext.Size()});

        auto encodedBody = NGIN::Crypto::Encoding::EncodeBase64Url(ConstByteSpan {body.data(), body.Size()});
        if (!encodedBody.has_value())
        {
            return std::unexpected(std::move(encodedBody).error());
        }

        std::string token {PASETO_V4_LOCAL_HEADER};
        token += encodedBody.value();
        if (!options.footer.empty())
        {
            auto encodedFooter = NGIN::Crypto::Encoding::EncodeBase64Url(options.footer);
            if (!encodedFooter.has_value())
            {
                return std::unexpected(std::move(encodedFooter).error());
            }
            token.push_back('.');
            token += encodedFooter.value();
        }

        return token;
#else
        (void) context;
        return std::unexpected(CryptoError {CryptoErrorCode::UnsupportedAlgorithm});
#endif
    }
}// namespace NGIN::Crypto::Tokens
