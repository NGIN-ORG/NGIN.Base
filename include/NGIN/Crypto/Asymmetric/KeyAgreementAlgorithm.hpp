/// @file KeyAgreementAlgorithm.hpp
/// @brief Backend-neutral key-agreement algorithm identifiers.
#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto
{
    /// @brief Backend-neutral key-agreement algorithm identifiers.
    enum class KeyAgreementAlgorithm : NGIN::UInt8
    {
        X25519,
    };
}// namespace NGIN::Crypto
