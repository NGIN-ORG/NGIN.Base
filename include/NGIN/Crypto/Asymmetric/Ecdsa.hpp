#pragma once

#include <NGIN/Crypto/Asymmetric/KeyTypes.hpp>
#include <NGIN/Crypto/Encoding/Der.hpp>
#include <NGIN/Crypto/Errors/CryptoError.hpp>
#include <NGIN/Crypto/Signatures/Sign.hpp>
#include <NGIN/Crypto/Signatures/Verify.hpp>

#include <cstddef>

namespace NGIN::Crypto::Asymmetric
{
    using EcdsaP256PublicKey          = PublicKey<EcdsaP256KeyTag, 65>;
    using EcdsaP256PrivateKey         = PrivateKey<EcdsaP256KeyTag, 32>;
    using EcdsaP256Sha256Signature    = NGIN::Crypto::Signatures::Signature<64>;
    using EcdsaP256Sha256SignatureRaw = EcdsaP256Sha256Signature;

    namespace detail
    {
        [[nodiscard]] constexpr CryptoError EcdsaDerParseError() noexcept
        {
            return CryptoError {CryptoErrorCode::ParseError};
        }

        [[nodiscard]] inline ByteBuffer NormalizeEcdsaIntegerForDer(ConstByteSpan raw)
        {
            NGIN::UIntSize offset = 0;
            while (offset + 1 < raw.size() && raw[offset] == NGIN::Byte {0})
            {
                ++offset;
            }

            const auto value = raw.subspan(offset);
            ByteBuffer output;
            output.Reserve(value.size() + 1);
            if ((std::to_integer<NGIN::UInt8>(value[0]) & 0x80u) != 0)
            {
                output.PushBack(NGIN::Byte {0});
            }
            for (auto byte: value)
            {
                output.PushBack(byte);
            }

            return output;
        }

        [[nodiscard]] inline CryptoExpected<void> CopyDerIntegerToEcdsaComponent(
                ConstByteSpan integer,
                ByteSpan      output) noexcept
        {
            if (output.size() != 32 || integer.empty())
            {
                return EcdsaDerParseError();
            }

            auto value = integer;
            if (std::to_integer<NGIN::UInt8>(value[0]) == 0)
            {
                if (value.size() == 1)
                {
                    return EcdsaDerParseError();
                }
                value = value.subspan(1);
            }
            else if ((std::to_integer<NGIN::UInt8>(value[0]) & 0x80u) != 0)
            {
                return EcdsaDerParseError();
            }

            if (value.size() > output.size())
            {
                return EcdsaDerParseError();
            }

            for (auto& byte: output)
            {
                byte = NGIN::Byte {0};
            }

            const auto pad = output.size() - value.size();
            for (NGIN::UIntSize i = 0; i < value.size(); ++i)
            {
                output[pad + i] = value[i];
            }

            return {};
        }
    }// namespace detail

    /// @brief Encodes a fixed raw P-256 ECDSA signature (`r || s`) as a DER SEQUENCE of INTEGERs.
    [[nodiscard]] inline CryptoExpected<ByteBuffer> EncodeEcdsaP256Sha256SignatureDer(
            const EcdsaP256Sha256Signature& signature)
    {
        const auto r = detail::NormalizeEcdsaIntegerForDer(ConstByteSpan {signature.data(), 32});
        const auto s = detail::NormalizeEcdsaIntegerForDer(ConstByteSpan {signature.data() + 32, 32});

        auto derR = NGIN::Crypto::Encoding::EncodeDerInteger(ConstByteSpan {r.data(), r.Size()});
        if (!derR.HasValue())
        {
            return derR.Error();
        }
        auto derS = NGIN::Crypto::Encoding::EncodeDerInteger(ConstByteSpan {s.data(), s.Size()});
        if (!derS.HasValue())
        {
            return derS.Error();
        }

        ByteBuffer children;
        children.Reserve(derR.Value().Size() + derS.Value().Size());
        for (auto byte: derR.Value())
        {
            children.PushBack(byte);
        }
        for (auto byte: derS.Value())
        {
            children.PushBack(byte);
        }

        return NGIN::Crypto::Encoding::EncodeDerSequence(ConstByteSpan {children.data(), children.Size()});
    }

    /// @brief Parses a DER ECDSA signature into fixed raw P-256 `r || s` form.
    [[nodiscard]] inline CryptoExpected<EcdsaP256Sha256Signature> ParseEcdsaP256Sha256SignatureDer(
            ConstByteSpan der) noexcept
    {
        NGIN::Crypto::Encoding::DerReader reader {
                der,
                {
                        .maxElementBytes = 96,
                        .maxDepth        = 2,
                },
        };

        auto sequenceElement = reader.ReadElement();
        if (!sequenceElement.HasValue() || !reader.IsAtEnd())
        {
            return detail::EcdsaDerParseError();
        }

        auto sequence = NGIN::Crypto::Encoding::ReadDerSequence(reader, sequenceElement.Value());
        if (!sequence.HasValue())
        {
            return detail::EcdsaDerParseError();
        }

        auto rElement = sequence.Value().ReadElement();
        auto sElement = sequence.Value().ReadElement();
        if (!rElement.HasValue() || !sElement.HasValue() || !sequence.Value().IsAtEnd())
        {
            return detail::EcdsaDerParseError();
        }

        auto r = NGIN::Crypto::Encoding::ReadDerInteger(rElement.Value());
        auto s = NGIN::Crypto::Encoding::ReadDerInteger(sElement.Value());
        if (!r.HasValue() || !s.HasValue())
        {
            return detail::EcdsaDerParseError();
        }

        EcdsaP256Sha256Signature signature {};
        auto                     copyR = detail::CopyDerIntegerToEcdsaComponent(r.Value(), ByteSpan {signature.data(), 32});
        auto                     copyS = detail::CopyDerIntegerToEcdsaComponent(s.Value(), ByteSpan {signature.data() + 32, 32});
        if (!copyR.HasValue() || !copyS.HasValue())
        {
            return detail::EcdsaDerParseError();
        }

        return signature;
    }

    [[nodiscard]] inline CryptoExpected<void> SignEcdsaP256Sha256Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const EcdsaP256PrivateKey&                  privateKey,
            ConstByteSpan                               message,
            EcdsaP256Sha256Signature&                   signature) noexcept
    {
        return NGIN::Crypto::Signatures::SignInto(
                context,
                SignatureAlgorithm::EcdsaP256Sha256,
                NGIN::Crypto::Signatures::SignInput {
                        .privateKey = NGIN::Crypto::Memory::SecretView {privateKey.Bytes()},
                        .message    = message,
                },
                ByteSpan {signature.data(), signature.size()});
    }

    [[nodiscard]] inline CryptoExpected<EcdsaP256Sha256Signature> SignEcdsaP256Sha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const EcdsaP256PrivateKey&                  privateKey,
            ConstByteSpan                               message)
    {
        EcdsaP256Sha256Signature signature {};
        auto                     result = SignEcdsaP256Sha256Into(context, privateKey, message, signature);
        if (!result.HasValue())
        {
            return result.Error();
        }

        return signature;
    }

    [[nodiscard]] inline CryptoExpected<void> VerifyEcdsaP256Sha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const EcdsaP256PublicKey&                   publicKey,
            ConstByteSpan                               message,
            const EcdsaP256Sha256Signature&             signature) noexcept
    {
        return NGIN::Crypto::Signatures::Verify(
                context,
                SignatureAlgorithm::EcdsaP256Sha256,
                NGIN::Crypto::Signatures::VerifyInput {
                        .publicKey = publicKey.Bytes(),
                        .message   = message,
                        .signature = ConstByteSpan {signature.data(), signature.size()},
                });
    }
}// namespace NGIN::Crypto::Asymmetric
