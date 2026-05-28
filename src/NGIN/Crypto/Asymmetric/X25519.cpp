#include <NGIN/Crypto/Asymmetric/X25519.hpp>

#include <NGIN/Crypto/Errors/CryptoError.hpp>

namespace NGIN::Crypto::Asymmetric
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError OutputBufferTooSmall() noexcept
        {
            return CryptoError {CryptoErrorCode::OutputBufferTooSmall};
        }

        [[nodiscard]] constexpr CryptoError UnsupportedAlgorithm() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }
    }// namespace

    CryptoExpected<X25519KeyPair> GenerateX25519KeyPair(
            const NGIN::Crypto::Backend::CryptoContext& context) noexcept
    {
        auto supported = context.EnsureSupports(KeyAgreementAlgorithm::X25519);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

        return UnsupportedAlgorithm();
    }

    CryptoExpected<void> DeriveX25519SharedSecretInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const X25519PrivateKey&                     privateKey,
            const X25519PublicKey&                      peerPublicKey,
            ByteSpan                                    output) noexcept
    {
        (void) privateKey;
        (void) peerPublicKey;

        if (output.size() != GetKeyAgreementSizes(KeyAgreementAlgorithm::X25519).sharedSecretSize)
        {
            return OutputBufferTooSmall();
        }

        auto supported = context.EnsureSupports(KeyAgreementAlgorithm::X25519);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

        return UnsupportedAlgorithm();
    }

    CryptoExpected<X25519SharedSecret> DeriveX25519SharedSecret(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const X25519PrivateKey&                     privateKey,
            const X25519PublicKey&                      peerPublicKey) noexcept
    {
        X25519SharedSecret output;
        auto               result = DeriveX25519SharedSecretInto(context, privateKey, peerPublicKey, output.UnsafeMutableBytes());
        if (!result.HasValue())
        {
            return result.Error();
        }

        return output;
    }
}// namespace NGIN::Crypto::Asymmetric
