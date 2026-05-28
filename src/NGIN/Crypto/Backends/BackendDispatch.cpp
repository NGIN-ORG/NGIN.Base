#include <NGIN/Crypto/Backend/CryptoContext.hpp>

#include <NGIN/Crypto/Random/SecureRandom.hpp>

namespace NGIN::Crypto::Backend
{
    CryptoExpected<void> CryptoContext::FillRandom(ByteSpan output) const noexcept
    {
        if (!SupportsRandom())
        {
            return CryptoError {CryptoErrorCode::UnsupportedBackend};
        }

        return NGIN::Crypto::Random::Fill(output);
    }

    CryptoExpected<CryptoContext> CreateContext(const BackendOptions& options) noexcept
    {
        if (options.requireSecureRandom && !NGIN::Crypto::Random::IsAvailable())
        {
            return CryptoError {CryptoErrorCode::BackendUnavailable};
        }

        BackendCapabilities capabilities;
        capabilities.EnableRandom();

        return CryptoContext {BackendInfo {BackendKind::Platform, "platform-random"}, capabilities};
    }
}// namespace NGIN::Crypto::Backend
