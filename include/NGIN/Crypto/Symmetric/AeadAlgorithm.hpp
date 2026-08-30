/// @file AeadAlgorithm.hpp
/// @brief Backend-neutral authenticated-encryption algorithm identifiers.
#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto
{
    /// @brief Backend-neutral authenticated-encryption algorithm identifiers.
    enum class AeadAlgorithm : NGIN::UInt8
    {
        Aes128Gcm,
        Aes256Gcm,
        ChaCha20Poly1305,
        XChaCha20Poly1305,
    };
}// namespace NGIN::Crypto
