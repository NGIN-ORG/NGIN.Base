#include <NGIN/Crypto/Random/SecureRandom.hpp>

#include <NGIN/Crypto/Random/RandomError.hpp>

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace NGIN::Crypto::Random
{
    CryptoExpected<void> Fill(ByteSpan output) noexcept
    {
        if (output.empty())
        {
            return {};
        }

        const int fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0)
        {
            return EntropyUnavailableError(errno);
        }

        auto* bytes     = output.data();
        auto  remaining = output.size();

        while (remaining > 0)
        {
            const auto received = read(fd, bytes, remaining);
            if (received < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                const auto error = errno;
                close(fd);
                return EntropyUnavailableError(error);
            }

            if (received == 0)
            {
                close(fd);
                return EntropyUnavailableError();
            }

            const auto receivedSize = static_cast<NGIN::UIntSize>(received);
            bytes += receivedSize;
            remaining -= receivedSize;
        }

        close(fd);
        return {};
    }
}// namespace NGIN::Crypto::Random
