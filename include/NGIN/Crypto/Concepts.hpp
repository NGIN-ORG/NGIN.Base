#pragma once

#include <NGIN/Primitives.hpp>

#include <concepts>

namespace NGIN::Crypto
{
    /// @brief Accepts byte-like scalar types that can be used for explicit byte conversions.
    template<class T>
    concept ByteLikeConcept = std::same_as<T, NGIN::Byte> || std::same_as<T, NGIN::UInt8>;
}// namespace NGIN::Crypto
