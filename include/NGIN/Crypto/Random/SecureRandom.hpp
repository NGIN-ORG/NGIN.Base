#pragma once

#include <NGIN/Crypto/Result.hpp>
#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Random
{
    /// @brief Returns whether the platform secure random source is available.
    [[nodiscard]] NGIN_CRYPTO_API bool IsAvailable() noexcept;

    /// @brief Fills `output` with bytes from the platform secure random source.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<void> Fill(ByteSpan output) noexcept;
}// namespace NGIN::Crypto::Random
