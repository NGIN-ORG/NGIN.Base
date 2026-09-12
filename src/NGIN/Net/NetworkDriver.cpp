#include "NetworkDriver.hpp"
#include "../IO/RuntimeLoop.hpp"
#include "SocketPlatform.hpp"
#include "SocketState.hpp"

#include <NGIN/Async/Task.hpp>
#include <NGIN/Net/Sockets/TcpSocket.hpp>
#include <NGIN/Net/Sockets/UdpSocket.hpp>

#include <atomic>
#include <limits>
#include <mutex>
#include <unordered_map>

#if !defined(NGIN_PLATFORM_WINDOWS)
namespace NGIN::Net
{
    struct NetworkDriver::Impl final
    {
        using Loop   = NGIN::IO::detail::RuntimeLoop;
        using Poller = NGIN::IO::detail::RuntimePoller;
        enum class Kind
        {
            Send,
            Receive,
            SendTo,
            ReceiveFrom,
            Connect,
            Accept
        };

        struct Operation final
        {
            Impl*                                  owner {};
            Kind                                   kind {};
            int                                    descriptor {-1};
            bool                                   read {false};
            bool                                   done {false};
            bool                                   canceled {false};
            bool                                   connectStarted {false};
            bool                                   monitoringFailed {false};
            std::uint64_t                          identifier {};
            ConstByteSpan                          source;
            ByteSpan                               destination;
            Endpoint                               endpoint;
            sockaddr_storage                       address {};
            socklen_t                              addressLength {};
            NGIN::UInt32                           bytes {};
            std::shared_ptr<detail::SocketState>   accepted;
            std::atomic<bool>                      cancellationRequested {false};
            NGIN::Async::CancellationRegistration  cancellation;
            NGIN::Async::CancellationRegistration  closeCancellation;
            detail::SocketLease                    lease;
            NGIN::Execution::CompletionReservation delivery;
            NGIN::Execution::CompletionReservation control;
            NGIN::Async::AsyncFault                fault;
            NetError                               error;
        };

        struct SocketRegistration final : Loop::Handler
        {
            explicit SocketRegistration(Impl& backend) : owner(backend) {}
            void Ready(const Poller::Event& event) noexcept override
            {
                std::shared_ptr<Operation> reading;
                std::shared_ptr<Operation> writing;
                {
                    std::lock_guard lock(owner.m_mutex);
                    if (event.ready & (Poller::Read | Poller::Error))
                        reading = reader;
                    if (event.ready & (Poller::Write | Poller::Error))
                        writing = writer;
                }
                if (reading)
                    owner.Advance(reading, false);
                if (writing)
                    owner.Advance(writing, false);
            }
            void Stop() noexcept override
            {
                std::shared_ptr<Operation> reading;
                std::shared_ptr<Operation> writing;
                {
                    std::lock_guard lock(owner.m_mutex);
                    reading = reader;
                    writing = writer;
                }
                if (reading)
                    owner.Advance(reading, true);
                if (writing)
                    owner.Advance(writing, true);
            }
            Impl&                      owner;
            std::uint64_t              identifier {};
            unsigned                   interests {};
            std::shared_ptr<Operation> reader;
            std::shared_ptr<Operation> writer;
        };

        explicit Impl(NGIN::IO::Runtime& runtime) : m_loop(NGIN::IO::detail::RuntimeAccess::Loop(runtime)) {}
        ~Impl()
        {
            if (!m_sockets.empty())
                std::terminate();
        }
        void Stop() noexcept { m_stop.store(true, std::memory_order_release); }

        // Caller owns m_mutex. All native calls use the admitted handle, never
        // the public wrapper, which may have moved or requested close meanwhile.
        bool Try(Operation& operation, bool cancel) noexcept
        {
            operation.canceled = cancel || m_stop.load(std::memory_order_acquire) ||
                                 operation.cancellationRequested.load(std::memory_order_acquire) ||
                                 operation.lease.IsClosing();
            if (operation.canceled || !operation.error.IsOk())
                return true;
            ssize_t result    = -1;
            int     sendFlags = 0;
#if defined(MSG_NOSIGNAL)
            sendFlags |= MSG_NOSIGNAL;
#endif
            switch (operation.kind)
            {
                case Kind::Send:
                    result = ::send(operation.descriptor, operation.source.data(), operation.source.size(), sendFlags);
                    break;
                case Kind::Receive:
                    result = ::recv(operation.descriptor, operation.destination.data(), operation.destination.size(), 0);
                    break;
                case Kind::SendTo:
                    result = ::sendto(operation.descriptor, operation.source.data(), operation.source.size(), sendFlags,
                                      reinterpret_cast<const sockaddr*>(&operation.address), operation.addressLength);
                    break;
                case Kind::ReceiveFrom:
                    operation.addressLength = sizeof(operation.address);
                    result                  = ::recvfrom(operation.descriptor, operation.destination.data(), operation.destination.size(), 0,
                                                         reinterpret_cast<sockaddr*>(&operation.address), &operation.addressLength);
                    if (result >= 0)
                        operation.endpoint = detail::FromSockAddr(operation.address, operation.addressLength);
                    break;
                case Kind::Connect: {
                    if (!operation.connectStarted)
                    {
                        result                   = ::connect(operation.descriptor, reinterpret_cast<const sockaddr*>(&operation.address),
                                                             operation.addressLength);
                        operation.connectStarted = true;
                    }
                    else
                    {
                        int       error  = 0;
                        socklen_t length = sizeof(error);
                        if (::getsockopt(operation.descriptor, SOL_SOCKET, SO_ERROR, &error, &length) != 0)
                        {
                            operation.error = detail::LastError();
                            return true;
                        }
                        if (error == 0)
                        {
                            // Registration can observe an unconnected socket
                            // before connect starts. Do not mistake that stale
                            // readiness for an established connection.
                            sockaddr_storage peer {};
                            socklen_t        peerLength = sizeof(peer);
                            if (::getpeername(operation.descriptor, reinterpret_cast<sockaddr*>(&peer), &peerLength) == 0)
                                return true;
                            if (errno == ENOTCONN)
                                return false;
                            operation.error = detail::LastError();
                            return true;
                        }
                        operation.error = detail::MapError(error);
                        if (detail::IsWouldBlock(operation.error) || detail::IsInProgress(operation.error))
                        {
                            operation.error = {};
                            return false;
                        }
                        return true;
                    }
                    break;
                }
                case Kind::Accept: {
#if defined(__linux__)
                    const int accepted = ::accept4(operation.descriptor, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
                    const int accepted = ::accept(operation.descriptor, nullptr, nullptr);
#endif
                    if (accepted < 0)
                        break;
                    // The state was allocated before accept, so ownership cannot
                    // be lost to allocation failure after accepting a connection.
                    operation.accepted->Adopt(accepted);
#if !defined(__linux__)
                    const int flags = ::fcntl(accepted, F_GETFL, 0);
                    if (flags < 0 || ::fcntl(accepted, F_SETFL, flags | O_NONBLOCK) < 0 ||
                        ::fcntl(accepted, F_SETFD, FD_CLOEXEC) < 0)
                    {
                        operation.error = detail::LastError();
                        (void) operation.accepted->RequestClose();
                        return true;
                    }
#endif
#if defined(SO_NOSIGPIPE)
                    const int enabled = 1;
                    if (::setsockopt(accepted, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0)
                    {
                        operation.error = detail::LastError();
                        (void) operation.accepted->RequestClose();
                    }
#endif
                    return true;
                }
            }
            if (result >= 0)
            {
                operation.bytes = static_cast<NGIN::UInt32>(result);
                return true;
            }
            const int nativeError = errno;
            operation.error       = detail::MapError(nativeError);
            if (nativeError == EINTR || detail::IsWouldBlock(operation.error) ||
                (operation.kind == Kind::Connect && detail::IsInProgress(operation.error)))
            {
                operation.error = {};
                return false;
            }
            return true;
        }

        // Retire the readiness registration before releasing its pinned handle.
        // A failed interest update also terminates the other direction rather
        // than leaving it stranded on an invalid registration.
        std::shared_ptr<Operation> DetachLocked(const std::shared_ptr<Operation>& operation) noexcept
        {
            operation->done  = true;
            const auto found = m_sockets.find(operation->descriptor);
            if (found == m_sockets.end() || found->second->identifier != operation->identifier)
                return {};
            auto& registration = *found->second;
            (operation->read ? registration.reader : registration.writer).reset();
            const unsigned interests = (registration.reader ? Poller::Read : 0U) |
                                       (registration.writer ? Poller::Write : 0U);
            if (interests == 0)
            {
                m_loop.Unwatch(registration.identifier);
                m_sockets.erase(found);
            }
            else if (interests != registration.interests)
            {
                const std::error_code error = m_loop.Modify(registration.identifier, interests);
                if (!error)
                {
                    registration.interests = interests;
                    return {};
                }
                auto peer   = registration.reader ? registration.reader : registration.writer;
                peer->error = detail::MapError(error.value());
                return peer;
            }
            return {};
        }

        static void Retire(const std::shared_ptr<Operation>& operation) noexcept
        {
            // Reset joins concurrent publication of the cancellation ticket.
            // No callback can mutate that ticket after these registrations end.
            operation->cancellation.Reset();
            operation->closeCancellation.Reset();
            // POSIX has no cancellation primitive for a pending connect. Close
            // it on cancellation or monitoring failure so later operations cannot
            // inherit an unobserved connection.
            if ((operation->canceled || operation->monitoringFailed) &&
                operation->kind == Kind::Connect && operation->connectStarted)
                operation->lease.RequestClose();
            operation->lease.Reset();
            if (operation->canceled || !operation->error.IsOk() || !operation->fault.IsOk())
                operation->accepted.reset();
        }

        void Advance(const std::shared_ptr<Operation>& operation, bool cancel) noexcept
        {
            std::shared_ptr<Operation> failedPeer;
            {
                std::lock_guard lock(m_mutex);
                if (operation->done || !Try(*operation, cancel))
                    return;
                failedPeer = DetachLocked(operation);
            }
            Retire(operation);
            if (failedPeer)
                Advance(failedPeer, false);
            operation->delivery.Dispatch();
            operation->control.Reset();
        }

        bool Watch(const std::shared_ptr<Operation>& operation)
        {
            const auto existing = m_sockets.find(operation->descriptor);
            if (existing != m_sockets.end())
            {
                auto& slot      = *existing->second;
                auto& direction = operation->read ? slot.reader : slot.writer;
                if (direction)
                {
                    operation->error = NetError {NetErrorCode::OperationInProgress};
                    return false;
                }
                operation->identifier = slot.identifier;
                direction             = operation;
                return true;
            }
            auto slot                                       = std::make_shared<SocketRegistration>(*this);
            (operation->read ? slot->reader : slot->writer) = operation;
            m_sockets.emplace(operation->descriptor, slot);
            auto watched = m_loop.ReserveWatch(static_cast<std::uintptr_t>(operation->descriptor), slot);
            if (!watched)
            {
                m_sockets.erase(operation->descriptor);
                if (watched.error() == std::errc::operation_canceled)
                    operation->canceled = true;
                else
                    operation->error = detail::MapError(watched.error().value());
                return false;
            }
            slot->identifier = operation->identifier = *watched;
            return true;
        }

        bool Arm(const std::shared_ptr<Operation>& operation) noexcept
        {
            auto&          slot      = *m_sockets.find(operation->descriptor)->second;
            const unsigned interests = (slot.reader ? Poller::Read : 0U) | (slot.writer ? Poller::Write : 0U);
            // Detach must restore the peer even if a platform update fails partially.
            slot.interests = interests;
            if (const std::error_code error = m_loop.Modify(slot.identifier, interests))
            {
                operation->monitoringFailed = true;
                operation->error            = detail::MapError(error.value());
                return false;
            }
            return true;
        }

        struct OperationAwaiter final
        {
            Impl&                                owner;
            std::shared_ptr<detail::SocketState> socket;
            NGIN::Execution::ExecutorRef         executor;
            NGIN::Async::CancellationToken       token;
            std::shared_ptr<Operation>           state;

            bool await_ready() const noexcept { return false; }
            bool Setup(const std::shared_ptr<Operation>& operation, std::coroutine_handle<> continuation) noexcept
            {
                operation->owner = &owner;
                operation->read  = operation->kind == Kind::Receive || operation->kind == Kind::ReceiveFrom ||
                                  operation->kind == Kind::Accept;
                if (owner.m_stop.load(std::memory_order_acquire) ||
                    owner.m_loop.GetState() != Loop::State::Running || token.IsCancellationRequested())
                {
                    operation->canceled = true;
                    return false;
                }
                auto delivery = executor.ReserveCompletion(NGIN::Execution::WorkItem(continuation));
                if (!delivery)
                {
                    operation->fault = NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed,
                                                                   static_cast<int>(delivery.error()));
                    return false;
                }
                operation->delivery = std::move(*delivery);
                auto control        = owner.m_loop.ReserveOperation(NGIN::Execution::WorkItem([operation] {
                    operation->owner->Advance(operation, true);
                }));
                if (!control)
                {
                    operation->fault = NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed,
                                                                   static_cast<int>(control.error()));
                    return false;
                }
                operation->control        = std::move(*control);
                const unsigned directions = operation->kind == Kind::Connect ? detail::SocketState::Exclusive : (operation->read ? detail::SocketState::Read : detail::SocketState::Write);
                auto           lease      = detail::SocketLease::Acquire(socket, directions);
                if (!lease)
                {
                    operation->error = lease.error();
                    return false;
                }
                operation->lease      = std::move(*lease);
                operation->descriptor = static_cast<int>(operation->lease.Native());
                if (operation->source.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
                    operation->destination.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                {
                    operation->error = NetError {NetErrorCode::MessageTooLarge};
                    return false;
                }
                if ((operation->kind == Kind::Connect || operation->kind == Kind::SendTo) &&
                    !detail::ToSockAddr(operation->endpoint, operation->address, operation->addressLength))
                {
                    operation->error = NetError {NetErrorCode::InvalidArgument};
                    return false;
                }
                // Never let a readiness operation block the owner thread. The
                // default Open options already create nonblocking sockets.
                const int flags = ::fcntl(operation->descriptor, F_GETFL, 0);
                if (flags < 0 || (flags & O_NONBLOCK) == 0)
                {
                    operation->error = flags < 0 ? detail::LastError() : NetError {NetErrorCode::InvalidArgument};
                    return false;
                }
                const auto cancel = +[](void* raw) noexcept {
                    auto& pending = *static_cast<Operation*>(raw);
                    if (!pending.cancellationRequested.exchange(true, std::memory_order_acq_rel))
                        pending.control.Dispatch();
                    return false;
                };
                const auto registered = token.Register(operation->cancellation, {}, {}, cancel, operation.get());
                if (!registered)
                {
                    operation->fault = NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::CancellationRegistrationFailed,
                                                                   static_cast<int>(registered.error()));
                    return false;
                }
                const auto closeRegistered = operation->lease.CloseToken().Register(
                        operation->closeCancellation, {}, {}, cancel, operation.get());
                if (!closeRegistered)
                {
                    operation->fault = NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::CancellationRegistrationFailed,
                                                                   static_cast<int>(closeRegistered.error()));
                    return false;
                }
                try
                {
                    if (operation->kind == Kind::Accept)
                        operation->accepted = std::make_shared<detail::SocketState>();
                    // Admission, cancellation, and registration storage are all
                    // secured before the first OS operation, including connect.
                    return owner.Watch(operation);
                } catch (const std::bad_alloc&)
                {
                    operation->error = NetError {NetErrorCode::ResourceExhausted};
                    return false;
                }
            }
            bool await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                const auto                 operation     = state;
                const bool                 inlineAllowed = executor.IsCurrent();
                std::shared_ptr<Operation> failedPeer;
                {
                    std::lock_guard lock(owner.m_mutex);
                    if (Setup(operation, continuation) && !owner.Try(*operation, false) && owner.Arm(operation))
                        return true;
                    failedPeer = owner.DetachLocked(operation);
                }
                Retire(operation);
                if (failedPeer)
                    owner.Advance(failedPeer, false);
                if (!inlineAllowed && operation->delivery.IsValid())
                {
                    operation->delivery.Dispatch();
                    operation->control.Reset();
                    return true;
                }
                operation->delivery.Reset();
                operation->control.Reset();
                return false;
            }
            void await_resume() const noexcept {}
        };

        template<typename Result>
        static NGIN::Async::Task<Result, NetError> Submit(
                NGIN::Async::TaskContext& ctx, NGIN::IO::Runtime* runtime,
                std::shared_ptr<detail::SocketState> socket, Kind kind, ConstByteSpan source,
                ByteSpan destination, Endpoint endpoint, NGIN::Async::CancellationToken token)
        {
            auto backend = runtime ? AcquireNetworkDriver(*runtime) : nullptr;
            if (!backend)
            {
                auto fault = NGIN::Async::MakeAsyncFault(
                        NGIN::Async::AsyncFaultCode::InvalidTaskUsage, 0,
                        "Async socket operations require a bound, running IO::Runtime");
                if constexpr (std::is_void_v<Result>)
                {
                    co_await NGIN::Async::Faulted(std::move(fault));
                    co_return;
                }
                else
                    co_return NGIN::Async::Completion<Result, NetError>::Faulted(std::move(fault));
            }
            auto operationContext  = ctx.WithLinkedCancellationToken(token);
            auto operation         = std::make_shared<Operation>();
            operation->kind        = kind;
            operation->source      = source;
            operation->destination = destination;
            operation->endpoint    = endpoint;
            OperationAwaiter awaiter {*backend->m_impl, std::move(socket), ctx.GetExecutor(),
                                      operationContext.GetCancellationToken(), operation};
            co_await awaiter;
            using Completion = NGIN::Async::Completion<Result, NetError>;
            auto completion  = [&]() -> Completion {
                if (!operation->fault.IsOk())
                    return Completion::Faulted(std::move(operation->fault));
                if (operation->canceled)
                    return Completion::Canceled();
                if (!operation->error.IsOk())
                    return Completion::DomainFailure(operation->error);
                if constexpr (std::is_void_v<Result>)
                    return Completion::Success();
                else if constexpr (std::is_same_v<Result, TcpSocket>)
                    return Completion::Success(TcpSocket(
                            detail::SocketHandleAccess::FromState(std::move(operation->accepted)), true, runtime));
                else if constexpr (std::is_same_v<Result, DatagramReceiveResult>)
                    return Completion::Success(DatagramReceiveResult {operation->endpoint, operation->bytes});
                else
                    return Completion::Success(operation->bytes);
            }();
            if constexpr (std::is_void_v<Result>)
            {
                if (completion.IsFault())
                    co_await NGIN::Async::Faulted(std::move(completion).Fault());
                else if (completion.IsCanceled())
                    co_await NGIN::Async::Canceled();
                else if (completion.IsDomainError())
                    co_await NGIN::Async::DomainFailure(completion.DomainError());
                co_return;
            }
            else
                co_return completion;
        }

        Loop&                                                        m_loop;
        std::mutex                                                   m_mutex;
        std::unordered_map<int, std::shared_ptr<SocketRegistration>> m_sockets;
        std::atomic<bool>                                            m_stop {false};
    };

    NetworkDriver::NetworkDriver(NGIN::IO::Runtime& runtime) : m_impl(std::make_unique<Impl>(runtime)) {}
    NetworkDriver::~NetworkDriver() = default;
    void NetworkDriver::Stop() noexcept
    {
        m_impl->Stop();
    }

    NGIN::Async::Task<NGIN::UInt32, NetError> NetworkDriver::SubmitSend(
            NGIN::Async::TaskContext& ctx, NGIN::IO::Runtime* runtime, std::shared_ptr<detail::SocketState> socket,
            ConstByteSpan data, NGIN::Async::CancellationToken token)
    {
        return Impl::Submit<NGIN::UInt32>(ctx, runtime, std::move(socket), Impl::Kind::Send, data, {}, {}, std::move(token));
    }
    NGIN::Async::Task<NGIN::UInt32, NetError> NetworkDriver::SubmitReceive(
            NGIN::Async::TaskContext& ctx, NGIN::IO::Runtime* runtime, std::shared_ptr<detail::SocketState> socket,
            ByteSpan destination, NGIN::Async::CancellationToken token)
    {
        return Impl::Submit<NGIN::UInt32>(ctx, runtime, std::move(socket), Impl::Kind::Receive, {}, destination, {}, std::move(token));
    }
    NGIN::Async::Task<NGIN::UInt32, NetError> NetworkDriver::SubmitSendTo(
            NGIN::Async::TaskContext& ctx, NGIN::IO::Runtime* runtime, std::shared_ptr<detail::SocketState> socket,
            Endpoint endpoint, ConstByteSpan data, NGIN::Async::CancellationToken token)
    {
        return Impl::Submit<NGIN::UInt32>(ctx, runtime, std::move(socket), Impl::Kind::SendTo, data, {}, endpoint, std::move(token));
    }
    NGIN::Async::Task<DatagramReceiveResult, NetError> NetworkDriver::SubmitReceiveFrom(
            NGIN::Async::TaskContext& ctx, NGIN::IO::Runtime* runtime, std::shared_ptr<detail::SocketState> socket,
            ByteSpan destination, NGIN::Async::CancellationToken token)
    {
        return Impl::Submit<DatagramReceiveResult>(ctx, runtime, std::move(socket), Impl::Kind::ReceiveFrom, {}, destination, {}, std::move(token));
    }
    NGIN::Async::Task<void, NetError> NetworkDriver::SubmitConnect(
            NGIN::Async::TaskContext& ctx, NGIN::IO::Runtime* runtime, std::shared_ptr<detail::SocketState> socket,
            Endpoint endpoint, NGIN::Async::CancellationToken token)
    {
        return Impl::Submit<void>(ctx, runtime, std::move(socket), Impl::Kind::Connect, {}, {}, endpoint, std::move(token));
    }
    NGIN::Async::Task<TcpSocket, NetError> NetworkDriver::SubmitAccept(
            NGIN::Async::TaskContext& ctx, NGIN::IO::Runtime* runtime, std::shared_ptr<detail::SocketState> socket,
            NGIN::Async::CancellationToken token)
    {
        return Impl::Submit<TcpSocket>(ctx, runtime, std::move(socket), Impl::Kind::Accept, {}, {}, {}, std::move(token));
    }
}// namespace NGIN::Net
#endif

namespace NGIN::Net
{
    std::shared_ptr<NetworkDriver> AcquireNetworkDriver(NGIN::IO::Runtime& runtime)
    {
        auto backend = NGIN::IO::detail::RuntimeAccess::Acquire(runtime, NGIN::IO::detail::RuntimeServiceKind::Network, +[](NGIN::IO::Runtime& owner) -> std::shared_ptr<NGIN::IO::detail::RuntimeService> { return std::make_shared<NetworkDriver>(owner); });
        return std::static_pointer_cast<NetworkDriver>(backend);
    }
}// namespace NGIN::Net
