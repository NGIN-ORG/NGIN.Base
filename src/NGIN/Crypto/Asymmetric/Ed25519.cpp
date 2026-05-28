#include <NGIN/Crypto/Asymmetric/Ed25519.hpp>

#include <NGIN/Crypto/Errors/CryptoError.hpp>

namespace NGIN::Crypto::Asymmetric
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError UnsupportedAlgorithm() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
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

        return UnsupportedAlgorithm();
    }
}// namespace NGIN::Crypto::Asymmetric
