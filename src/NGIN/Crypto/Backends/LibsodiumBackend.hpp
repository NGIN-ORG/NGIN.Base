#pragma once

#include <NGIN/Crypto/Backend/CryptoContext.hpp>

namespace NGIN::Crypto::Backend::detail
{
    [[nodiscard]] CryptoExpected<CryptoContext> CreateLibsodiumContext(const BackendOptions& options) noexcept;

    [[nodiscard]] CryptoExpected<void> RandomLibsodium(ByteSpan output) noexcept;

    [[nodiscard]] CryptoExpected<void> Argon2idLibsodium(
            NGIN::Crypto::Memory::SecretView password,
            ConstByteSpan                    salt,
            NGIN::UInt32                     memoryKiB,
            NGIN::UInt32                     iterations,
            NGIN::UInt32                     parallelism,
            ByteSpan                         output) noexcept;

    [[nodiscard]] CryptoExpected<std::string> HashPasswordLibsodium(
            NGIN::Crypto::Memory::SecretView password,
            NGIN::UInt32                     memoryKiB,
            NGIN::UInt32                     iterations,
            NGIN::UInt32                     parallelism);

    [[nodiscard]] CryptoExpected<void> VerifyPasswordHashLibsodium(
            NGIN::Crypto::Memory::SecretView password,
            std::string_view                 encodedHash) noexcept;

    [[nodiscard]] CryptoExpected<bool> PasswordHashNeedsRehashLibsodium(
            std::string_view encodedHash,
            NGIN::UInt32     memoryKiB,
            NGIN::UInt32     iterations,
            NGIN::UInt32     parallelism) noexcept;

    [[nodiscard]] CryptoExpected<void> AeadSealLibsodium(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    plaintext,
            ConstByteSpan                    associatedData,
            ByteSpan                         ciphertext,
            ByteSpan                         tag) noexcept;

    [[nodiscard]] CryptoExpected<void> AeadOpenLibsodium(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    ciphertext,
            ConstByteSpan                    associatedData,
            ConstByteSpan                    tag,
            ByteSpan                         plaintext) noexcept;

    [[nodiscard]] CryptoExpected<void> Blake2bLibsodium(
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    input,
            ByteSpan                         output) noexcept;

    [[nodiscard]] CryptoExpected<void> XChaCha20XorLibsodium(
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    input,
            ByteSpan                         output) noexcept;

    [[nodiscard]] CryptoExpected<void> GenerateEd25519KeyPairLibsodium(
            ByteSpan publicKey,
            ByteSpan privateKey) noexcept;

    [[nodiscard]] CryptoExpected<void> SignLibsodium(
            SignatureAlgorithm               algorithm,
            NGIN::Crypto::Memory::SecretView privateKey,
            ConstByteSpan                    message,
            ByteSpan                         signature) noexcept;

    [[nodiscard]] CryptoExpected<void> VerifySignatureLibsodium(
            SignatureAlgorithm algorithm,
            ConstByteSpan      publicKey,
            ConstByteSpan      message,
            ConstByteSpan      signature) noexcept;

    [[nodiscard]] CryptoExpected<void> GenerateX25519KeyPairLibsodium(
            ByteSpan publicKey,
            ByteSpan privateKey) noexcept;

    [[nodiscard]] CryptoExpected<void> DeriveX25519SharedSecretLibsodium(
            NGIN::Crypto::Memory::SecretView privateKey,
            ConstByteSpan                    peerPublicKey,
            ByteSpan                         output) noexcept;
}// namespace NGIN::Crypto::Backend::detail
