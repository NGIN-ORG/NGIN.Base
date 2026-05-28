#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto
{
    /// @brief Broad algorithm families used for capability reporting and diagnostics.
    enum class AlgorithmFamily
    {
        Random,
        Hash,
        Mac,
        Kdf,
        Aead,
        KeyAgreement,
        Signature,
        Encoding,
        Certificate,
        Token,
    };

    /// @brief Backend-neutral hash algorithm identifiers.
    enum class HashAlgorithm : NGIN::UInt8
    {
        Sha256,
        Sha512,
        Sha3_256,
        Sha3_512,
        Blake3,
    };

    /// @brief Backend-neutral message authentication algorithm identifiers.
    enum class MacAlgorithm : NGIN::UInt8
    {
        HmacSha256,
        HmacSha512,
    };

    /// @brief Backend-neutral key derivation algorithm identifiers.
    enum class KdfAlgorithm : NGIN::UInt8
    {
        HkdfSha256,
        HkdfSha512,
        Pbkdf2Sha256,
        Pbkdf2Sha512,
        Argon2id,
    };

    /// @brief Backend-neutral authenticated encryption algorithm identifiers.
    enum class AeadAlgorithm : NGIN::UInt8
    {
        Aes128Gcm,
        Aes256Gcm,
        ChaCha20Poly1305,
        XChaCha20Poly1305,
    };

    /// @brief Backend-neutral key agreement algorithm identifiers.
    enum class KeyAgreementAlgorithm : NGIN::UInt8
    {
        X25519,
    };

    /// @brief Backend-neutral signature algorithm identifiers.
    enum class SignatureAlgorithm : NGIN::UInt8
    {
        Ed25519,
        EcdsaP256Sha256,
        RsaPssSha256,
    };
}// namespace NGIN::Crypto
