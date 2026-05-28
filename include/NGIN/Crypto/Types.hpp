#pragma once

#include <NGIN/Primitives.hpp>

#include <array>
#include <span>

namespace NGIN::Crypto
{
    /// @brief Mutable view over bytes.
    using ByteSpan = std::span<NGIN::Byte>;

    /// @brief Immutable view over bytes.
    using ConstByteSpan = std::span<const NGIN::Byte>;

    /// @brief Fixed-size byte array used for digests, keys, nonces, tags, and signatures.
    template<NGIN::UIntSize Size>
    using FixedBytes = std::array<NGIN::Byte, Size>;
}// namespace NGIN::Crypto
