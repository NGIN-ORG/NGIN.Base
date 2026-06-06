#pragma once

#include <NGIN/Crypto/Backend/CryptoContext.hpp>

namespace NGIN::Crypto::Backend::detail
{
    [[nodiscard]] CryptoExpected<CryptoContext> CreateOpenSslContext(const BackendOptions& options) noexcept;

    [[nodiscard]] CryptoExpected<void> HashOpenSsl(
            HashAlgorithm algorithm,
            ConstByteSpan input,
            ByteSpan      output) noexcept;

    [[nodiscard]] CryptoExpected<void> MacOpenSsl(
            MacAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    input,
            ByteSpan                         output) noexcept;

    [[nodiscard]] CryptoExpected<void> HkdfOpenSsl(
            KdfAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView inputKeyMaterial,
            ConstByteSpan                    salt,
            ConstByteSpan                    info,
            ByteSpan                         output) noexcept;

    [[nodiscard]] CryptoExpected<void> Pbkdf2OpenSsl(
            KdfAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView password,
            ConstByteSpan                    salt,
            NGIN::UInt32                     iterations,
            ByteSpan                         output) noexcept;

    [[nodiscard]] CryptoExpected<void> AeadSealOpenSsl(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    plaintext,
            ConstByteSpan                    associatedData,
            ByteSpan                         ciphertext,
            ByteSpan                         tag) noexcept;

    [[nodiscard]] CryptoExpected<void> AeadOpenOpenSsl(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    ciphertext,
            ConstByteSpan                    associatedData,
            ConstByteSpan                    tag,
            ByteSpan                         plaintext) noexcept;

    [[nodiscard]] CryptoExpected<void> GenerateEd25519KeyPairOpenSsl(
            ByteSpan publicKey,
            ByteSpan privateKey) noexcept;

    [[nodiscard]] CryptoExpected<void> SignOpenSsl(
            SignatureAlgorithm               algorithm,
            NGIN::Crypto::Memory::SecretView privateKey,
            ConstByteSpan                    message,
            ByteSpan                         signature) noexcept;

    [[nodiscard]] CryptoExpected<void> VerifySignatureOpenSsl(
            SignatureAlgorithm algorithm,
            ConstByteSpan      publicKey,
            ConstByteSpan      message,
            ConstByteSpan      signature) noexcept;

    [[nodiscard]] CryptoExpected<void> GenerateX25519KeyPairOpenSsl(
            ByteSpan publicKey,
            ByteSpan privateKey) noexcept;

    [[nodiscard]] CryptoExpected<void> DeriveX25519SharedSecretOpenSsl(
            NGIN::Crypto::Memory::SecretView privateKey,
            ConstByteSpan                    peerPublicKey,
            ByteSpan                         output) noexcept;
}// namespace NGIN::Crypto::Backend::detail
