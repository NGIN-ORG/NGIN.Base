/// @file ResolvedAddress.hpp
/// @brief One endpoint returned by name resolution.
#pragma once

#include <NGIN/Net/ResolveSocketType.hpp>
#include <NGIN/Net/Types/Endpoint.hpp>

#include <string>

namespace NGIN::Net
{
    /// @brief Associates a resolved endpoint with its transport, protocol, and optional canonical name.
    struct ResolvedAddress final
    {
        Endpoint          endpoint {};                        ///< Resolved network endpoint.
        ResolveSocketType socketType {ResolveSocketType::Any};///< Socket transport for this result.
        int               protocol {0};                       ///< Native protocol identifier.
        std::string       canonicalName {};                   ///< Canonical host name when requested and available.

        /// @brief Compares all resolved-address fields.
        [[nodiscard]] bool operator==(const ResolvedAddress&) const = default;
    };
}// namespace NGIN::Net
