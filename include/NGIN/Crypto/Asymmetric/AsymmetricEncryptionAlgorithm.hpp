/// @file AsymmetricEncryptionAlgorithm.hpp
/// @brief Backend-neutral asymmetric-encryption algorithm identifiers.
#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto
{
    /// @brief Backend-neutral asymmetric-encryption algorithm identifiers.
    enum class AsymmetricEncryptionAlgorithm : NGIN::UInt8
    {
        RsaOaepSha256,
    };
}// namespace NGIN::Crypto
