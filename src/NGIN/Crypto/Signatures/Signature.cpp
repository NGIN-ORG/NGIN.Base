#include <NGIN/Crypto/Signatures/Sign.hpp>
#include <NGIN/Crypto/Signatures/Verify.hpp>

#include <NGIN/Crypto/Errors/CryptoError.hpp>

namespace NGIN::Crypto::Signatures
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError InvalidKey() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidKey};
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
    }// namespace

    CryptoExpected<void> SignInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            SignatureAlgorithm                          algorithm,
            const SignInput&                            input,
            ByteSpan                                    signature) noexcept
    {
        const auto sizes = NGIN::Crypto::Asymmetric::GetSignatureKeySizes(algorithm);
        if (sizes.signatureSize == 0)
        {
            return std::unexpected(UnsupportedAlgorithm());
        }
        if (input.privateKey.Size() != sizes.privateKeySize)
        {
            return std::unexpected(InvalidKey());
        }
        if (signature.size() != sizes.signatureSize)
        {
            return std::unexpected(OutputBufferTooSmall());
        }

        auto supported = context.EnsureSupports(algorithm);
        if (!supported.has_value())
        {
            return std::unexpected(std::move(supported).error());
        }

        return context.SignInto(algorithm, input.privateKey, input.message, signature);
    }

    CryptoExpected<ByteBuffer> Sign(
            const NGIN::Crypto::Backend::CryptoContext& context,
            SignatureAlgorithm                          algorithm,
            const SignInput&                            input)
    {
        const auto size = SignatureSize(algorithm);
        if (size == 0)
        {
            return std::unexpected(UnsupportedAlgorithm());
        }

        auto output = MakeByteBuffer(size);
        auto result = SignInto(context, algorithm, input, ByteSpan {output.data(), output.Size()});
        if (!result.has_value())
        {
            return std::unexpected(std::move(result).error());
        }

        return output;
    }

    CryptoExpected<void> Verify(
            const NGIN::Crypto::Backend::CryptoContext& context,
            SignatureAlgorithm                          algorithm,
            const VerifyInput&                          input) noexcept
    {
        const auto sizes = NGIN::Crypto::Asymmetric::GetSignatureKeySizes(algorithm);
        if (sizes.signatureSize == 0)
        {
            return std::unexpected(UnsupportedAlgorithm());
        }
        if (input.publicKey.size() != sizes.publicKeySize)
        {
            return std::unexpected(InvalidKey());
        }
        if (input.signature.size() != sizes.signatureSize)
        {
            return std::unexpected(InvalidTag());
        }

        auto supported = context.EnsureSupports(algorithm);
        if (!supported.has_value())
        {
            return std::unexpected(std::move(supported).error());
        }

        return context.VerifySignature(algorithm, input.publicKey, input.message, input.signature);
    }
}// namespace NGIN::Crypto::Signatures
