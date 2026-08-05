/// @file IpAddress.hpp
/// @brief IPv4/IPv6 address value type.
#pragma once

#include <array>
#include <compare>
#include <span>
#include <string>
#include <string_view>

#include <NGIN/Defines.hpp>
#include <NGIN/Net/Types/AddressFamily.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Utilities/Expected.hpp>

namespace NGIN::Net
{
    /// @brief Failure category returned when parsing an address or endpoint.
    enum class AddressParseErrorCode : NGIN::UInt8
    {
        Empty,
        InvalidFormat,
        InvalidCharacter,
        ComponentOutOfRange,
        MissingPort,
        PortOutOfRange,
        InvalidScope,
    };

    /// @brief Address parse failure and offending byte offset.
    struct AddressParseError final
    {
        AddressParseErrorCode code {AddressParseErrorCode::InvalidFormat};
        NGIN::UIntSize        offset {0};
    };

    template<typename T>
    using AddressExpected = NGIN::Utilities::Expected<T, AddressParseError>;

    /// @brief IPv4 or IPv6 address stored as raw bytes.
    struct NGIN_NET_API IpAddress final
    {
        static constexpr std::size_t V4Size = 4;
        static constexpr std::size_t V6Size = 16;

        /// @brief Constructs the IPv4 unspecified address.
        constexpr IpAddress() noexcept = default;

        /// @brief Constructs an address from a family and network-order bytes.
        constexpr IpAddress(AddressFamily family, const std::array<NGIN::Byte, V6Size>& bytes) noexcept
            : m_family(family), m_bytes(bytes)
        {
        }

        /// @brief Returns the address family.
        [[nodiscard]] constexpr AddressFamily GetFamily() const noexcept { return m_family; }
        /// @brief Returns whether this is an IPv4 address.
        [[nodiscard]] constexpr bool IsV4() const noexcept { return m_family == AddressFamily::V4; }
        /// @brief Returns whether this is an IPv6 address.
        [[nodiscard]] constexpr bool IsV6() const noexcept { return m_family == AddressFamily::V6; }
        /// @brief Returns whether the family is IPv4 or IPv6.
        [[nodiscard]] constexpr bool IsValid() const noexcept { return IsV4() || IsV6(); }

        /// @brief Returns the active network-order address bytes.
        [[nodiscard]] constexpr std::span<const NGIN::Byte> Bytes() const noexcept
        {
            return {m_bytes.data(), IsV4() ? V4Size : V6Size};
        }

        /// @brief Returns the IPv4 unspecified address.
        static constexpr IpAddress AnyV4() noexcept { return IpAddress(AddressFamily::V4, {}); }
        /// @brief Returns the IPv6 unspecified address.
        static constexpr IpAddress AnyV6() noexcept { return IpAddress(AddressFamily::V6, {}); }

        /// @brief Returns the IPv4 loopback address.
        static constexpr IpAddress LoopbackV4() noexcept
        {
            std::array<NGIN::Byte, V6Size> bytes {};
            bytes[0] = NGIN::Byte {127};
            bytes[3] = NGIN::Byte {1};
            return IpAddress(AddressFamily::V4, bytes);
        }

        /// @brief Returns the IPv6 loopback address.
        static constexpr IpAddress LoopbackV6() noexcept
        {
            std::array<NGIN::Byte, V6Size> bytes {};
            bytes[15] = NGIN::Byte {1};
            return IpAddress(AddressFamily::V6, bytes);
        }

        /// @brief Parses a numeric IPv4 or IPv6 address without name resolution.
        [[nodiscard]] static AddressExpected<IpAddress> Parse(std::string_view text);

        /// Writes the canonical representation without a null terminator.
        /// @brief Writes a canonical numeric address without a null terminator.
        /// @return False when the destination is too small or the address is invalid.
        [[nodiscard]] bool TryFormat(std::span<char> destination, NGIN::UIntSize& written) const noexcept;
        /// @brief Returns the canonical numeric address string.
        [[nodiscard]] std::string ToString() const;

        /// @brief Compares family and network-order bytes.
        [[nodiscard]] constexpr auto operator<=>(const IpAddress&) const noexcept = default;

    private:
        AddressFamily                  m_family {AddressFamily::V4};
        std::array<NGIN::Byte, V6Size> m_bytes {};
    };

    /// @brief Hash function compatible with `IpAddress` equality.
    struct NGIN_NET_API IpAddressHash final
    {
        /// @brief Returns a hash of the address family and active bytes.
        [[nodiscard]] std::size_t operator()(const IpAddress& address) const noexcept;
    };
}// namespace NGIN::Net
