#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <NGIN/Net/TLS/TlsError.hpp>
#include <NGIN/Net/TLS/TlsTypes.hpp>
#include <NGIN/Net/Types/Buffer.hpp>

namespace NGIN::Net::TLS::detail
{
    enum class TlsProviderStatus : NGIN::UInt8
    {
        Complete,
        WantRead,
        WantWrite,
        Closed,
    };

    struct TlsProviderResult final
    {
        TlsProviderStatus status {TlsProviderStatus::Complete};
        NGIN::UInt32      bytes {0};
    };

    class TlsSession
    {
    public:
        virtual ~TlsSession() = default;

        [[nodiscard]] virtual TlsExpected<TlsProviderResult>       Handshake()                                       = 0;
        [[nodiscard]] virtual TlsExpected<TlsProviderResult>       Read(NGIN::Net::ByteSpan destination)             = 0;
        [[nodiscard]] virtual TlsExpected<TlsProviderResult>       Write(NGIN::Net::ConstByteSpan source)            = 0;
        [[nodiscard]] virtual TlsExpected<TlsProviderResult>       Shutdown()                                        = 0;
        [[nodiscard]] virtual TlsExpected<std::vector<NGIN::Byte>> DrainEncrypted()                                  = 0;
        [[nodiscard]] virtual TlsExpected<void>                    FeedEncrypted(NGIN::Net::ConstByteSpan encrypted) = 0;
        virtual void                                               NotifyTransportEof() noexcept                     = 0;

        [[nodiscard]] virtual std::string             NegotiatedProtocol() const = 0;
        [[nodiscard]] virtual std::string             ServerName() const         = 0;
        [[nodiscard]] virtual std::vector<NGIN::Byte> PeerCertificateDer() const = 0;
    };

    class TlsContextState
    {
    public:
        virtual ~TlsContextState() = default;

        [[nodiscard]] virtual bool                                     IsClient() const noexcept     = 0;
        [[nodiscard]] virtual std::string_view                         ProviderName() const noexcept = 0;
        [[nodiscard]] virtual TlsExpected<std::unique_ptr<TlsSession>> CreateClientSession(
                const TlsClientOptions& options) const = 0;
        [[nodiscard]] virtual TlsExpected<std::unique_ptr<TlsSession>> CreateServerSession(
                const TlsServerOptions& options) const = 0;
    };

    [[nodiscard]] TlsExpected<std::shared_ptr<const TlsContextState>> CreateOpenSslClientContext(
            TlsClientContextOptions options);
    [[nodiscard]] TlsExpected<std::shared_ptr<const TlsContextState>> CreateOpenSslServerContext(
            TlsServerContextOptions options);

    [[nodiscard]] inline TlsError MakeTlsError(
            TlsErrorCategory category,
            TlsErrorCode     code,
            std::string      message,
            int              native = 0)
    {
        return TlsError {
                .category = category,
                .code     = code,
                .native   = native,
                .message  = std::move(message),
        };
    }

    [[nodiscard]] inline TlsError MakeTransportError(NGIN::Net::NetError error, std::string message)
    {
        return TlsError {
                .category       = TlsErrorCategory::Transport,
                .code           = TlsErrorCode::TransportFailure,
                .native         = error.native,
                .message        = std::move(message),
                .transportError = error,
        };
    }
}// namespace NGIN::Net::TLS::detail
