#include <NGIN/Crypto/Asymmetric/Ed25519.hpp>

#include <NGIN/Crypto/Errors/CryptoError.hpp>

namespace NGIN::Crypto::Asymmetric
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError InternalError() noexcept
        {
            return CryptoError {CryptoErrorCode::InternalError};
        }
    }// namespace

    CryptoExpected<Ed25519KeyPair> GenerateEd25519KeyPair(
            const NGIN::Crypto::Backend::CryptoContext& context) noexcept
    {
        auto supported = context.EnsureSupports(SignatureAlgorithm::Ed25519);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

        FixedBytes<32> publicKey {};
        auto           privateKey = NGIN::Crypto::Memory::FixedSecret<32> {};
        auto           result     = context.GenerateEd25519KeyPairInto(publicKey, privateKey.UnsafeMutableBytes());
        if (!result.HasValue())
        {
            return result.Error();
        }

        if (privateKey.Bytes().size() != Ed25519PrivateKey::SizeValue)
        {
            return InternalError();
        }

        return Ed25519KeyPair {
                .publicKey  = Ed25519PublicKey::FromBytes(publicKey),
                .privateKey = Ed25519PrivateKey {std::move(privateKey)},
        };
    }
}// namespace NGIN::Crypto::Asymmetric
