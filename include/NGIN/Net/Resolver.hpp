/// @file Resolver.hpp
/// @brief Synchronous and explicitly driven asynchronous name resolution.
#pragma once

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Defines.hpp>
#include <NGIN/Net/ResolveError.hpp>
#include <NGIN/Net/ResolveOptions.hpp>
#include <NGIN/Net/ResolvedAddress.hpp>
#include <NGIN/Net/ResolverDriver.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace NGIN::Net
{
    /// @brief Resolves a host and service synchronously through the platform resolver.
    [[nodiscard]] NGIN_NET_API ResolveExpected<std::vector<ResolvedAddress>> Resolve(
            std::string_view      host,
            std::string_view      service,
            const ResolveOptions& options = {});

    /// @brief Resolves a host and service asynchronously on an explicit resolver driver.
    [[nodiscard]] NGIN_NET_API NGIN::Async::Task<std::vector<ResolvedAddress>, ResolveError> ResolveAsync(
            NGIN::Async::TaskContext&      context,
            ResolverDriver&                driver,
            std::string                    host,
            std::string                    service,
            ResolveOptions                 options      = {},
            NGIN::Async::CancellationToken cancellation = {});
}// namespace NGIN::Net
