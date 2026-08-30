/// @file KeySizes.hpp
/// @brief Fixed encoded sizes for asymmetric keys, signatures, and shared secrets.
#pragma once

#include <NGIN/Crypto/Asymmetric/KeyAgreementAlgorithm.hpp>
#include <NGIN/Crypto/Signatures/SignatureAlgorithm.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto::Asymmetric
{
    /// @brief Fixed encoded sizes for a signature algorithm's key and signature material.
    struct SignatureKeySizes
    {
        NGIN::UIntSize publicKeySize {0};
        NGIN::UIntSize privateKeySize {0};
        NGIN::UIntSize signatureSize {0};
    };

    /// @brief Fixed encoded sizes for a key-agreement algorithm.
    struct KeyAgreementSizes
    {
        NGIN::UIntSize publicKeySize {0};
        NGIN::UIntSize privateKeySize {0};
        NGIN::UIntSize sharedSecretSize {0};
    };

    /// @brief Returns fixed key and signature sizes for an algorithm.
    /// @return Zero sizes when the algorithm uses variable-size encodings.
    [[nodiscard]] constexpr SignatureKeySizes GetSignatureKeySizes(SignatureAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
            case SignatureAlgorithm::Ed25519:
                return SignatureKeySizes {.publicKeySize = 32, .privateKeySize = 32, .signatureSize = 64};
            case SignatureAlgorithm::EcdsaP256Sha256:
                return SignatureKeySizes {.publicKeySize = 65, .privateKeySize = 32, .signatureSize = 64};
            case SignatureAlgorithm::RsaPssSha256:
                return {};
        }

        return {};
    }

    /// @brief Returns fixed key and shared-secret sizes for an algorithm.
    [[nodiscard]] constexpr KeyAgreementSizes GetKeyAgreementSizes(KeyAgreementAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
            case KeyAgreementAlgorithm::X25519:
                return KeyAgreementSizes {.publicKeySize = 32, .privateKeySize = 32, .sharedSecretSize = 32};
        }

        return {};
    }
}// namespace NGIN::Crypto::Asymmetric
