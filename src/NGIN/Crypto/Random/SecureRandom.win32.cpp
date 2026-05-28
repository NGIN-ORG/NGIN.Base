#include <NGIN/Crypto/Random/SecureRandom.hpp>

#include <NGIN/Crypto/Random/RandomError.hpp>

#include <algorithm>
#include <limits>

#include <bcrypt.h>

namespace NGIN::Crypto::Random
{
    CryptoExpected<void> Fill(ByteSpan output) noexcept
    {
        auto* bytes     = output.data();
        auto  remaining = output.size();

        while (remaining > 0)
        {
            const auto chunk = static_cast<ULONG>(
                    std::min<NGIN::UIntSize>(remaining, static_cast<NGIN::UIntSize>(std::numeric_limits<ULONG>::max())));

            const auto status = BCryptGenRandom(
                    nullptr,
                    reinterpret_cast<PUCHAR>(bytes),
                    chunk,
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            if (status != 0)
            {
                return EntropyUnavailableError(static_cast<NGIN::Int32>(status));
            }

            bytes += chunk;
            remaining -= chunk;
        }

        return {};
    }
}// namespace NGIN::Crypto::Random
