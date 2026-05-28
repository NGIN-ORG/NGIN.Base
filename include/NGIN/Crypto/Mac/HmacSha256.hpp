#pragma once

#include <NGIN/Crypto/Mac/Hmac.hpp>

namespace NGIN::Crypto::Mac
{
    /// @brief Computes HMAC-SHA256 into caller-provided tag storage.
    [[nodiscard]] inline CryptoExpected<void> HmacSha256Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            NGIN::Crypto::Memory::SecretView            key,
            ConstByteSpan                               input,
            HmacSha256Tag&                              output) noexcept
    {
        return MacInto(context, MacAlgorithm::HmacSha256, key, input, ByteSpan {output.data(), output.size()});
    }

    /// @brief Computes HMAC-SHA256 into an owned fixed-size tag.
    [[nodiscard]] CryptoExpected<HmacSha256Tag> HmacSha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            NGIN::Crypto::Memory::SecretView            key,
            ConstByteSpan                               input);
}// namespace NGIN::Crypto::Mac
