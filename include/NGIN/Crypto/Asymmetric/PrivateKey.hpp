/// @file PrivateKey.hpp
/// @brief Fixed-size, algorithm-tagged private keys backed by secure storage.
#pragma once

#include <NGIN/Crypto/Asymmetric/detail/KeyUtilities.hpp>
#include <NGIN/Crypto/Memory/Secret.hpp>
#include <NGIN/Crypto/Result.hpp>

#include <utility>

namespace NGIN::Crypto::Asymmetric
{
    /// @brief Fixed-size secret private key associated with an algorithm tag.
    /// @tparam AlgorithmTag Tag preventing accidental mixing of algorithms.
    /// @tparam Size Encoded key size in bytes.
    template<class AlgorithmTag, NGIN::UIntSize Size>
    class PrivateKey
    {
    public:
        /// @brief Algorithm tag associated with this key.
        using Algorithm = AlgorithmTag;
        /// @brief Fixed-size byte representation accepted during construction.
        using ValueType = FixedBytes<Size>;
        /// @brief Secure storage type used for the private bytes.
        using SecretType = NGIN::Crypto::Memory::FixedSecret<Size>;

        /// @brief Encoded key size in bytes.
        static constexpr NGIN::UIntSize SizeValue = Size;

        /// @brief Constructs a zero-filled private key in secure storage.
        PrivateKey() noexcept = default;

        /// @brief Takes ownership of an exact-size secret value.
        explicit PrivateKey(SecretType secret) noexcept
            : m_secret {std::move(secret)}
        {
        }

        /// @brief Private keys are non-copyable to avoid duplicating secret material implicitly.
        PrivateKey(const PrivateKey&) = delete;
        /// @brief Private keys are non-copy-assignable to avoid duplicating secret material implicitly.
        PrivateKey& operator=(const PrivateKey&) = delete;
        /// @brief Transfers ownership of secret material.
        PrivateKey(PrivateKey&&) noexcept = default;
        /// @brief Replaces this key by transferring secret material.
        PrivateKey& operator=(PrivateKey&&) noexcept = default;

        /// @brief Moves an exact-size byte value into secure private-key storage.
        [[nodiscard]] static PrivateKey FromBytes(ValueType bytes) noexcept
        {
            return PrivateKey {SecretType::FromValue(std::move(bytes))};
        }

        /// @brief Validates and copies dynamically sized bytes into secure private-key storage.
        /// @return A key, or `InvalidKey` when the span length differs from `SizeValue`.
        [[nodiscard]] static CryptoExpected<PrivateKey> FromSecretBytes(ConstByteSpan bytes) noexcept
        {
            if (bytes.size() != Size)
            {
                return std::unexpected(detail::InvalidKey());
            }

            return PrivateKey {SecretType::FromValue(detail::CopyFixedBytes<Size>(bytes))};
        }

        /// @brief Returns a read-only span over the private-key bytes.
        [[nodiscard]] ConstByteSpan Bytes() const noexcept
        {
            return m_secret.Bytes();
        }

        /// @brief Returns mutable access to private-key storage for backend output.
        /// @warning Callers must not retain or expose the returned span.
        [[nodiscard]] ByteSpan UnsafeMutableBytes() noexcept
        {
            return m_secret.UnsafeMutableBytes();
        }

        /// @brief Returns the secure storage object containing the private key.
        [[nodiscard]] const SecretType& Secret() const noexcept
        {
            return m_secret;
        }

    private:
        SecretType m_secret {};
    };
}// namespace NGIN::Crypto::Asymmetric
