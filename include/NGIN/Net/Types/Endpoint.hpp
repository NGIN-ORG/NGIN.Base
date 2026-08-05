/// @file Endpoint.hpp
/// @brief Network endpoint (address + port).
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Net/Types/IpAddress.hpp>
#include <NGIN/Primitives.hpp>

#include <compare>
#include <span>
#include <string>
#include <string_view>

namespace NGIN::Net
{
    /// @brief Address and port pair.
    struct NGIN_NET_API Endpoint final
    {
        IpAddress    address {};
        NGIN::UInt16 port {0};
        NGIN::UInt32 scopeId {0};

        /// @brief Parses a numeric address and port, including bracketed IPv6 endpoints.
        [[nodiscard]] static AddressExpected<Endpoint> Parse(std::string_view text);

        /// Writes the canonical representation without a null terminator.
        /// @brief Writes the canonical address and port without a null terminator.
        /// @return False when the destination is too small or the endpoint is invalid.
        [[nodiscard]] bool TryFormat(std::span<char> destination, NGIN::UIntSize& written) const noexcept;
        /// @brief Returns the canonical address-and-port string.
        [[nodiscard]] std::string ToString() const;

        /// @brief Compares address, port, and IPv6 scope identifier.
        [[nodiscard]] constexpr auto operator<=>(const Endpoint&) const noexcept = default;
    };

    /// @brief Hash function compatible with `Endpoint` equality.
    struct NGIN_NET_API EndpointHash final
    {
        /// @brief Returns a hash of address, port, and scope identifier.
        [[nodiscard]] std::size_t operator()(const Endpoint& endpoint) const noexcept;
    };
}// namespace NGIN::Net
