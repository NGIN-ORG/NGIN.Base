/// @file TlsStream.hpp
/// @brief TLS filter over an asynchronous byte stream.
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <NGIN/Async/Task.hpp>
#include <NGIN/Defines.hpp>
#include <NGIN/Net/TLS/TlsContext.hpp>
#include <NGIN/Net/Transport/IByteStream.hpp>

namespace NGIN::Net::TLS
{
    namespace detail
    {
        class TlsSession;
    }

    class NGIN_NET_API TlsStream final : public NGIN::Net::Transport::IByteStream
    {
    public:
        TlsStream(const TlsStream&)            = delete;
        TlsStream& operator=(const TlsStream&) = delete;
        TlsStream(TlsStream&&)                 = delete;
        TlsStream& operator=(TlsStream&&)      = delete;
        ~TlsStream() override;

        [[nodiscard]] static TlsExpected<std::unique_ptr<TlsStream>> CreateClient(
                std::unique_ptr<NGIN::Net::Transport::IByteStream> inner,
                const TlsContext&                                  context,
                TlsClientOptions                                   options);

        [[nodiscard]] static TlsExpected<std::unique_ptr<TlsStream>> CreateServer(
                std::unique_ptr<NGIN::Net::Transport::IByteStream> inner,
                const TlsContext&                                  context,
                TlsServerOptions                                   options = {});

        NGIN::Async::Task<void, TlsError> HandshakeAsync(
                NGIN::Async::TaskContext&      ctx,
                NGIN::Async::CancellationToken token   = {},
                TlsHandshakeOptions            options = {});

        NGIN::Async::Task<NGIN::UInt32, TlsError> ReadTlsAsync(
                NGIN::Async::TaskContext&      ctx,
                NGIN::Net::ByteSpan            destination,
                NGIN::Async::CancellationToken token = {});

        NGIN::Async::Task<NGIN::UInt32, TlsError> WriteTlsAsync(
                NGIN::Async::TaskContext&      ctx,
                NGIN::Net::ConstByteSpan       source,
                NGIN::Async::CancellationToken token = {});

        NGIN::Async::Task<void, TlsError> ShutdownAsync(
                NGIN::Async::TaskContext&      ctx,
                NGIN::Async::CancellationToken token = {});

        NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> ReadAsync(
                NGIN::Async::TaskContext&      ctx,
                NGIN::Net::ByteSpan            destination,
                NGIN::Async::CancellationToken token) override;

        NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> WriteAsync(
                NGIN::Async::TaskContext&      ctx,
                NGIN::Net::ConstByteSpan       source,
                NGIN::Async::CancellationToken token) override;

        NGIN::Net::NetExpected<void> Close() override;

        [[nodiscard]] TlsStreamState                                                State() const noexcept;
        [[nodiscard]] std::string_view                                              NegotiatedProtocol() const noexcept;
        [[nodiscard]] std::string_view                                              ServerName() const noexcept;
        [[nodiscard]] const std::optional<NGIN::Crypto::Certificates::Certificate>& PeerCertificate() const noexcept;
        [[nodiscard]] NGIN::Net::Transport::IByteStream*                            Inner() noexcept;

    private:
        TlsStream(
                std::unique_ptr<NGIN::Net::Transport::IByteStream> inner,
                std::unique_ptr<detail::TlsSession>                session,
                bool                                               allowTruncatedEof) noexcept;

        NGIN::Async::Task<void, TlsError> FlushEncrypted(
                NGIN::Async::TaskContext&      ctx,
                NGIN::Async::CancellationToken token);
        NGIN::Async::Task<void, TlsError> ReceiveEncrypted(
                NGIN::Async::TaskContext&      ctx,
                NGIN::Async::CancellationToken token);
        void CaptureSessionMetadata();

        std::unique_ptr<NGIN::Net::Transport::IByteStream>     m_inner;
        std::unique_ptr<detail::TlsSession>                    m_session;
        std::atomic<TlsStreamState>                            m_state {TlsStreamState::Created};
        std::atomic_flag                                       m_readActive           = ATOMIC_FLAG_INIT;
        std::atomic_flag                                       m_writeActive          = ATOMIC_FLAG_INIT;
        std::atomic_flag                                       m_controlActive        = ATOMIC_FLAG_INIT;
        std::atomic_flag                                       m_transportWriteActive = ATOMIC_FLAG_INIT;
        std::mutex                                             m_providerMutex;
        std::shared_ptr<std::atomic<bool>>                     m_activeTimeoutFired;
        bool                                                   m_allowTruncatedEof {false};
        std::string                                            m_negotiatedProtocol;
        std::string                                            m_serverName;
        std::optional<NGIN::Crypto::Certificates::Certificate> m_peerCertificate;
    };
}// namespace NGIN::Net::TLS
