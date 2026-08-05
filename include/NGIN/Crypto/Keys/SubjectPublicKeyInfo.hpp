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

    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<SubjectPublicKeyInfo> ParseSubjectPublicKeyInfo(ConstByteSpan der);
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<ByteBuffer> WriteSubjectPublicKeyInfo(
            KeyAlgorithm algorithm, ConstByteSpan publicKey);

    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<NGIN::Crypto::Asymmetric::Ed25519PublicKey> ImportEd25519PublicKey(
            const SubjectPublicKeyInfo& publicKeyInfo) noexcept;
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<NGIN::Crypto::Asymmetric::X25519PublicKey> ImportX25519PublicKey(
            const SubjectPublicKeyInfo& publicKeyInfo) noexcept;
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<NGIN::Crypto::Asymmetric::EcdsaP256PublicKey> ImportEcdsaP256PublicKey(
            const SubjectPublicKeyInfo& publicKeyInfo) noexcept;

    [[nodiscard]] NGIN_CRYPTO_API SubjectPublicKeyInfo ExportSubjectPublicKeyInfo(
            const NGIN::Crypto::Asymmetric::Ed25519PublicKey& publicKey);
    [[nodiscard]] NGIN_CRYPTO_API SubjectPublicKeyInfo ExportSubjectPublicKeyInfo(
            const NGIN::Crypto::Asymmetric::X25519PublicKey& publicKey);
    [[nodiscard]] NGIN_CRYPTO_API SubjectPublicKeyInfo ExportSubjectPublicKeyInfo(
            const NGIN::Crypto::Asymmetric::EcdsaP256PublicKey& publicKey);
}// namespace NGIN::Crypto::Keys
