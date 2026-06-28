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
        constexpr NGIN::UIntSize   ED25519_PUBLIC_KEY_BYTES            = 32;
        constexpr NGIN::UIntSize   ED25519_SIGNATURE_BYTES             = 64;
        constexpr NGIN::UIntSize   PASETO_V4_LOCAL_KEY_BYTES           = 32;
        constexpr NGIN::UIntSize   PASETO_V4_LOCAL_NONCE_BYTES         = 32;
#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        constexpr NGIN::UIntSize   PASETO_V4_LOCAL_COUNTER_NONCE_BYTES = 24;
#endif
        constexpr NGIN::UIntSize   PASETO_V4_LOCAL_TAG_BYTES           = 32;

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

        [[nodiscard]] bool HasDuplicateMembers(const NGIN::Serialization::JsonValue& value) noexcept
        {
            if (value.IsObject())
            {
                const auto& members = value.AsObject().members;
                for (NGIN::UIntSize i = 0; i < members.Size(); ++i)
                {
                    for (NGIN::UIntSize j = i + 1; j < members.Size(); ++j)
                    {
                        if (members[i].name == members[j].name)
                        {
                            return true;
                        }
                    }
                    if (HasDuplicateMembers(members[i].value))
                    {
                        return true;
                    }
                }
                return false;
            }

            if (value.IsArray())
            {
                for (const auto& item: value.AsArray().values)
                {
                    if (HasDuplicateMembers(item))
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        [[nodiscard]] CryptoExpected<NGIN::Serialization::JsonDocument> ParseJsonObject(std::string_view json)
        {
            auto document = NGIN::Serialization::JsonParser::Parse(json);
            if (!document.HasValue() || !document.Value().Root().IsObject() ||
                HasDuplicateMembers(document.Value().Root()))
            {
                return ParseError();
            }

            return std::move(document.Value());
        }

        [[nodiscard]] CryptoExpected<NGIN::Int64> JsonNumberToInt64(const NGIN::Serialization::JsonValue& value) noexcept
        {
            if (!value.IsNumber() || !std::isfinite(value.AsNumber()))
            {
                return ParseError();
            }

            const auto number = value.AsNumber();
            const auto whole  = std::trunc(number);
            if (number != whole || whole < static_cast<NGIN::F64>(std::numeric_limits<NGIN::Int64>::min()) ||
                whole > static_cast<NGIN::F64>(std::numeric_limits<NGIN::Int64>::max()))
            {
                return ParseError();
            }

            return static_cast<NGIN::Int64>(whole);
        }

        [[nodiscard]] bool HasClaim(const NGIN::Serialization::JsonObject& object, std::string_view claim) noexcept
        {
            return object.Find(claim) != nullptr;
        }

        [[nodiscard]] CryptoExpected<void> ValidateRequiredClaims(std::string_view payloadJson, PasetoValidationPolicy policy)
        {
            auto document = ParseJsonObject(payloadJson);
            if (!document.HasValue())
            {
                return document.Error();
            }

            const auto& object = document.Value().Root().AsObject();
            for (std::string_view claim: policy.requiredClaims)
            {
                if (!HasClaim(object, claim))
                {
                    return PolicyRejected();
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
            for (auto byte: bytes)
            {
                output.PushBack(byte);
            }
        }

        [[nodiscard]] CryptoExpected<ByteBuffer> Pae(std::initializer_list<ConstByteSpan> pieces)
        {
            if (pieces.size() > std::numeric_limits<NGIN::UInt64>::max())
            {
                return ParseError();
            }

            NGIN::UIntSize payloadSize = 0;
            for (ConstByteSpan piece: pieces)
            {
                if (piece.size() > std::numeric_limits<NGIN::UInt64>::max() ||
                    payloadSize > std::numeric_limits<NGIN::UIntSize>::max() - piece.size())
                {
                    return ParseError();
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
                return ParseError();
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
                return InvalidArgument();
            }

            auto encryptionInput = Concat(StringBytes(PASETO_V4_LOCAL_ENCRYPTION_INFO), nonce);
            if (!encryptionInput.HasValue())
            {
                return encryptionInput.Error();
            }

            auto split = NGIN::Crypto::Backend::detail::Blake2bLibsodium(
                    key,
                    ConstByteSpan {encryptionInput.Value().data(), encryptionInput.Value().Size()},
                    encryptionMaterial);
            if (!split.HasValue())
            {
                return split.Error();
            }

            auto authInput = Concat(StringBytes(PASETO_V4_LOCAL_AUTH_INFO), nonce);
            if (!authInput.HasValue())
            {
                return authInput.Error();
            }

            return NGIN::Crypto::Backend::detail::Blake2bLibsodium(
                    key,
                    ConstByteSpan {authInput.Value().data(), authInput.Value().Size()},
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
            if (!preAuth.HasValue())
            {
                return preAuth.Error();
            }

            return NGIN::Crypto::Backend::detail::Blake2bLibsodium(
                    authKey,
                    ConstByteSpan {preAuth.Value().data(), preAuth.Value().Size()},
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
            if (!document.HasValue())
            {
                return document.Error();
            }

            return document.Value().Root().AsObject().Find(name) != nullptr;
        }

        [[nodiscard]] CryptoExpected<std::string> GetPasetoStringClaimInPayload(
                std::string_view payloadJson,
                std::string_view name)
        {
            auto document = ParseJsonObject(payloadJson);
            if (!document.HasValue())
            {
                return document.Error();
            }

            const auto* value = document.Value().Root().AsObject().Find(name);
            if (value == nullptr || !value->IsString())
            {
                return InvalidArgument();
            }

            return std::string {value->AsString()};
        }

        [[nodiscard]] CryptoExpected<NGIN::Int64> GetPasetoInt64ClaimInPayload(
                std::string_view payloadJson,
                std::string_view name)
        {
            auto document = ParseJsonObject(payloadJson);
            if (!document.HasValue())
            {
                return document.Error();
            }

            const auto* value = document.Value().Root().AsObject().Find(name);
            if (value == nullptr || !value->IsNumber())
            {
                return InvalidArgument();
            }

            return JsonNumberToInt64(*value);
        }

        [[nodiscard]] CryptoExpected<bool> GetPasetoBoolClaimInPayload(
                std::string_view payloadJson,
                std::string_view name)
        {
            auto document = ParseJsonObject(payloadJson);
            if (!document.HasValue())
            {
                return document.Error();
            }

            const auto* value = document.Value().Root().AsObject().Find(name);
            if (value == nullptr || !value->IsBool())
            {
                return InvalidArgument();
            }

            return value->AsBool();
        }
    }// namespace

    CryptoExpected<PasetoV4PublicToken> ParsePasetoV4Public(std::string_view token, PasetoParseOptions options)
    {
        if (!StartsWith(token, PASETO_V4_PUBLIC_HEADER))
        {
            return ParseError();
        }

        const auto bodyStart = PASETO_V4_PUBLIC_HEADER.size();
        const auto footerDot = token.find('.', bodyStart);

        auto payloadAndSignatureText = footerDot == std::string_view::npos ? token.substr(bodyStart)
                                                                           : token.substr(bodyStart, footerDot - bodyStart);
        auto footerText              = footerDot == std::string_view::npos ? std::string_view {} : token.substr(footerDot + 1);

        if (payloadAndSignatureText.empty())
        {
            return ParseError();
        }

        auto payloadAndSignature = NGIN::Crypto::Encoding::DecodeBase64Url(payloadAndSignatureText);
        if (!payloadAndSignature.HasValue())
        {
            return payloadAndSignature.Error();
        }
        if (payloadAndSignature.Value().Size() < ED25519_SIGNATURE_BYTES)
        {
            return ParseError();
        }

        const auto payloadSize = payloadAndSignature.Value().Size() - ED25519_SIGNATURE_BYTES;
        if (payloadSize > options.maxPayloadBytes)
        {
            return ParseError();
        }

        auto footer = NGIN::Crypto::Encoding::DecodeBase64Url(footerText);
        if (!footer.HasValue())
        {
            return footer.Error();
        }
        if (footer.Value().Size() > options.maxFooterBytes)
        {
            return ParseError();
        }

        auto payloadJson = CopyToString(ConstByteSpan {payloadAndSignature.Value().data(), payloadSize});
        auto parsedJson  = ParseJsonObject(payloadJson);
        if (!parsedJson.HasValue())
        {
            return parsedJson.Error();
        }

        auto signature = MakeByteBuffer(ED25519_SIGNATURE_BYTES);
        for (NGIN::UIntSize i = 0; i < ED25519_SIGNATURE_BYTES; ++i)
        {
            signature[i] = payloadAndSignature.Value()[payloadSize + i];
        }

        return PasetoV4PublicToken {
                .payloadJson = std::move(payloadJson),
                .footer      = CopyToString(ConstByteSpan {footer.Value().data(), footer.Value().Size()}),
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
            return InvalidKey();
        }
        if (policy.implicitAssertion.size() > policy.parseOptions.maxImplicitBytes)
        {
            return ParseError();
        }

        auto parsed = ParsePasetoV4Public(token, policy.parseOptions);
        if (!parsed.HasValue())
        {
            return parsed.Error();
        }

        if (!policy.expectedFooter.empty())
        {
            if (!NGIN::Crypto::Memory::ConstantTimeEqual(StringBytes(parsed.Value().footer), policy.expectedFooter))
            {
                return PolicyRejected();
            }
        }

        auto claims = ValidateRequiredClaims(parsed.Value().payloadJson, policy);
        if (!claims.HasValue())
        {
            return claims.Error();
        }

        auto message = Pae({
                StringBytes(PASETO_V4_PUBLIC_HEADER),
                StringBytes(parsed.Value().payloadJson),
                StringBytes(parsed.Value().footer),
                policy.implicitAssertion,
        });
        if (!message.HasValue())
        {
            return message.Error();
        }

        auto verified = NGIN::Crypto::Signatures::Verify(
                context,
                SignatureAlgorithm::Ed25519,
                NGIN::Crypto::Signatures::VerifyInput {
                        .publicKey = publicKey,
                        .message   = ConstByteSpan {message.Value().data(), message.Value().Size()},
                        .signature = ConstByteSpan {parsed.Value().signature.data(), parsed.Value().signature.Size()},
                });
        if (!verified.HasValue())
        {
            return verified.Error();
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
            return InvalidKey();
        }
        if (policy.implicitAssertion.size() > policy.parseOptions.maxImplicitBytes)
        {
            return ParseError();
        }
        if (!StartsWith(token, PASETO_V4_LOCAL_HEADER))
        {
            return ParseError();
        }

        const auto bodyStart = PASETO_V4_LOCAL_HEADER.size();
        const auto footerDot = token.find('.', bodyStart);

        auto payloadText =
                footerDot == std::string_view::npos ? token.substr(bodyStart) : token.substr(bodyStart, footerDot - bodyStart);
        auto footerText = footerDot == std::string_view::npos ? std::string_view {} : token.substr(footerDot + 1);
        if (payloadText.empty())
        {
            return ParseError();
        }

        auto payload = NGIN::Crypto::Encoding::DecodeBase64Url(payloadText);
        if (!payload.HasValue())
        {
            return payload.Error();
        }
        if (payload.Value().Size() < PASETO_V4_LOCAL_NONCE_BYTES + PASETO_V4_LOCAL_TAG_BYTES)
        {
            return ParseError();
        }

        auto footer = NGIN::Crypto::Encoding::DecodeBase64Url(footerText);
        if (!footer.HasValue())
        {
            return footer.Error();
        }
        if (footer.Value().Size() > policy.parseOptions.maxFooterBytes)
        {
            return ParseError();
        }

        const ConstByteSpan footerBytes {footer.Value().data(), footer.Value().Size()};
        if (!policy.expectedFooter.empty() &&
            !NGIN::Crypto::Memory::ConstantTimeEqual(footerBytes, policy.expectedFooter))
        {
            return PolicyRejected();
        }

        const auto ciphertextSize = payload.Value().Size() - PASETO_V4_LOCAL_NONCE_BYTES - PASETO_V4_LOCAL_TAG_BYTES;
        if (ciphertextSize > policy.parseOptions.maxPayloadBytes)
        {
            return ParseError();
        }

        if (!IsLibsodiumContext(context))
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        const ConstByteSpan nonce {
                payload.Value().data(),
                PASETO_V4_LOCAL_NONCE_BYTES,
        };
        const ConstByteSpan ciphertext {
                payload.Value().data() + PASETO_V4_LOCAL_NONCE_BYTES,
                ciphertextSize,
        };
        const ConstByteSpan tag {
                payload.Value().data() + PASETO_V4_LOCAL_NONCE_BYTES + ciphertextSize,
                PASETO_V4_LOCAL_TAG_BYTES,
        };

        auto encryptionMaterial = MakeByteBuffer(PASETO_V4_LOCAL_KEY_BYTES + PASETO_V4_LOCAL_COUNTER_NONCE_BYTES);
        auto authKey            = MakeByteBuffer(PASETO_V4_LOCAL_TAG_BYTES);
        auto derived            = DerivePasetoV4LocalKeys(
                key,
                nonce,
                ByteSpan {encryptionMaterial.data(), encryptionMaterial.Size()},
                ByteSpan {authKey.data(), authKey.Size()});
        if (!derived.HasValue())
        {
            return derived.Error();
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
        if (!tagResult.HasValue())
        {
            NGIN::Crypto::Memory::SecureZero(ByteSpan {encryptionMaterial.data(), encryptionMaterial.Size()});
            return tagResult.Error();
        }

        if (!NGIN::Crypto::Memory::ConstantTimeEqual(ConstByteSpan {computedTag.data(), computedTag.Size()}, tag))
        {
            NGIN::Crypto::Memory::SecureZero(ByteSpan {encryptionMaterial.data(), encryptionMaterial.Size()});
            NGIN::Crypto::Memory::SecureZero(ByteSpan {computedTag.data(), computedTag.Size()});
            return CryptoError {CryptoErrorCode::AuthenticationFailed};
        }
        NGIN::Crypto::Memory::SecureZero(ByteSpan {computedTag.data(), computedTag.Size()});

        auto plaintext = MakeByteBuffer(ciphertextSize);
        auto opened    = NGIN::Crypto::Backend::detail::XChaCha20XorLibsodium(
                NGIN::Crypto::Memory::SecretView {encryptionKeyBytes},
                counterNonceBytes,
                ciphertext,
                ByteSpan {plaintext.data(), plaintext.Size()});
        NGIN::Crypto::Memory::SecureZero(ByteSpan {encryptionMaterial.data(), encryptionMaterial.Size()});
        if (!opened.HasValue())
        {
            NGIN::Crypto::Memory::SecureZero(ByteSpan {plaintext.data(), plaintext.Size()});
            return opened.Error();
        }

        auto payloadJson = CopyToString(ConstByteSpan {plaintext.data(), plaintext.Size()});
        NGIN::Crypto::Memory::SecureZero(ByteSpan {plaintext.data(), plaintext.Size()});
        auto parsedJson = ParseJsonObject(payloadJson);
        if (!parsedJson.HasValue())
        {
            return parsedJson.Error();
        }

        auto claims = ValidateRequiredClaims(payloadJson, policy);
        if (!claims.HasValue())
        {
            return claims.Error();
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
        return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
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
            return InvalidKey();
        }
        if (payloadJson.size() > options.limits.maxPayloadBytes ||
            options.footer.size() > options.limits.maxFooterBytes ||
            options.implicitAssertion.size() > options.limits.maxImplicitBytes)
        {
            return ParseError();
        }
        if (!IsLibsodiumContext(context))
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        auto parsedJson = ParseJsonObject(payloadJson);
        if (!parsedJson.HasValue())
        {
            return parsedJson.Error();
        }

#if defined(NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
        auto nonce  = MakeByteBuffer(PASETO_V4_LOCAL_NONCE_BYTES);
        auto random = context.FillRandom(ByteSpan {nonce.data(), nonce.Size()});
        if (!random.HasValue())
        {
            return random.Error();
        }

        auto encryptionMaterial = MakeByteBuffer(PASETO_V4_LOCAL_KEY_BYTES + PASETO_V4_LOCAL_COUNTER_NONCE_BYTES);
        auto authKey            = MakeByteBuffer(PASETO_V4_LOCAL_TAG_BYTES);
        auto derived            = DerivePasetoV4LocalKeys(
                key,
                ConstByteSpan {nonce.data(), nonce.Size()},
                ByteSpan {encryptionMaterial.data(), encryptionMaterial.Size()},
                ByteSpan {authKey.data(), authKey.Size()});
        if (!derived.HasValue())
        {
            return derived.Error();
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
        if (!encrypted.HasValue())
        {
            NGIN::Crypto::Memory::SecureZero(ByteSpan {authKey.data(), authKey.Size()});
            NGIN::Crypto::Memory::SecureZero(ByteSpan {ciphertext.data(), ciphertext.Size()});
            return encrypted.Error();
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
        if (!tagResult.HasValue())
        {
            NGIN::Crypto::Memory::SecureZero(ByteSpan {ciphertext.data(), ciphertext.Size()});
            return tagResult.Error();
        }

        ByteBuffer body;
        body.Reserve(nonce.Size() + ciphertext.Size() + tag.Size());
        AppendBytes(body, ConstByteSpan {nonce.data(), nonce.Size()});
        AppendBytes(body, ConstByteSpan {ciphertext.data(), ciphertext.Size()});
        AppendBytes(body, ConstByteSpan {tag.data(), tag.Size()});
        NGIN::Crypto::Memory::SecureZero(ByteSpan {ciphertext.data(), ciphertext.Size()});

        auto encodedBody = NGIN::Crypto::Encoding::EncodeBase64Url(ConstByteSpan {body.data(), body.Size()});
        if (!encodedBody.HasValue())
        {
            return encodedBody.Error();
        }

        std::string token {PASETO_V4_LOCAL_HEADER};
        token += encodedBody.Value();
        if (!options.footer.empty())
        {
            auto encodedFooter = NGIN::Crypto::Encoding::EncodeBase64Url(options.footer);
            if (!encodedFooter.HasValue())
            {
                return encodedFooter.Error();
            }
            token.push_back('.');
            token += encodedFooter.Value();
        }

        return token;
#else
        (void) context;
        return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
#endif
    }
}// namespace NGIN::Crypto::Tokens
