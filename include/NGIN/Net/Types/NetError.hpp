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
        InvalidArgument,
        NameNotFound,
        ServiceNotFound,
        AddressFamilyNotSupported,
        Unknown,
    };

    /// @brief Structured error with optional native OS code.
    struct NetError final
    {
        NetErrorCode code {NetErrorCode::Ok};
        int          native {0};

        /// @brief Constructs a successful result code.
        constexpr NetError() noexcept = default;

        /// @brief Constructs a portable network error with an optional native code.
        constexpr explicit NetError(NetErrorCode errorCode, int nativeCode = 0) noexcept
            : code(errorCode), native(nativeCode)
        {
        }

        /// @brief Returns whether this payload represents success.
        [[nodiscard]] constexpr bool IsOk() const noexcept { return code == NetErrorCode::Ok; }

        /// @brief Converts this payload to the common error-domain representation.
        [[nodiscard]] constexpr NGIN::Utilities::ErrorInfo ToErrorInfo() const noexcept
        {
            return {NGIN::Utilities::ErrorDomain::Net, code, native};
        }
    };

    /// @brief Converts a network error to the closest standard or native error code.
    [[nodiscard]] inline std::error_code ToErrorCode(NetError error) noexcept
    {
        if (error.native != 0)
        {
            return std::error_code(error.native, std::system_category());
        }

        switch (error.code)
        {
            case NetErrorCode::WouldBlock:
                return std::make_error_code(std::errc::resource_unavailable_try_again);
            case NetErrorCode::TimedOut:
                return std::make_error_code(std::errc::timed_out);
            case NetErrorCode::Disconnected:
                return std::make_error_code(std::errc::connection_aborted);
            case NetErrorCode::ConnectionReset:
                return std::make_error_code(std::errc::connection_reset);
            case NetErrorCode::HostUnreachable:
                return std::make_error_code(std::errc::host_unreachable);
            case NetErrorCode::MessageTooLarge:
                return std::make_error_code(std::errc::message_size);
            case NetErrorCode::PermissionDenied:
                return std::make_error_code(std::errc::permission_denied);
            case NetErrorCode::InvalidArgument:
                return std::make_error_code(std::errc::invalid_argument);
            case NetErrorCode::NameNotFound:
                return std::make_error_code(std::errc::host_unreachable);
            case NetErrorCode::ServiceNotFound:
                return std::make_error_code(std::errc::invalid_argument);
            case NetErrorCode::AddressFamilyNotSupported:
                return std::make_error_code(std::errc::address_family_not_supported);
            case NetErrorCode::Unknown:
                return std::make_error_code(std::errc::io_error);
            case NetErrorCode::Ok:
                return {};
        }

        return {};
    }

    template<typename T>
    using NetExpected = NGIN::Utilities::Expected<T, NetError>;
}// namespace NGIN::Net
