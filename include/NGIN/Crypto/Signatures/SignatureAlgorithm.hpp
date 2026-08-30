/// @file SignatureAlgorithm.hpp
/// @brief Backend-neutral signature algorithm identifiers.
#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto
{
    /// @brief Backend-neutral signature algorithm identifiers.
    enum class SignatureAlgorithm : NGIN::UInt8
    {
        Ed25519,
        EcdsaP256Sha256,
        RsaPssSha256,
    };
}// namespace NGIN::Crypto
