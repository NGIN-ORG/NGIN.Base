/// @file SubjectPublicKeyInfo.hpp
/// @brief X.509 SubjectPublicKeyInfo parsing, writing, and typed key conversion.
#pragma once

#include <NGIN/Crypto/Asymmetric/Ecdsa.hpp>
#include <NGIN/Crypto/Asymmetric/Ed25519.hpp>
#include <NGIN/Crypto/Asymmetric/X25519.hpp>
#include <NGIN/Crypto/Keys/KeyFormat.hpp>
#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Keys
{
    /// @brief Parsed X.509 SubjectPublicKeyInfo carrying raw public-key bytes.
    struct SubjectPublicKeyInfo
    {
        KeyAlgorithmIdentifier algorithm;
        ByteBuffer             publicKey;
    };

    /// @brief Parses a DER X.509 SubjectPublicKeyInfo envelope.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<SubjectPublicKeyInfo> ParseSubjectPublicKeyInfo(ConstByteSpan der);
    /// @brief Writes raw public-key bytes into a DER SubjectPublicKeyInfo envelope.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<ByteBuffer> WriteSubjectPublicKeyInfo(
            KeyAlgorithm algorithm, ConstByteSpan publicKey);

    /// @brief Imports an Ed25519 public key from a matching SubjectPublicKeyInfo.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<NGIN::Crypto::Asymmetric::Ed25519PublicKey> ImportEd25519PublicKey(
            const SubjectPublicKeyInfo& publicKeyInfo) noexcept;
    /// @brief Imports an X25519 public key from a matching SubjectPublicKeyInfo.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<NGIN::Crypto::Asymmetric::X25519PublicKey> ImportX25519PublicKey(
            const SubjectPublicKeyInfo& publicKeyInfo) noexcept;
    /// @brief Imports an ECDSA P-256 public key from a matching SubjectPublicKeyInfo.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<NGIN::Crypto::Asymmetric::EcdsaP256PublicKey> ImportEcdsaP256PublicKey(
            const SubjectPublicKeyInfo& publicKeyInfo) noexcept;

    /// @brief Exports an Ed25519 public key as an in-memory SubjectPublicKeyInfo.
    [[nodiscard]] NGIN_CRYPTO_API SubjectPublicKeyInfo ExportSubjectPublicKeyInfo(
            const NGIN::Crypto::Asymmetric::Ed25519PublicKey& publicKey);
    /// @brief Exports an X25519 public key as an in-memory SubjectPublicKeyInfo.
    [[nodiscard]] NGIN_CRYPTO_API SubjectPublicKeyInfo ExportSubjectPublicKeyInfo(
            const NGIN::Crypto::Asymmetric::X25519PublicKey& publicKey);
    /// @brief Exports an ECDSA P-256 public key as an in-memory SubjectPublicKeyInfo.
    [[nodiscard]] NGIN_CRYPTO_API SubjectPublicKeyInfo ExportSubjectPublicKeyInfo(
            const NGIN::Crypto::Asymmetric::EcdsaP256PublicKey& publicKey);
}// namespace NGIN::Crypto::Keys
