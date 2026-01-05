/// @file NetError.hpp
/// @brief Error codes for network operations.
#pragma once

#include <expected>
#include <system_error>

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

    [[nodiscard]] inline std::error_code ToErrorCode(NetError error) noexcept
    {
        if (error.native != 0)
        {
            return std::error_code(error.native, std::system_category());
        }

        switch (error.code)
        {
            case NetErrc::WouldBlock: return std::make_error_code(std::errc::resource_unavailable_try_again);
            case NetErrc::TimedOut: return std::make_error_code(std::errc::timed_out);
            case NetErrc::Disconnected: return std::make_error_code(std::errc::connection_aborted);
            case NetErrc::ConnectionReset: return std::make_error_code(std::errc::connection_reset);
            case NetErrc::HostUnreachable: return std::make_error_code(std::errc::host_unreachable);
            case NetErrc::MessageTooLarge: return std::make_error_code(std::errc::message_size);
            case NetErrc::PermissionDenied: return std::make_error_code(std::errc::permission_denied);
            case NetErrc::Unknown: return std::make_error_code(std::errc::io_error);
            case NetErrc::Ok: break;
        }

        return std::make_error_code(std::errc::io_error);
    }

    template<typename T>
    using NetExpected = std::expected<T, NetError>;
}// namespace NGIN::Net
