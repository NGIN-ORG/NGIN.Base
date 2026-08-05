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

    class NGIN_NETTLS_API TlsStream final : public NGIN::Net::Transport::IByteStream
    {
    public:
        /// @brief TLS streams are non-copyable because they uniquely own session and transport state.
        TlsStream(const TlsStream&) = delete;
        /// @brief TLS streams are non-copy-assignable because they uniquely own session and transport state.
        TlsStream& operator=(const TlsStream&) = delete;
        /// @brief TLS streams are immovable because outstanding operations retain their address.
        TlsStream(TlsStream&&) = delete;
        /// @brief TLS streams are non-move-assignable because outstanding operations retain their address.
        TlsStream& operator=(TlsStream&&) = delete;
        /// @brief Closes provider and transport state.
        ~TlsStream() override;

        /// @brief Creates a client TLS filter over an owned byte stream.
        /// @note The context may be released after creation because provider state is shared internally.
        [[nodiscard]] static TlsExpected<std::unique_ptr<TlsStream>> CreateClient(
                std::unique_ptr<NGIN::Net::Transport::IByteStream> inner,
                const TlsContext&                                  context,
                TlsClientOptions                                   options);

        /// @brief Creates a server TLS filter over an owned byte stream.
        [[nodiscard]] static TlsExpected<std::unique_ptr<TlsStream>> CreateServer(
                std::unique_ptr<NGIN::Net::Transport::IByteStream> inner,
                const TlsContext&                                  context,
                TlsServerOptions                                   options = {});

        /// @brief Performs the TLS handshake with cancellation and optional timeout policy.
        NGIN::Async::Task<void, TlsError> HandshakeAsync(
                NGIN::Async::TaskContext&      ctx,
                NGIN::Async::CancellationToken token   = {},
                TlsHandshakeOptions            options = {});

        /// @brief Asynchronously reads decrypted application data.
        /// @pre The handshake completed and no concurrent read is active.
        NGIN::Async::Task<NGIN::UInt32, TlsError> ReadTlsAsync(
                NGIN::Async::TaskContext&      ctx,
                NGIN::Net::ByteSpan            destination,
                NGIN::Async::CancellationToken token = {});

        /// @brief Asynchronously writes application data through TLS encryption.
        /// @pre The handshake completed and no concurrent write is active.
        NGIN::Async::Task<NGIN::UInt32, TlsError> WriteTlsAsync(
                NGIN::Async::TaskContext&      ctx,
                NGIN::Net::ConstByteSpan       source,
                NGIN::Async::CancellationToken token = {});

        /// @brief Sends the TLS close notification and closes the filtered stream.
        NGIN::Async::Task<void, TlsError> ShutdownAsync(
                NGIN::Async::TaskContext&      ctx,
                NGIN::Async::CancellationToken token = {});

        /// @copydoc NGIN::Net::Transport::IByteStream::ReadAsync
        NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> ReadAsync(
                NGIN::Async::TaskContext&      ctx,
                NGIN::Net::ByteSpan            destination,
                NGIN::Async::CancellationToken token) override;

        /// @copydoc NGIN::Net::Transport::IByteStream::WriteAsync
        NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> WriteAsync(
                NGIN::Async::TaskContext&      ctx,
                NGIN::Net::ConstByteSpan       source,
                NGIN::Async::CancellationToken token) override;

        /// @copydoc NGIN::Net::Transport::IByteStream::Close
        NGIN::Net::NetExpected<void> Close() override;

        /// @brief Returns the current TLS session state.
        [[nodiscard]] TlsStreamState State() const noexcept;
        /// @brief Returns the negotiated ALPN protocol, or an empty view when none was selected.
        /// @note The view remains valid until this stream is destroyed.
        [[nodiscard]] std::string_view NegotiatedProtocol() const noexcept;
        /// @brief Returns the configured or observed server name.
        [[nodiscard]] std::string_view ServerName() const noexcept;
        /// @brief Returns the peer leaf certificate when the provider supplied one.
        [[nodiscard]] const std::optional<NGIN::Crypto::Certificates::Certificate>& PeerCertificate() const noexcept;
        /// @brief Returns the owned underlying transport without transferring ownership.
        [[nodiscard]] NGIN::Net::Transport::IByteStream* Inner() noexcept;

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
