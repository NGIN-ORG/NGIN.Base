/// @file HashAlgorithm.hpp
/// @brief Backend-neutral hash algorithm identifiers.
#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto
{
    /// @brief Backend-neutral hash algorithm identifiers.
    enum class HashAlgorithm : NGIN::UInt8
    {
        Sha256,
        Sha512,
        Sha3_256,
        Sha3_512,
        Blake3,
    };
}// namespace NGIN::Crypto
