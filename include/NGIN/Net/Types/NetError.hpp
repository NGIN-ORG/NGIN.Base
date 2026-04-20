/// @file NetError.hpp
/// @brief Error codes for network operations.
#pragma once

#include <system_error>

#include <NGIN/Primitives.hpp>
#include <NGIN/Utilities/Error.hpp>
#include <NGIN/Utilities/Expected.hpp>

namespace NGIN::Net
{
    /// @brief Network error codes for fast-path operations.
    enum class NetErrorCode : NGIN::UInt8
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
        NetErrorCode code {NetErrorCode::Ok};
        int          native {0};

        constexpr NetError() noexcept = default;

        constexpr explicit NetError(NetErrorCode errorCode, int nativeCode = 0) noexcept
            : code(errorCode)
            , native(nativeCode)
        {
        }

        [[nodiscard]] constexpr bool IsOk() const noexcept { return code == NetErrorCode::Ok; }

        [[nodiscard]] constexpr NGIN::Utilities::ErrorInfo ToErrorInfo() const noexcept
        {
            return {NGIN::Utilities::ErrorDomain::Net, code, native};
        }
    };

    [[nodiscard]] inline std::error_code ToErrorCode(NetError error) noexcept
    {
        if (error.native != 0)
        {
            return std::error_code(error.native, std::system_category());
        }

        switch (error.code)
        {
            case NetErrorCode::WouldBlock: return std::make_error_code(std::errc::resource_unavailable_try_again);
            case NetErrorCode::TimedOut: return std::make_error_code(std::errc::timed_out);
            case NetErrorCode::Disconnected: return std::make_error_code(std::errc::connection_aborted);
            case NetErrorCode::ConnectionReset: return std::make_error_code(std::errc::connection_reset);
            case NetErrorCode::HostUnreachable: return std::make_error_code(std::errc::host_unreachable);
            case NetErrorCode::MessageTooLarge: return std::make_error_code(std::errc::message_size);
            case NetErrorCode::PermissionDenied: return std::make_error_code(std::errc::permission_denied);
            case NetErrorCode::Unknown: return std::make_error_code(std::errc::io_error);
            case NetErrorCode::Ok: return {};
        }

        return {};
    }

    template<typename T>
    using NetExpected = NGIN::Utilities::Expected<T, NetError>;
}// namespace NGIN::Net
