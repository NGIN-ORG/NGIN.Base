#include <NGIN/Crypto/Random/SecureRandom.hpp>

#include <NGIN/Crypto/Random/RandomError.hpp>

#include <cerrno>
#include <sys/random.h>

namespace NGIN::Crypto::Random
{
    CryptoExpected<void> Fill(ByteSpan output) noexcept
    {
        auto* bytes     = output.data();
        auto  remaining = output.size();

        while (remaining > 0)
        {
            const auto received = getrandom(bytes, remaining, 0);
            if (received < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                return EntropyUnavailableError(errno);
            }

            if (received == 0)
            {
                return EntropyUnavailableError();
            }

            const auto receivedSize = static_cast<NGIN::UIntSize>(received);
            bytes += receivedSize;
            remaining -= receivedSize;
        }

        return {};
    }
}// namespace NGIN::Crypto::Random
