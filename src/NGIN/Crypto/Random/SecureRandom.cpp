#include <NGIN/Crypto/Random/EntropySource.hpp>
#include <NGIN/Crypto/Random/SecureRandom.hpp>

namespace NGIN::Crypto::Random
{
    namespace
    {
        [[nodiscard]] CryptoExpected<void> FillPlatformEntropy(void*, ByteSpan output) noexcept
        {
            return Fill(output);
        }
    }// namespace

    bool IsAvailable() noexcept
    {
        return true;
    }

    EntropySource PlatformEntropySource() noexcept
    {
        return EntropySource {nullptr, FillPlatformEntropy, true};
    }
}// namespace NGIN::Crypto::Random
