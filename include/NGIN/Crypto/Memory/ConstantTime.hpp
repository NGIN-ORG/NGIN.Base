#pragma once

#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Memory
{
    /// @brief Compares equal-length buffers without data-dependent early exit.
    [[nodiscard]] NGIN_CRYPTO_API bool ConstantTimeEqual(ConstByteSpan left, ConstByteSpan right) noexcept;
}// namespace NGIN::Crypto::Memory
