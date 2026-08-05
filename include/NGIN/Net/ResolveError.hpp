/// @file ResolveError.hpp
/// @brief Structured error returned by endpoint resolution.
#pragma once

#include <NGIN/Net/Types/NetError.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <string>

namespace NGIN::Net
{
    /// @brief Describes a network or platform-resolver failure.
    struct ResolveError final
    {
        NetError    network {};      ///< Portable and native network error information.
        int         resolverCode {0};///< Native name-resolver status code.
        std::string diagnostic {};   ///< Human-readable resolver diagnostic.
    };

    /// @brief Expected-like result returned by resolver operations.
    /// @tparam T Successful resolution result type.
    template<typename T>
    using ResolveExpected = NGIN::Utilities::Expected<T, ResolveError>;
}// namespace NGIN::Net
