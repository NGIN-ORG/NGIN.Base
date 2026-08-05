/// @file Endpoint.hpp
/// @brief Network endpoint (address + port).
#pragma once

#include <NGIN/Net/Types/IpAddress.hpp>
#include <NGIN/Primitives.hpp>

#include <compare>
#include <span>
#include <string>
#include <string_view>

namespace NGIN::Net
{
    /// @brief Address and port pair.
    struct Endpoint final
    {
        IpAddress    address {};
        NGIN::UInt16 port {0};
        NGIN::UInt32 scopeId {0};

        [[nodiscard]] static AddressExpected<Endpoint> Parse(std::string_view text);

        /// Writes the canonical representation without a null terminator.
        [[nodiscard]] bool        TryFormat(std::span<char> destination, NGIN::UIntSize& written) const noexcept;
        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] constexpr auto operator<=>(const Endpoint&) const noexcept = default;
    };

    struct EndpointHash final
    {
        [[nodiscard]] std::size_t operator()(const Endpoint& endpoint) const noexcept;
    };
}// namespace NGIN::Net
