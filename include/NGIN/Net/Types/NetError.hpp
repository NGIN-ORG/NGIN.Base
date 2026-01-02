/// @file NetError.hpp
/// @brief Error codes for network operations.
#pragma once

#include <expected>

#include <NGIN/Primitives.hpp>

namespace NGIN::Net
{
    /// @brief Network error codes for fast-path operations.
    enum class NetErrc : NGIN::UInt8
    {
        Ok,
        WouldBlock,
        TimedOut,
        Disconnected,
        ConnectionReset,
        HostUnreachable,
        MessageTooLarge,
        PermissionDenied,
        Unknown,
    };

    /// @brief Structured error with optional native OS code.
    struct NetError final
    {
        NetErrc code {NetErrc::Ok};
        int     native {0};

        [[nodiscard]] constexpr bool IsOk() const noexcept { return code == NetErrc::Ok; }
    };

    template<typename T>
    using NetExpected = std::expected<T, NetError>;
}// namespace NGIN::Net
