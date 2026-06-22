#pragma once

#include <NGIN/Crypto/Asymmetric/Ecdsa.hpp>
#include <NGIN/Crypto/Asymmetric/Ed25519.hpp>
#include <NGIN/Crypto/Asymmetric/X25519.hpp>
#include <NGIN/Crypto/Keys/KeyFormat.hpp>
#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Keys
{
    /// @brief Parsed PKCS#8 PrivateKeyInfo carrying the raw privateKey OCTET STRING contents.
    struct PrivateKeyInfo
    {
        NGIN::UInt32           version {0};
        KeyAlgorithmIdentifier algorithm;
        ByteBuffer             privateKey;
    };

    /// @brief Parsed AlgorithmIdentifier for encrypted private-key envelopes.
    ///
    /// @details The identifier preserves raw DER parameter bytes but does not interpret PBES/PBKDF/cipher policy.
    struct EncryptedPrivateKeyAlgorithmIdentifier
    {
        NGIN::Containers::Vector<NGIN::UInt32> objectIdentifier;
        ByteBuffer                             parameters;
        bool                                   hasParameters {false};
    };

    /// @brief Parsed PKCS#8 EncryptedPrivateKeyInfo carrying encrypted payload bytes.
    ///
    /// @details This is a data-only envelope. Decryption and password policy are intentionally not performed here.
    struct EncryptedPrivateKeyInfo
    {
        EncryptedPrivateKeyAlgorithmIdentifier encryptionAlgorithm;
        ByteBuffer                             encryptedData;
    };

    [[nodiscard]] CryptoExpected<PrivateKeyInfo> ParsePrivateKeyInfo(ConstByteSpan der);
    [[nodiscard]] CryptoExpected<ByteBuffer>     WritePrivateKeyInfo(KeyAlgorithm algorithm, ConstByteSpan privateKey);

    [[nodiscard]] CryptoExpected<EncryptedPrivateKeyInfo> ParseEncryptedPrivateKeyInfo(ConstByteSpan der);
    [[nodiscard]] CryptoExpected<ByteBuffer> WriteEncryptedPrivateKeyInfo(
            const EncryptedPrivateKeyAlgorithmIdentifier& encryptionAlgorithm,
            ConstByteSpan encryptedData);

    [[nodiscard]] CryptoExpected<NGIN::Crypto::Asymmetric::Ed25519PrivateKey> ImportEd25519PrivateKey(
            const PrivateKeyInfo& privateKeyInfo) noexcept;
    [[nodiscard]] CryptoExpected<NGIN::Crypto::Asymmetric::X25519PrivateKey> ImportX25519PrivateKey(
            const PrivateKeyInfo& privateKeyInfo) noexcept;
    [[nodiscard]] CryptoExpected<NGIN::Crypto::Asymmetric::EcdsaP256PrivateKey> ImportEcdsaP256PrivateKey(
            const PrivateKeyInfo& privateKeyInfo) noexcept;

    [[nodiscard]] PrivateKeyInfo ExportPrivateKeyInfo(const NGIN::Crypto::Asymmetric::Ed25519PrivateKey& privateKey);
    [[nodiscard]] PrivateKeyInfo ExportPrivateKeyInfo(const NGIN::Crypto::Asymmetric::X25519PrivateKey& privateKey);
    [[nodiscard]] PrivateKeyInfo ExportPrivateKeyInfo(const NGIN::Crypto::Asymmetric::EcdsaP256PrivateKey& privateKey);
}// namespace NGIN::Crypto::Keys
