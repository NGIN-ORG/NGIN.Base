#pragma once

#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Mac/MacAlgorithm.hpp>
#include <NGIN/Crypto/Memory/SecretView.hpp>
#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Mac
{
    /// @brief Fixed-size public message authentication tag bytes.
    template<NGIN::UIntSize Size>
    using Tag = FixedBytes<Size>;

    using HmacSha256Tag = Tag<32>;
    using HmacSha512Tag = Tag<64>;

    /// @brief Returns the tag size for a MAC algorithm in bytes.
    [[nodiscard]] constexpr NGIN::UIntSize MacTagSize(MacAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
            case MacAlgorithm::HmacSha256:
                return 32;
            case MacAlgorithm::HmacSha512:
                return 64;
        }

        return 0;
    }

    /// @brief Computes a MAC into caller-provided tag storage.
    [[nodiscard]] CryptoExpected<void> MacInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            MacAlgorithm                                algorithm,
            NGIN::Crypto::Memory::SecretView            key,
            ConstByteSpan                               input,
            ByteSpan                                    output) noexcept;

    /// @brief Computes a MAC into an owned byte buffer.
    [[nodiscard]] CryptoExpected<ByteBuffer> ComputeMac(
            const NGIN::Crypto::Backend::CryptoContext& context,
            MacAlgorithm                                algorithm,
            NGIN::Crypto::Memory::SecretView            key,
            ConstByteSpan                               input);

    /// @brief Verifies a MAC tag. Future backend implementations must compare computed tags in constant time.
    [[nodiscard]] CryptoExpected<void> VerifyMac(
            const NGIN::Crypto::Backend::CryptoContext& context,
            MacAlgorithm                                algorithm,
            NGIN::Crypto::Memory::SecretView            key,
            ConstByteSpan                               input,
            ConstByteSpan                               expectedTag) noexcept;
}// namespace NGIN::Crypto::Mac
