#pragma once

#include <NGIN/Crypto/Memory/SecureBuffer.hpp>
#include <NGIN/Crypto/Memory/ZeroMemory.hpp>
#include <NGIN/Crypto/Random/SecureRandom.hpp>
#include <NGIN/Crypto/Result.hpp>
#include <NGIN/Crypto/Types.hpp>
#include <NGIN/Primitives.hpp>

#include <array>
#include <type_traits>
#include <utility>

namespace NGIN::Crypto::Memory
{
    namespace detail
    {
        template<class T>
        struct FixedBytesTraits
        {
            static constexpr bool IsFixedBytes = false;
        };

        template<NGIN::UIntSize Size>
        struct FixedBytesTraits<FixedBytes<Size>>
        {
            static constexpr bool IsFixedBytes = true;
            static constexpr auto SizeValue    = Size;
        };
    }// namespace detail

    /// @brief Dynamic owned secret byte storage.
    using DynamicSecret = SecureBuffer;

    /// @brief Move-only wrapper for typed secret values.
    ///
    /// `Secret<T>` is intended for trivially copyable fixed-size secret material such as keys and derived key bytes.
    /// It prevents accidental copying of the wrapper and wipes the stored object representation on destruction and move.
    template<class T>
    class Secret
    {
        static_assert(std::is_trivially_copyable_v<T>, "Secret<T> requires trivially copyable secret payloads.");
        static_assert(std::is_trivially_destructible_v<T>, "Secret<T> requires trivially destructible secret payloads.");

    public:
        using ValueType = T;

        Secret() noexcept(std::is_nothrow_default_constructible_v<T>)
            requires std::is_default_constructible_v<T>
            : m_value {}
        {
        }

        explicit Secret(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
            : m_value {std::move(value)}
        {
        }

        Secret(const Secret&)            = delete;
        Secret& operator=(const Secret&) = delete;

        Secret(Secret&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
            : m_value {std::move(other.m_value)}
        {
            other.Wipe();
        }

        Secret& operator=(Secret&& other) noexcept(std::is_nothrow_move_assignable_v<T>)
        {
            if (this != &other)
            {
                Wipe();
                m_value = std::move(other.m_value);
                other.Wipe();
            }

            return *this;
        }

        ~Secret()
        {
            Wipe();
        }

        /// @brief Constructs a typed secret from an existing value.
        [[nodiscard]] static Secret FromValue(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            return Secret {std::move(value)};
        }

        /// @brief Generates a fixed-size secret using the platform secure random source.
        [[nodiscard]] static CryptoExpected<Secret> Generate() noexcept
            requires(detail::FixedBytesTraits<T>::IsFixedBytes)
        {
            T    value {};
            auto result = NGIN::Crypto::Random::Fill(ByteSpan {value.data(), value.size()});
            if (!result.HasValue())
            {
                return result.Error();
            }

            return Secret {std::move(value)};
        }

        /// @brief Returns a read-only view of the typed secret value.
        [[nodiscard]] const T& View() const noexcept
        {
            return m_value;
        }

        /// @brief Returns a mutable typed view. Use only where mutation is required by a crypto operation.
        [[nodiscard]] T& UnsafeMutableView() noexcept
        {
            return m_value;
        }

        /// @brief Returns a read-only byte view over the stored object representation.
        [[nodiscard]] ConstByteSpan Bytes() const noexcept
        {
            return ConstByteSpan {reinterpret_cast<const NGIN::Byte*>(&m_value), sizeof(T)};
        }

        /// @brief Returns a mutable byte view. Use only for direct cryptographic fills or transforms.
        [[nodiscard]] ByteSpan UnsafeMutableBytes() noexcept
        {
            return ByteSpan {reinterpret_cast<NGIN::Byte*>(&m_value), sizeof(T)};
        }

    private:
        void Wipe() noexcept
        {
            SecureZero(&m_value, sizeof(T));
        }

        T m_value {};
    };

    /// @brief Fixed-size secret byte storage for keys, seeds, and derived secret material.
    template<NGIN::UIntSize Size>
    using FixedSecret = Secret<FixedBytes<Size>>;
}// namespace NGIN::Crypto::Memory
