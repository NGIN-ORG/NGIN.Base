#pragma once

#include <NGIN/Crypto/Backend/CryptoContext.hpp>

namespace NGIN::Crypto::Backend::detail
{
    [[nodiscard]] CryptoExpected<CryptoContext> CreateCngContext(const BackendOptions& options) noexcept;

    [[nodiscard]] CryptoExpected<void> HashCng(
            HashAlgorithm algorithm,
            ConstByteSpan input,
            ByteSpan      output) noexcept;

    [[nodiscard]] CryptoExpected<void> MacCng(
            MacAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    input,
            ByteSpan                         output) noexcept;

    [[nodiscard]] CryptoExpected<void> Pbkdf2Cng(
            KdfAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView password,
            ConstByteSpan                    salt,
            NGIN::UInt32                     iterations,
            ByteSpan                         output) noexcept;

    [[nodiscard]] CryptoExpected<void> AeadSealCng(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    plaintext,
            ConstByteSpan                    associatedData,
            ByteSpan                         ciphertext,
            ByteSpan                         tag) noexcept;

    [[nodiscard]] CryptoExpected<void> AeadOpenCng(
            AeadAlgorithm                    algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    nonce,
            ConstByteSpan                    ciphertext,
            ConstByteSpan                    associatedData,
            ConstByteSpan                    tag,
            ByteSpan                         plaintext) noexcept;
}// namespace NGIN::Crypto::Backend::detail
