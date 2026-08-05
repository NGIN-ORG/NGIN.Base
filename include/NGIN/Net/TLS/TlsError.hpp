/// @file TlsError.hpp
/// @brief Provider-neutral TLS diagnostics.
#pragma once

#include <string>

#include <NGIN/Net/Types/NetError.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Utilities/Expected.hpp>

namespace NGIN::Net::TLS
{
    /// @brief Broad subsystem category of a TLS failure.
    enum class TlsErrorCategory : NGIN::UInt8
    {
        Transport,
        Protocol,
        Certificate,
        Hostname,
        Timeout,
        Cancellation,
        Provider,
        State,
    };

    /// @brief Provider-neutral TLS failure code.
    enum class TlsErrorCode : NGIN::UInt8
    {
        Unknown,
        ProviderUnavailable,
        InvalidConfiguration,
        InvalidState,
        ConcurrentOperation,
        HandshakeFailed,
        ProtocolViolation,
        CertificateRejected,
        HostnameMismatch,
        AlpnMismatch,
        TimedOut,
        Canceled,
        TransportFailure,
        TruncatedStream,
        Closed,
    };

    /// @brief Structured TLS failure with provider and transport context.
    struct TlsError final
    {
        TlsErrorCategory    category {TlsErrorCategory::State};
        TlsErrorCode        code {TlsErrorCode::Unknown};
        int                 native {0};
        std::string         message;
        NGIN::Net::NetError transportError {};

        /// @brief Maps this TLS failure to the closest transport-layer error.
        [[nodiscard]] NGIN::Net::NetError ToNetError() const noexcept
        {
            switch (category)
            {
                case TlsErrorCategory::Transport:
                    return transportError.IsOk()
                                   ? NGIN::Net::NetError {NGIN::Net::NetErrorCode::Unknown, native}
                                   : transportError;
                case TlsErrorCategory::Timeout:
                    return NGIN::Net::NetError {NGIN::Net::NetErrorCode::TimedOut, native};
                case TlsErrorCategory::Certificate:
                case TlsErrorCategory::Hostname:
                    return NGIN::Net::NetError {NGIN::Net::NetErrorCode::PermissionDenied, native};
                case TlsErrorCategory::Protocol:
                    return NGIN::Net::NetError {NGIN::Net::NetErrorCode::ConnectionReset, native};
                case TlsErrorCategory::Cancellation:
                case TlsErrorCategory::Provider:
                case TlsErrorCategory::State:
                    return NGIN::Net::NetError {NGIN::Net::NetErrorCode::Unknown, native};
            }
            return NGIN::Net::NetError {NGIN::Net::NetErrorCode::Unknown, native};
        }
    };

    template<typename T>
    using TlsExpected = NGIN::Utilities::Expected<T, TlsError>;
}// namespace NGIN::Net::TLS
