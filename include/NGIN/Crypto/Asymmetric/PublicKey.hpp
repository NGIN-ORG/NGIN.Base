/// @file PublicKey.hpp
/// @brief Fixed-size, algorithm-tagged public key values.
#pragma once

#include <NGIN/Crypto/Asymmetric/detail/KeyUtilities.hpp>
#include <NGIN/Crypto/Result.hpp>

#include <utility>

namespace NGIN::Crypto::Asymmetric
{
    /// @brief Fixed-size, non-secret public key associated with an algorithm tag.
    /// @tparam AlgorithmTag Tag preventing accidental mixing of algorithms.
    /// @tparam Size Encoded key size in bytes.
    template<class AlgorithmTag, NGIN::UIntSize Size>
    class PublicKey
    {
    public:
        /// @brief Algorithm tag associated with this key.
        using Algorithm = AlgorithmTag;

        /// @brief Fixed-size byte representation.
        using ValueType = FixedBytes<Size>;

        /// @brief Encoded key size in bytes.
        static constexpr NGIN::UIntSize SizeValue = Size;

        /// @brief Constructs a zero-filled public key.
        constexpr PublicKey() noexcept = default;

        /// @brief Constructs a public key from an exact-size byte value.
        constexpr explicit PublicKey(ValueType bytes) noexcept
            : m_bytes {std::move(bytes)}
        {
        }

        /// @brief Constructs a public key from an exact-size byte value.
        [[nodiscard]] static constexpr PublicKey FromBytes(ValueType bytes) noexcept
        {
            return PublicKey {std::move(bytes)};
        }

        /// @brief Validates and copies a dynamically sized public-key encoding.
        /// @return A key, or `InvalidKey` when the span length differs from `SizeValue`.
        [[nodiscard]] static CryptoExpected<PublicKey> FromBytes(ConstByteSpan bytes) noexcept
        {
            if (bytes.size() != Size)
            {
                return std::unexpected(detail::InvalidKey());
            }

            return PublicKey {detail::CopyFixedBytes<Size>(bytes)};
        }

        /// @brief Returns a read-only span over the encoded public-key bytes.
        [[nodiscard]] constexpr ConstByteSpan Bytes() const noexcept
        {
            return ConstByteSpan {m_bytes.data(), m_bytes.size()};
        }

        /// @brief Returns the fixed-size byte value.
        [[nodiscard]] constexpr const ValueType& View() const noexcept
        {
            return m_bytes;
        }

    private:
        ValueType m_bytes {};
    };
}// namespace NGIN::Crypto::Asymmetric
