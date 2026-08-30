/// @file MacAlgorithm.hpp
/// @brief Backend-neutral message-authentication algorithm identifiers.
#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto
{
    /// @brief Backend-neutral message-authentication algorithm identifiers.
    enum class MacAlgorithm : NGIN::UInt8
    {
        HmacSha256,
        HmacSha512,
    };
}// namespace NGIN::Crypto
