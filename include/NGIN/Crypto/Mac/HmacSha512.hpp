#pragma once

#include <NGIN/Crypto/Mac/Hmac.hpp>

namespace NGIN::Crypto::Mac
{
    /// @brief Computes HMAC-SHA512 into caller-provided tag storage.
    [[nodiscard]] inline CryptoExpected<void> HmacSha512Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            NGIN::Crypto::Memory::SecretView            key,
            ConstByteSpan                               input,
            HmacSha512Tag&                              output) noexcept
    {
        return MacInto(context, MacAlgorithm::HmacSha512, key, input, ByteSpan {output.data(), output.size()});
    }

    /// @brief Computes HMAC-SHA512 into an owned fixed-size tag.
    [[nodiscard]] CryptoExpected<HmacSha512Tag> HmacSha512(
            const NGIN::Crypto::Backend::CryptoContext& context,
            NGIN::Crypto::Memory::SecretView            key,
            ConstByteSpan                               input);
}// namespace NGIN::Crypto::Mac
