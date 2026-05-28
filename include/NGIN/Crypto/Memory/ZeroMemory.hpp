#pragma once

#include <NGIN/Crypto/Types.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto::Memory
{
    /// @brief Clears memory through an optimizer-resistant erase primitive.
    void SecureZero(void* data, NGIN::UIntSize size) noexcept;

    /// @brief Clears a mutable byte span through an optimizer-resistant erase primitive.
    inline void SecureZero(ByteSpan bytes) noexcept
    {
        SecureZero(bytes.data(), bytes.size());
    }
}// namespace NGIN::Crypto::Memory
