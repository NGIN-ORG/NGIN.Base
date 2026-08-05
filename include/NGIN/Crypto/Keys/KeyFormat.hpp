/// @file KeyFormat.hpp
/// @brief Mappings between DER key algorithms and operation algorithms.
#pragma once

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Crypto/Algorithm.hpp>
#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Result.hpp>

namespace NGIN::Crypto::Keys
{
    /// @brief Backend-neutral key algorithm identifiers used by DER key-format parsers.
    enum class KeyAlgorithm : NGIN::UInt8
    {
        Unknown,
        Ed25519,
        X25519,
        EcdsaP256,
        Rsa,
    };

    /// @brief Parsed ASN.1 AlgorithmIdentifier with raw parameter bytes preserved when present.
    struct KeyAlgorithmIdentifier
    {
        KeyAlgorithm                           algorithm {KeyAlgorithm::Unknown};
        NGIN::Containers::Vector<NGIN::UInt32> objectIdentifier;
        ByteBuffer                             parameters;
        bool                                   hasParameters {false};
    };

    /// @brief Maps a key algorithm to its signature operation algorithm.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<SignatureAlgorithm> ToSignatureAlgorithm(
            KeyAlgorithm algorithm) noexcept;
    /// @brief Maps a key algorithm to its key-agreement operation algorithm.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<KeyAgreementAlgorithm> ToKeyAgreementAlgorithm(
            KeyAlgorithm algorithm) noexcept;
    /// @brief Maps a signature operation algorithm to its key algorithm.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<KeyAlgorithm> FromSignatureAlgorithm(
            SignatureAlgorithm algorithm) noexcept;
    /// @brief Maps a key-agreement operation algorithm to its key algorithm.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<KeyAlgorithm> FromKeyAgreementAlgorithm(
            KeyAgreementAlgorithm algorithm) noexcept;
}// namespace NGIN::Crypto::Keys
