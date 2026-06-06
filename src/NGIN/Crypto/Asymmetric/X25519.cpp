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

        [[nodiscard]] constexpr CryptoError InternalError() noexcept
        {
            return CryptoError {CryptoErrorCode::InternalError};
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

        FixedBytes<32> publicKey {};
        auto           privateKey = NGIN::Crypto::Memory::FixedSecret<32> {};
        auto           result     = context.GenerateX25519KeyPairInto(publicKey, privateKey.UnsafeMutableBytes());
        if (!result.HasValue())
        {
            return result.Error();
        }

        if (privateKey.Bytes().size() != X25519PrivateKey::SizeValue)
        {
            return InternalError();
        }

        return X25519KeyPair {
                .publicKey  = X25519PublicKey::FromBytes(publicKey),
                .privateKey = X25519PrivateKey {std::move(privateKey)},
        };
    }

    CryptoExpected<void> DeriveX25519SharedSecretInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const X25519PrivateKey&                     privateKey,
            const X25519PublicKey&                      peerPublicKey,
            ByteSpan                                    output) noexcept
    {
        if (output.size() != GetKeyAgreementSizes(KeyAgreementAlgorithm::X25519).sharedSecretSize)
        {
            return OutputBufferTooSmall();
        }

        auto supported = context.EnsureSupports(KeyAgreementAlgorithm::X25519);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

        return context.DeriveX25519SharedSecretInto(
                NGIN::Crypto::Memory::SecretView {privateKey.Bytes()},
                peerPublicKey.Bytes(),
                output);
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
