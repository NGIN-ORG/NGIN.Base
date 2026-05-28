#include <NGIN/Crypto/Symmetric/Aead.hpp>

#include <NGIN/Crypto/Errors/CryptoError.hpp>

namespace NGIN::Crypto::Symmetric
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError InvalidKey() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidKey};
        }

        [[nodiscard]] constexpr CryptoError InvalidNonce() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidNonce};
        }

        [[nodiscard]] constexpr CryptoError InvalidTag() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidTag};
        }

        [[nodiscard]] constexpr CryptoError OutputBufferTooSmall() noexcept
        {
            return CryptoError {CryptoErrorCode::OutputBufferTooSmall};
        }

        [[nodiscard]] constexpr CryptoError UnsupportedAlgorithm() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] CryptoExpected<void> ValidateCommon(
                AeadAlgorithm                    algorithm,
                NGIN::Crypto::Memory::SecretView key,
                ConstByteSpan                    nonce) noexcept
        {
            if (key.Size() != AeadKeySize(algorithm))
            {
                return InvalidKey();
            }
            if (nonce.size() != AeadNonceSize(algorithm))
            {
                return InvalidNonce();
            }

            return {};
        }
    }// namespace

    CryptoExpected<void> SealInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            AeadAlgorithm                               algorithm,
            const AeadSealInput&                        input,
            ByteSpan                                    ciphertext,
            ByteSpan                                    tag) noexcept
    {
        if (ciphertext.size() != input.plaintext.size() || tag.size() != AeadTagSize(algorithm))
        {
            return OutputBufferTooSmall();
        }

        auto common = ValidateCommon(algorithm, input.key, input.nonce);
        if (!common.HasValue())
        {
            return common.Error();
        }

        auto supported = context.EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> OpenInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            AeadAlgorithm                               algorithm,
            const AeadOpenInput&                        input,
            ByteSpan                                    plaintext) noexcept
    {
        if (plaintext.size() != input.ciphertext.size())
        {
            return OutputBufferTooSmall();
        }
        if (input.tag.size() != AeadTagSize(algorithm))
        {
            return InvalidTag();
        }

        auto common = ValidateCommon(algorithm, input.key, input.nonce);
        if (!common.HasValue())
        {
            return common.Error();
        }

        auto supported = context.EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

        return UnsupportedAlgorithm();
    }

    CryptoExpected<AeadSealResult> Seal(
            const NGIN::Crypto::Backend::CryptoContext& context,
            AeadAlgorithm                               algorithm,
            const AeadSealInput&                        input)
    {
        AeadSealResult output {
                .ciphertext = MakeByteBuffer(input.plaintext.size()),
                .tag        = {},
        };

        auto result = SealInto(
                context,
                algorithm,
                input,
                ByteSpan {output.ciphertext.data(), output.ciphertext.Size()},
                ByteSpan {output.tag.data(), output.tag.size()});
        if (!result.HasValue())
        {
            return result.Error();
        }

        return output;
    }

    CryptoExpected<ByteBuffer> Open(
            const NGIN::Crypto::Backend::CryptoContext& context,
            AeadAlgorithm                               algorithm,
            const AeadOpenInput&                        input)
    {
        auto output = MakeByteBuffer(input.ciphertext.size());
        auto result = OpenInto(context, algorithm, input, ByteSpan {output.data(), output.Size()});
        if (!result.HasValue())
        {
            return result.Error();
        }

        return output;
    }
}// namespace NGIN::Crypto::Symmetric
