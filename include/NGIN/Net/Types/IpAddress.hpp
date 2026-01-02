/// @file IpAddress.hpp
/// @brief IPv4/IPv6 address value type.
#pragma once

#include <array>
#include <span>

#include <NGIN/Net/Types/AddressFamily.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Net
{
    /// @brief IPv4 or IPv6 address stored as raw bytes.
    struct IpAddress final
    {
        static constexpr std::size_t V4Size = 4;
        static constexpr std::size_t V6Size = 16;

        constexpr IpAddress() noexcept = default;

        constexpr IpAddress(AddressFamily family, const std::array<NGIN::Byte, V6Size>& bytes) noexcept
            : m_family(family)
            , m_bytes(bytes)
        {
        }

        [[nodiscard]] constexpr AddressFamily GetFamily() const noexcept { return m_family; }
        [[nodiscard]] constexpr bool IsV4() const noexcept { return m_family == AddressFamily::V4; }
        [[nodiscard]] constexpr bool IsV6() const noexcept { return m_family == AddressFamily::V6; }

        [[nodiscard]] constexpr std::span<const NGIN::Byte> Bytes() const noexcept
        {
            return {m_bytes.data(), IsV4() ? V4Size : V6Size};
        }

        static constexpr IpAddress AnyV4() noexcept { return IpAddress(AddressFamily::V4, {}); }
        static constexpr IpAddress AnyV6() noexcept { return IpAddress(AddressFamily::V6, {}); }

        static constexpr IpAddress LoopbackV4() noexcept
        {
            std::array<NGIN::Byte, V6Size> bytes {};
            bytes[0] = NGIN::Byte {127};
            bytes[3] = NGIN::Byte {1};
            return IpAddress(AddressFamily::V4, bytes);
        }

        static constexpr IpAddress LoopbackV6() noexcept
        {
            std::array<NGIN::Byte, V6Size> bytes {};
            bytes[15] = NGIN::Byte {1};
            return IpAddress(AddressFamily::V6, bytes);
        }

    private:
        AddressFamily                    m_family {AddressFamily::V4};
        std::array<NGIN::Byte, V6Size>   m_bytes {};
    };
}// namespace NGIN::Net
