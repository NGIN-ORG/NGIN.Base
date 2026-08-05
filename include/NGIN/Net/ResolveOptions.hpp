/// @file ResolveOptions.hpp
/// @brief Address-family, transport, parsing, and timeout policy for name resolution.
#pragma once

#include <NGIN/Net/ResolveSocketType.hpp>
#include <NGIN/Net/Types/AddressFamily.hpp>

#include <chrono>
#include <optional>

namespace NGIN::Net
{
    /// @brief Controls synchronous and asynchronous endpoint resolution.
    struct ResolveOptions final
    {
        AddressFamily                            family {AddressFamily::DualStack};  ///< Address family accepted in results.
        ResolveSocketType                        socketType {ResolveSocketType::Any};///< Socket transport accepted in results.
        bool                                     passive {false};                    ///< Produce bindable wildcard addresses when the host is empty.
        bool                                     numericHost {false};                ///< Require the host to be a numeric address.
        bool                                     numericService {false};             ///< Require the service to be a numeric port.
        bool                                     requestCanonicalName {false};       ///< Request the canonical host name from the platform resolver.
        std::optional<std::chrono::milliseconds> timeout {};                         ///< Optional asynchronous resolution timeout.
    };
}// namespace NGIN::Net
