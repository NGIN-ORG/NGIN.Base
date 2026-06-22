#pragma once

#include <NGIN/Crypto/Kdf/KeyDerivation.hpp>

namespace NGIN::Crypto::Kdf
{
    /// @brief Derives PBKDF2-HMAC-SHA256 key material.
    [[nodiscard]] inline CryptoExpected<void> Pbkdf2Sha256Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const Pbkdf2Parameters&                     parameters,
            ByteSpan                                    output) noexcept
    {
        return DeriveKeyInto(context, KeyDerivationParameters {KdfAlgorithm::Pbkdf2Sha256, parameters}, output);
    }

    /// @brief Derives PBKDF2-HMAC-SHA512 key material.
    [[nodiscard]] inline CryptoExpected<void> Pbkdf2Sha512Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const Pbkdf2Parameters&                     parameters,
            ByteSpan                                    output) noexcept
    {
        return DeriveKeyInto(context, KeyDerivationParameters {KdfAlgorithm::Pbkdf2Sha512, parameters}, output);
    }

    /// @brief Derives PBKDF2-HMAC-SHA256 key material into owned non-secret storage.
    [[nodiscard]] inline CryptoExpected<ByteBuffer> Pbkdf2Sha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const Pbkdf2Parameters&                     parameters,
            NGIN::UIntSize                              outputSize)
    {
        return DeriveKey(context, KeyDerivationParameters {KdfAlgorithm::Pbkdf2Sha256, parameters}, outputSize);
    }

    /// @brief Derives PBKDF2-HMAC-SHA512 key material into owned non-secret storage.
    [[nodiscard]] inline CryptoExpected<ByteBuffer> Pbkdf2Sha512(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const Pbkdf2Parameters&                     parameters,
            NGIN::UIntSize                              outputSize)
    {
        return DeriveKey(context, KeyDerivationParameters {KdfAlgorithm::Pbkdf2Sha512, parameters}, outputSize);
    }

    /// @brief Derives PBKDF2-HMAC-SHA256 key material into fixed-size secret storage.
    template<NGIN::UIntSize Size>
    [[nodiscard]] inline CryptoExpected<NGIN::Crypto::Memory::FixedSecret<Size>> Pbkdf2Sha256Secret(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const Pbkdf2Parameters&                     parameters)
    {
        return DeriveFixedSecret<Size>(context, KeyDerivationParameters {KdfAlgorithm::Pbkdf2Sha256, parameters});
    }

    /// @brief Derives PBKDF2-HMAC-SHA512 key material into fixed-size secret storage.
    template<NGIN::UIntSize Size>
    [[nodiscard]] inline CryptoExpected<NGIN::Crypto::Memory::FixedSecret<Size>> Pbkdf2Sha512Secret(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const Pbkdf2Parameters&                     parameters)
    {
        return DeriveFixedSecret<Size>(context, KeyDerivationParameters {KdfAlgorithm::Pbkdf2Sha512, parameters});
    }
}// namespace NGIN::Crypto::Kdf
