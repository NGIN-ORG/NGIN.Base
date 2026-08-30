/// @file KdfAlgorithm.hpp
/// @brief Backend-neutral key-derivation algorithm identifiers.
#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto
{
    /// @brief Backend-neutral key-derivation algorithm identifiers.
    enum class KdfAlgorithm : NGIN::UInt8
    {
        HkdfSha256,
        HkdfSha512,
        Pbkdf2Sha256,
        Pbkdf2Sha512,
        Argon2id,
    };
}// namespace NGIN::Crypto
