#include <NGIN/Crypto/Random/SecureRandom.hpp>

#include <NGIN/Crypto/Random/RandomError.hpp>

#include <cstdint>

#include <Security/SecRandom.h>

namespace NGIN::Crypto::Random
{
    CryptoExpected<void> Fill(ByteSpan output) noexcept
    {
        if (output.empty())
        {
            return {};
        }

        const auto status = SecRandomCopyBytes(kSecRandomDefault, output.size(), reinterpret_cast<std::uint8_t*>(output.data()));
        if (status != errSecSuccess)
        {
            return EntropyUnavailableError(static_cast<NGIN::Int32>(status));
        }

        return {};
    }
}// namespace NGIN::Crypto::Random
