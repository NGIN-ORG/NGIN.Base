/// @file Ecdsa.hpp
/// @brief P-256 ECDSA key, signature, DER conversion, signing, and verification helpers.
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

            const ConstByteSpan value = raw.subspan(offset);
            ByteBuffer          output;
            output.Reserve(value.size() + 1);
            if ((std::to_integer<NGIN::UInt8>(value[0]) & 0x80u) != 0)
            {
                output.PushBack(NGIN::Byte {0});
            }
            for (NGIN::Byte byte: value)
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

            ConstByteSpan value = integer;
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

            for (NGIN::Byte& byte: output)
            {
                byte = NGIN::Byte {0};
            }

            const NGIN::UIntSize pad = output.size() - value.size();
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
        const ByteBuffer r = detail::NormalizeEcdsaIntegerForDer(ConstByteSpan {signature.data(), 32});
        const ByteBuffer s = detail::NormalizeEcdsaIntegerForDer(ConstByteSpan {signature.data() + 32, 32});

        CryptoExpected<ByteBuffer> derR = NGIN::Crypto::Encoding::EncodeDerInteger(ConstByteSpan {r.data(), r.Size()});
        if (!derR.HasValue())
        {
            return derR.Error();
        }
        CryptoExpected<ByteBuffer> derS = NGIN::Crypto::Encoding::EncodeDerInteger(ConstByteSpan {s.data(), s.Size()});
        if (!derS.HasValue())
        {
            return derS.Error();
        }

        ByteBuffer children;
        children.Reserve(derR.Value().Size() + derS.Value().Size());
        for (NGIN::Byte byte: derR.Value())
        {
            children.PushBack(byte);
        }
        for (NGIN::Byte byte: derS.Value())
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

        CryptoExpected<NGIN::Crypto::Encoding::DerElement> sequenceElement = reader.ReadElement();
        if (!sequenceElement.HasValue() || !reader.IsAtEnd())
        {
            return detail::EcdsaDerParseError();
        }

        CryptoExpected<NGIN::Crypto::Encoding::DerReader> sequence =
                NGIN::Crypto::Encoding::ReadDerSequence(reader, sequenceElement.Value());
        if (!sequence.HasValue())
        {
            return detail::EcdsaDerParseError();
        }

        CryptoExpected<NGIN::Crypto::Encoding::DerElement> rElement = sequence.Value().ReadElement();
        CryptoExpected<NGIN::Crypto::Encoding::DerElement> sElement = sequence.Value().ReadElement();
        if (!rElement.HasValue() || !sElement.HasValue() || !sequence.Value().IsAtEnd())
        {
            return detail::EcdsaDerParseError();
        }

        CryptoExpected<ConstByteSpan> r = NGIN::Crypto::Encoding::ReadDerInteger(rElement.Value());
        CryptoExpected<ConstByteSpan> s = NGIN::Crypto::Encoding::ReadDerInteger(sElement.Value());
        if (!r.HasValue() || !s.HasValue())
        {
            return detail::EcdsaDerParseError();
        }

        EcdsaP256Sha256Signature signature {};
        CryptoExpected<void>     copyR = detail::CopyDerIntegerToEcdsaComponent(r.Value(), ByteSpan {signature.data(), 32});
        CryptoExpected<void>     copyS =
                detail::CopyDerIntegerToEcdsaComponent(s.Value(), ByteSpan {signature.data() + 32, 32});
        if (!copyR.HasValue() || !copyS.HasValue())
        {
            return detail::EcdsaDerParseError();
        }

        return signature;
    }

    /// @brief Signs a message into fixed raw P-256 `r || s` signature storage.
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

    /// @brief Signs a message and returns a fixed raw P-256 `r || s` signature.
    [[nodiscard]] inline CryptoExpected<EcdsaP256Sha256Signature> SignEcdsaP256Sha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const EcdsaP256PrivateKey&                  privateKey,
            ConstByteSpan                               message)
    {
        EcdsaP256Sha256Signature signature {};
        CryptoExpected<void>     result = SignEcdsaP256Sha256Into(context, privateKey, message, signature);
        if (!result.HasValue())
        {
            return result.Error();
        }

        return signature;
    }

    /// @brief Verifies a fixed raw P-256 `r || s` signature for a message.
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
