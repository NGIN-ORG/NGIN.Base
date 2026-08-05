#include <NGIN/Net/TLS/TlsStream.hpp>

#include "TlsProvider.hpp"

#include <array>
#include <utility>

#include <NGIN/Async/Completion.hpp>
#include <NGIN/Crypto/Certificates/Certificate.hpp>
#include <NGIN/Units.hpp>

namespace NGIN::Net::TLS
{
    namespace
    {
        class AtomicFlagGuard final
        {
        public:
            explicit AtomicFlagGuard(std::atomic_flag& flag) noexcept
                : m_flag(&flag), m_acquired(!flag.test_and_set(std::memory_order_acq_rel))
            {
            }

            AtomicFlagGuard(const AtomicFlagGuard&)            = delete;
            AtomicFlagGuard& operator=(const AtomicFlagGuard&) = delete;

            ~AtomicFlagGuard()
            {
                if (m_acquired)
                {
                    m_flag->clear(std::memory_order_release);
                }
            }

            [[nodiscard]] bool Acquired() const noexcept { return m_acquired; }

        private:
            std::atomic_flag* m_flag;
            bool              m_acquired;
        };

        class AtomicFlagRelease final
        {
        public:
            explicit AtomicFlagRelease(std::atomic_flag& flag) noexcept
                : m_flag(&flag)
            {
            }

            AtomicFlagRelease(const AtomicFlagRelease&)            = delete;
            AtomicFlagRelease& operator=(const AtomicFlagRelease&) = delete;

            ~AtomicFlagRelease()
            {
                m_flag->clear(std::memory_order_release);
            }

        private:
            std::atomic_flag* m_flag;
        };

        [[nodiscard]] TlsError InvalidState(std::string message)
        {
            return detail::MakeTlsError(TlsErrorCategory::State, TlsErrorCode::InvalidState, std::move(message));
        }

        [[nodiscard]] TlsError ConcurrentOperation(std::string message)
        {
            return detail::MakeTlsError(
                    TlsErrorCategory::State,
                    TlsErrorCode::ConcurrentOperation,
                    std::move(message));
        }

        [[nodiscard]] TlsError CancellationError(bool timedOut)
        {
            return detail::MakeTlsError(
                    timedOut ? TlsErrorCategory::Timeout : TlsErrorCategory::Cancellation,
                    timedOut ? TlsErrorCode::TimedOut : TlsErrorCode::Canceled,
                    timedOut ? "TLS operation timed out" : "TLS operation was canceled");
        }

        template<typename T>
        [[nodiscard]] NGIN::Async::Completion<void, TlsError> MapNetFailureToVoid(
                NGIN::Async::Completion<T, NGIN::Net::NetError> completion,
                bool                                            timedOut)
        {
            using Out = NGIN::Async::Completion<void, TlsError>;
            if (completion.IsDomainError())
            {
                return Out::DomainFailure(detail::MakeTransportError(
                        std::move(completion).DomainError(), "inner transport operation failed"));
            }
            if (completion.IsCanceled())
            {
                return Out::DomainFailure(CancellationError(timedOut));
            }
            if (completion.IsFault())
            {
                return Out::Faulted(std::move(completion).Fault());
            }
            return Out::Faulted(NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::InvalidTaskUsage));
        }

        NGIN::Async::Task<void, TlsError> PropagateVoidCompletion(
                NGIN::Async::Completion<void, TlsError> completion)
        {
            if (completion.IsDomainError())
            {
                co_await NGIN::Async::DomainFailure(std::move(completion).DomainError());
                co_return;
            }
            if (completion.IsCanceled())
            {
                co_await NGIN::Async::Canceled();
                co_return;
            }
            if (completion.IsFault())
            {
                co_await NGIN::Async::Faulted(std::move(completion).Fault());
                co_return;
            }
            co_return;
        }
    }// namespace

    TlsStream::TlsStream(
            std::unique_ptr<NGIN::Net::Transport::IByteStream> inner,
            std::unique_ptr<detail::TlsSession>                session,
            bool                                               allowTruncatedEof) noexcept
        : m_inner(std::move(inner)), m_session(std::move(session)), m_allowTruncatedEof(allowTruncatedEof)
    {
    }

    TlsStream::~TlsStream()
    {
        if (m_inner)
        {
            static_cast<void>(m_inner->Close());
        }
    }

    TlsExpected<std::unique_ptr<TlsStream>> TlsStream::CreateClient(
            std::unique_ptr<NGIN::Net::Transport::IByteStream> inner,
            const TlsContext&                                  context,
            TlsClientOptions                                   options)
    {
        if (!inner || !context.m_state || !context.m_state->IsClient())
        {
            return NGIN::Utilities::Unexpected(InvalidState("client TLS stream requires a client context and inner stream"));
        }

        auto session = context.m_state->CreateClientSession(options);
        if (!session.HasValue())
        {
            return NGIN::Utilities::Unexpected(session.Error());
        }
        return std::unique_ptr<TlsStream>(
                new TlsStream(std::move(inner), std::move(session.Value()), options.allowTruncatedEof));
    }

    TlsExpected<std::unique_ptr<TlsStream>> TlsStream::CreateServer(
            std::unique_ptr<NGIN::Net::Transport::IByteStream> inner,
            const TlsContext&                                  context,
            TlsServerOptions                                   options)
    {
        if (!inner || !context.m_state || context.m_state->IsClient())
        {
            return NGIN::Utilities::Unexpected(InvalidState("server TLS stream requires a server context and inner stream"));
        }

        auto session = context.m_state->CreateServerSession(options);
        if (!session.HasValue())
        {
            return NGIN::Utilities::Unexpected(session.Error());
        }
        return std::unique_ptr<TlsStream>(
                new TlsStream(std::move(inner), std::move(session.Value()), options.allowTruncatedEof));
    }

    NGIN::Async::Task<void, TlsError> TlsStream::FlushEncrypted(
            NGIN::Async::TaskContext&      ctx,
            NGIN::Async::CancellationToken token)
    {
        auto yieldContext = NGIN::Async::TaskContext {ctx.GetExecutor()};
        while (m_transportWriteActive.test_and_set(std::memory_order_acq_rel))
        {
            if (token.IsCancellationRequested())
            {
                co_await NGIN::Async::DomainFailure(CancellationError(
                        m_activeTimeoutFired && m_activeTimeoutFired->load(std::memory_order_acquire)));
                co_return;
            }
            co_await yieldContext.YieldNow();
        }
        AtomicFlagRelease releaseGuard {m_transportWriteActive};

        while (true)
        {
            auto encrypted = [&] {
                std::lock_guard lock(m_providerMutex);
                return m_session->DrainEncrypted();
            }();
            if (!encrypted.HasValue())
            {
                co_await NGIN::Async::DomainFailure(encrypted.Error());
                co_return;
            }
            if (encrypted.Value().empty())
            {
                co_return;
            }

            std::size_t offset = 0;
            while (offset < encrypted.Value().size())
            {
                auto operation = NGIN::Async::Spawn(
                        ctx,
                        m_inner->WriteAsync(
                                ctx,
                                NGIN::Net::ConstByteSpan {encrypted.Value().data() + offset, encrypted.Value().size() - offset},
                                token));
                auto completion = co_await operation;
                if (!completion)
                {
                    co_await PropagateVoidCompletion(MapNetFailureToVoid(
                            std::move(completion),
                            m_activeTimeoutFired && m_activeTimeoutFired->load(std::memory_order_acquire)));
                    co_return;
                }
                if (completion.Value() == 0)
                {
                    co_await NGIN::Async::DomainFailure(detail::MakeTransportError(
                            NGIN::Net::NetError {NGIN::Net::NetErrorCode::Disconnected},
                            "inner transport closed while writing TLS records"));
                    co_return;
                }
                offset += completion.Value();
            }
        }
    }

    NGIN::Async::Task<void, TlsError> TlsStream::ReceiveEncrypted(
            NGIN::Async::TaskContext&      ctx,
            NGIN::Async::CancellationToken token)
    {
        std::array<NGIN::Byte, 16 * 1024> buffer {};
        auto                              operation  = NGIN::Async::Spawn(ctx, m_inner->ReadAsync(ctx, buffer, token));
        auto                              completion = co_await operation;
        if (!completion)
        {
            co_await PropagateVoidCompletion(MapNetFailureToVoid(
                    std::move(completion),
                    m_activeTimeoutFired && m_activeTimeoutFired->load(std::memory_order_acquire)));
            co_return;
        }

        auto fed = [&]() -> TlsExpected<void> {
            std::lock_guard lock(m_providerMutex);
            if (completion.Value() == 0)
            {
                m_session->NotifyTransportEof();
                return {};
            }
            return m_session->FeedEncrypted(NGIN::Net::ConstByteSpan {buffer.data(), completion.Value()});
        }();
        if (!fed.HasValue())
        {
            co_await NGIN::Async::DomainFailure(fed.Error());
        }
        co_return;
    }

    NGIN::Async::Task<void, TlsError> TlsStream::HandshakeAsync(
            NGIN::Async::TaskContext&      ctx,
            NGIN::Async::CancellationToken token,
            TlsHandshakeOptions            options)
    {
        AtomicFlagGuard control {m_controlActive};
        if (!control.Acquired() || m_readActive.test(std::memory_order_acquire) ||
            m_writeActive.test(std::memory_order_acquire))
        {
            co_await NGIN::Async::DomainFailure(ConcurrentOperation("TLS handshake overlaps another stream operation"));
            co_return;
        }
        if (m_state.load(std::memory_order_acquire) != TlsStreamState::Created)
        {
            co_await NGIN::Async::DomainFailure(InvalidState("TLS handshake is valid only for a newly created stream"));
            co_return;
        }

        m_state.store(TlsStreamState::Handshaking, std::memory_order_release);
        auto operationContext = ctx.WithLinkedCancellationToken(token);

        struct TimeoutState final
        {
            NGIN::Async::CancellationSource    source;
            std::shared_ptr<std::atomic<bool>> fired {std::make_shared<std::atomic<bool>>(false)};
            std::shared_ptr<std::atomic<bool>> armed {std::make_shared<std::atomic<bool>>(true)};
        };
        std::shared_ptr<TimeoutState> timeoutState;
        if (options.timeout.count() > 0)
        {
            timeoutState         = std::make_shared<TimeoutState>();
            m_activeTimeoutFired = timeoutState->fired;
            operationContext.BindLinkedCancellationToken(timeoutState->source.GetToken());
            auto executor = ctx.GetExecutor();
            if (!executor.IsValid())
            {
                m_state.store(TlsStreamState::Failed, std::memory_order_release);
                co_await NGIN::Async::DomainFailure(InvalidState("TLS handshake requires a valid executor"));
                co_return;
            }
            executor.ExecuteAfter(
                    [timeoutState]() noexcept {
                        if (timeoutState->armed->exchange(false, std::memory_order_acq_rel))
                        {
                            timeoutState->fired->store(true, std::memory_order_release);
                            timeoutState->source.Cancel();
                        }
                    },
                    NGIN::Units::Milliseconds(static_cast<double>(options.timeout.count())));
        }

        const auto finishTimeout = [&]() noexcept {
            if (timeoutState)
            {
                timeoutState->armed->store(false, std::memory_order_release);
            }
            m_activeTimeoutFired.reset();
        };

        while (true)
        {
            auto step = [&] {
                std::lock_guard lock(m_providerMutex);
                return m_session->Handshake();
            }();
            if (!step.HasValue())
            {
                finishTimeout();
                m_state.store(TlsStreamState::Failed, std::memory_order_release);
                co_await NGIN::Async::DomainFailure(step.Error());
                co_return;
            }

            auto flushOperation = NGIN::Async::Spawn(
                    operationContext,
                    FlushEncrypted(operationContext, operationContext.GetCancellationToken()));
            auto flushCompletion = co_await flushOperation;
            if (!flushCompletion)
            {
                finishTimeout();
                m_state.store(TlsStreamState::Failed, std::memory_order_release);
                co_await PropagateVoidCompletion(std::move(flushCompletion));
                co_return;
            }

            if (step.Value().status == detail::TlsProviderStatus::Complete)
            {
                finishTimeout();
                CaptureSessionMetadata();
                m_state.store(TlsStreamState::Open, std::memory_order_release);
                co_return;
            }
            if (step.Value().status == detail::TlsProviderStatus::WantRead)
            {
                auto readOperation = NGIN::Async::Spawn(
                        operationContext,
                        ReceiveEncrypted(operationContext, operationContext.GetCancellationToken()));
                auto readCompletion = co_await readOperation;
                if (!readCompletion)
                {
                    finishTimeout();
                    m_state.store(TlsStreamState::Failed, std::memory_order_release);
                    co_await PropagateVoidCompletion(std::move(readCompletion));
                    co_return;
                }
                continue;
            }
            if (step.Value().status == detail::TlsProviderStatus::Closed)
            {
                finishTimeout();
                m_state.store(TlsStreamState::Failed, std::memory_order_release);
                co_await NGIN::Async::DomainFailure(detail::MakeTlsError(
                        TlsErrorCategory::Protocol,
                        TlsErrorCode::HandshakeFailed,
                        "peer closed during TLS handshake"));
                co_return;
            }
        }
    }

    NGIN::Async::Task<NGIN::UInt32, TlsError> TlsStream::ReadTlsAsync(
            NGIN::Async::TaskContext&      ctx,
            NGIN::Net::ByteSpan            destination,
            NGIN::Async::CancellationToken token)
    {
        AtomicFlagGuard active {m_readActive};
        if (!active.Acquired() || m_controlActive.test(std::memory_order_acquire))
        {
            co_return ConcurrentOperation("overlapping TLS reads are not allowed");
        }
        if (m_state.load(std::memory_order_acquire) != TlsStreamState::Open)
        {
            co_return InvalidState("TLS reads require an open, handshaken stream");
        }
        if (destination.empty())
        {
            co_return NGIN::UInt32 {0};
        }

        auto operationContext = ctx.WithLinkedCancellationToken(token);
        while (true)
        {
            auto step = [&] {
                std::lock_guard lock(m_providerMutex);
                return m_session->Read(destination);
            }();
            if (!step.HasValue())
            {
                if (m_allowTruncatedEof && step.Error().code == TlsErrorCode::TruncatedStream)
                {
                    m_state.store(TlsStreamState::Closed, std::memory_order_release);
                    co_return NGIN::UInt32 {0};
                }
                m_state.store(TlsStreamState::Failed, std::memory_order_release);
                co_return step.Error();
            }

            auto flushOperation = NGIN::Async::Spawn(
                    operationContext,
                    FlushEncrypted(operationContext, operationContext.GetCancellationToken()));
            auto flushCompletion = co_await flushOperation;
            if (!flushCompletion)
            {
                m_state.store(TlsStreamState::Failed, std::memory_order_release);
                if (flushCompletion.IsDomainError())
                {
                    co_return std::move(flushCompletion).DomainError();
                }
                if (flushCompletion.IsFault())
                {
                    co_return NGIN::Async::Completion<NGIN::UInt32, TlsError>::Faulted(
                            std::move(flushCompletion).Fault());
                }
                co_return CancellationError(false);
            }

            if (step.Value().status == detail::TlsProviderStatus::Complete)
            {
                co_return step.Value().bytes;
            }
            if (step.Value().status == detail::TlsProviderStatus::Closed)
            {
                m_state.store(TlsStreamState::Closed, std::memory_order_release);
                co_return NGIN::UInt32 {0};
            }
            if (step.Value().status == detail::TlsProviderStatus::WantRead)
            {
                auto readOperation = NGIN::Async::Spawn(
                        operationContext,
                        ReceiveEncrypted(operationContext, operationContext.GetCancellationToken()));
                auto readCompletion = co_await readOperation;
                if (!readCompletion)
                {
                    m_state.store(TlsStreamState::Failed, std::memory_order_release);
                    if (readCompletion.IsDomainError())
                    {
                        co_return std::move(readCompletion).DomainError();
                    }
                    if (readCompletion.IsFault())
                    {
                        co_return NGIN::Async::Completion<NGIN::UInt32, TlsError>::Faulted(
                                std::move(readCompletion).Fault());
                    }
                    co_return CancellationError(false);
                }
            }
        }
    }

    NGIN::Async::Task<NGIN::UInt32, TlsError> TlsStream::WriteTlsAsync(
            NGIN::Async::TaskContext&      ctx,
            NGIN::Net::ConstByteSpan       source,
            NGIN::Async::CancellationToken token)
    {
        AtomicFlagGuard active {m_writeActive};
        if (!active.Acquired() || m_controlActive.test(std::memory_order_acquire))
        {
            co_return ConcurrentOperation("overlapping TLS writes are not allowed");
        }
        if (m_state.load(std::memory_order_acquire) != TlsStreamState::Open)
        {
            co_return InvalidState("TLS writes require an open, handshaken stream");
        }
        if (source.empty())
        {
            co_return NGIN::UInt32 {0};
        }

        auto operationContext = ctx.WithLinkedCancellationToken(token);
        while (true)
        {
            auto step = [&] {
                std::lock_guard lock(m_providerMutex);
                return m_session->Write(source);
            }();
            if (!step.HasValue())
            {
                m_state.store(TlsStreamState::Failed, std::memory_order_release);
                co_return step.Error();
            }

            auto flushOperation = NGIN::Async::Spawn(
                    operationContext,
                    FlushEncrypted(operationContext, operationContext.GetCancellationToken()));
            auto flushCompletion = co_await flushOperation;
            if (!flushCompletion)
            {
                m_state.store(TlsStreamState::Failed, std::memory_order_release);
                if (flushCompletion.IsDomainError())
                {
                    co_return std::move(flushCompletion).DomainError();
                }
                if (flushCompletion.IsFault())
                {
                    co_return NGIN::Async::Completion<NGIN::UInt32, TlsError>::Faulted(
                            std::move(flushCompletion).Fault());
                }
                co_return CancellationError(false);
            }

            if (step.Value().status == detail::TlsProviderStatus::Complete)
            {
                co_return step.Value().bytes;
            }
            if (step.Value().status == detail::TlsProviderStatus::Closed)
            {
                m_state.store(TlsStreamState::Closed, std::memory_order_release);
                co_return detail::MakeTlsError(
                        TlsErrorCategory::State, TlsErrorCode::Closed, "TLS stream is closed");
            }
            if (step.Value().status == detail::TlsProviderStatus::WantRead)
            {
                auto readOperation = NGIN::Async::Spawn(
                        operationContext,
                        ReceiveEncrypted(operationContext, operationContext.GetCancellationToken()));
                auto readCompletion = co_await readOperation;
                if (!readCompletion)
                {
                    m_state.store(TlsStreamState::Failed, std::memory_order_release);
                    if (readCompletion.IsDomainError())
                    {
                        co_return std::move(readCompletion).DomainError();
                    }
                    if (readCompletion.IsFault())
                    {
                        co_return NGIN::Async::Completion<NGIN::UInt32, TlsError>::Faulted(
                                std::move(readCompletion).Fault());
                    }
                    co_return CancellationError(false);
                }
            }
        }
    }

    NGIN::Async::Task<void, TlsError> TlsStream::ShutdownAsync(
            NGIN::Async::TaskContext&      ctx,
            NGIN::Async::CancellationToken token)
    {
        AtomicFlagGuard control {m_controlActive};
        if (!control.Acquired() || m_readActive.test(std::memory_order_acquire) ||
            m_writeActive.test(std::memory_order_acquire))
        {
            co_await NGIN::Async::DomainFailure(ConcurrentOperation("TLS shutdown overlaps another stream operation"));
            co_return;
        }
        const auto initial = m_state.load(std::memory_order_acquire);
        if (initial == TlsStreamState::Closed)
        {
            co_return;
        }
        if (initial != TlsStreamState::Open)
        {
            co_await NGIN::Async::DomainFailure(InvalidState("TLS shutdown requires an open stream"));
            co_return;
        }

        m_state.store(TlsStreamState::ShuttingDown, std::memory_order_release);
        auto operationContext = ctx.WithLinkedCancellationToken(token);
        while (true)
        {
            auto step = [&] {
                std::lock_guard lock(m_providerMutex);
                return m_session->Shutdown();
            }();
            if (!step.HasValue())
            {
                if (m_allowTruncatedEof && step.Error().code == TlsErrorCode::TruncatedStream)
                {
                    m_state.store(TlsStreamState::Closed, std::memory_order_release);
                    static_cast<void>(m_inner->Close());
                    co_return;
                }
                m_state.store(TlsStreamState::Failed, std::memory_order_release);
                co_await NGIN::Async::DomainFailure(step.Error());
                co_return;
            }

            auto flushOperation = NGIN::Async::Spawn(
                    operationContext,
                    FlushEncrypted(operationContext, operationContext.GetCancellationToken()));
            auto flushCompletion = co_await flushOperation;
            if (!flushCompletion)
            {
                m_state.store(TlsStreamState::Failed, std::memory_order_release);
                co_await PropagateVoidCompletion(std::move(flushCompletion));
                co_return;
            }

            if (step.Value().status == detail::TlsProviderStatus::Complete ||
                step.Value().status == detail::TlsProviderStatus::Closed)
            {
                m_state.store(TlsStreamState::Closed, std::memory_order_release);
                static_cast<void>(m_inner->Close());
                co_return;
            }
            if (step.Value().status == detail::TlsProviderStatus::WantRead)
            {
                auto readOperation = NGIN::Async::Spawn(
                        operationContext,
                        ReceiveEncrypted(operationContext, operationContext.GetCancellationToken()));
                auto readCompletion = co_await readOperation;
                if (!readCompletion)
                {
                    m_state.store(TlsStreamState::Failed, std::memory_order_release);
                    co_await PropagateVoidCompletion(std::move(readCompletion));
                    co_return;
                }
            }
        }
    }

    NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> TlsStream::ReadAsync(
            NGIN::Async::TaskContext&      ctx,
            NGIN::Net::ByteSpan            destination,
            NGIN::Async::CancellationToken token)
    {
        auto operation  = NGIN::Async::Spawn(ctx, ReadTlsAsync(ctx, destination, token));
        auto completion = co_await operation;
        if (completion.IsDomainError())
        {
            co_return std::move(completion).DomainError().ToNetError();
        }
        if (completion.IsCanceled())
        {
            co_return NGIN::Async::Completion<NGIN::UInt32, NGIN::Net::NetError>::Canceled();
        }
        if (completion.IsFault())
        {
            co_return NGIN::Async::Completion<NGIN::UInt32, NGIN::Net::NetError>::Faulted(
                    std::move(completion).Fault());
        }
        co_return completion.Value();
    }

    NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> TlsStream::WriteAsync(
            NGIN::Async::TaskContext&      ctx,
            NGIN::Net::ConstByteSpan       source,
            NGIN::Async::CancellationToken token)
    {
        auto operation  = NGIN::Async::Spawn(ctx, WriteTlsAsync(ctx, source, token));
        auto completion = co_await operation;
        if (completion.IsDomainError())
        {
            co_return std::move(completion).DomainError().ToNetError();
        }
        if (completion.IsCanceled())
        {
            co_return NGIN::Async::Completion<NGIN::UInt32, NGIN::Net::NetError>::Canceled();
        }
        if (completion.IsFault())
        {
            co_return NGIN::Async::Completion<NGIN::UInt32, NGIN::Net::NetError>::Faulted(
                    std::move(completion).Fault());
        }
        co_return completion.Value();
    }

    NGIN::Net::NetExpected<void> TlsStream::Close()
    {
        m_state.store(TlsStreamState::Closed, std::memory_order_release);
        if (!m_inner)
        {
            return {};
        }
        return m_inner->Close();
    }

    TlsStreamState TlsStream::State() const noexcept
    {
        return m_state.load(std::memory_order_acquire);
    }

    std::string_view TlsStream::NegotiatedProtocol() const noexcept
    {
        return m_negotiatedProtocol;
    }

    std::string_view TlsStream::ServerName() const noexcept
    {
        return m_serverName;
    }

    const std::optional<NGIN::Crypto::Certificates::Certificate>& TlsStream::PeerCertificate() const noexcept
    {
        return m_peerCertificate;
    }

    NGIN::Net::Transport::IByteStream* TlsStream::Inner() noexcept
    {
        return m_inner.get();
    }

    void TlsStream::CaptureSessionMetadata()
    {
        std::lock_guard lock(m_providerMutex);
        m_negotiatedProtocol = m_session->NegotiatedProtocol();
        m_serverName         = m_session->ServerName();
        auto peerDer         = m_session->PeerCertificateDer();
        if (!peerDer.empty())
        {
            auto parsed = NGIN::Crypto::Certificates::ParseX509Certificate(peerDer);
            if (parsed.HasValue())
            {
                m_peerCertificate = std::move(parsed.Value());
            }
        }
    }
}// namespace NGIN::Net::TLS
