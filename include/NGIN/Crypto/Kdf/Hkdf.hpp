#pragma once

#include <NGIN/Crypto/Kdf/KeyDerivation.hpp>

namespace NGIN::Crypto::Kdf
{
    /// @brief Derives HKDF-SHA256 key material.
    [[nodiscard]] inline CryptoExpected<void> HkdfSha256Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const HkdfParameters&                       parameters,
            ByteSpan                                    output) noexcept
    {
        return DeriveKeyInto(context, KeyDerivationParameters {KdfAlgorithm::HkdfSha256, parameters}, output);
    }

    /// @brief Derives HKDF-SHA512 key material.
    [[nodiscard]] inline CryptoExpected<void> HkdfSha512Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const HkdfParameters&                       parameters,
            ByteSpan                                    output) noexcept
    {
        return DeriveKeyInto(context, KeyDerivationParameters {KdfAlgorithm::HkdfSha512, parameters}, output);
    }

    /// @brief Derives HKDF-SHA256 key material into owned non-secret storage.
    [[nodiscard]] inline CryptoExpected<ByteBuffer> HkdfSha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const HkdfParameters&                       parameters,
            NGIN::UIntSize                              outputSize)
    {
        return DeriveKey(context, KeyDerivationParameters {KdfAlgorithm::HkdfSha256, parameters}, outputSize);
    }

    /// @brief Derives HKDF-SHA512 key material into owned non-secret storage.
    [[nodiscard]] inline CryptoExpected<ByteBuffer> HkdfSha512(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const HkdfParameters&                       parameters,
            NGIN::UIntSize                              outputSize)
    {
        return DeriveKey(context, KeyDerivationParameters {KdfAlgorithm::HkdfSha512, parameters}, outputSize);
    }

    /// @brief Derives HKDF-SHA256 key material into fixed-size secret storage.
    template<NGIN::UIntSize Size>
    [[nodiscard]] inline CryptoExpected<NGIN::Crypto::Memory::FixedSecret<Size>> HkdfSha256Secret(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const HkdfParameters&                       parameters)
    {
        return DeriveFixedSecret<Size>(context, KeyDerivationParameters {KdfAlgorithm::HkdfSha256, parameters});
    }

    /// @brief Derives HKDF-SHA512 key material into fixed-size secret storage.
    template<NGIN::UIntSize Size>
    [[nodiscard]] inline CryptoExpected<NGIN::Crypto::Memory::FixedSecret<Size>> HkdfSha512Secret(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const HkdfParameters&                       parameters)
    {
        return DeriveFixedSecret<Size>(context, KeyDerivationParameters {KdfAlgorithm::HkdfSha512, parameters});
    }
}// namespace NGIN::Crypto::Kdf
