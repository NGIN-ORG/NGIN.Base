#include "NetworkDriver.hpp"
#include "../IO/RuntimeLoop.hpp"
#include "SocketPlatform.hpp"
#include "SocketState.hpp"

#include <NGIN/Async/Task.hpp>
#include <NGIN/Net/Sockets/TcpSocket.hpp>
#include <NGIN/Net/Sockets/UdpSocket.hpp>

#include <array>
#include <atomic>
#include <cassert>
#include <limits>
#include <mutex>

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

        struct Operation final : Loop::Handler
        {
            void                                                        Ready(const Poller::Event& event) noexcept override { owner->Complete(*this, event); }
            void                                                        Stop() noexcept override { owner->Cancel(*this); }
            Impl*                                                       owner {};
            Kind                                                        kind {};
            detail::NativeSocket                                        descriptor {INVALID_SOCKET};
            OVERLAPPED                                                  overlapped {};
            WSABUF                                                      buffer {};
            DWORD                                                       flags {}, initialBytes {};
            sockaddr_storage                                            address {};
            socklen_t                                                   addressLength {};
            std::array<NGIN::Byte, 2 * (sizeof(sockaddr_storage) + 16)> acceptAddresses {};
            ConstByteSpan                                               source;
            ByteSpan                                                    destination;
            Endpoint                                                    endpoint;
            NGIN::UInt32                                                bytes {};
            std::uint64_t                                               identifier {};
            bool                                                        done {false};
            bool                                                        issued {false};
            bool                                                        canceled {false};
            bool                                                        cancellationRequested {false};
            bool                                                        connected {false};
            std::shared_ptr<detail::SocketState>                        accepted;
            detail::SocketLease                                         lease;
            NGIN::Async::CancellationRegistration                       cancellation, closeCancellation;
            NGIN::Execution::CompletionReservation                      delivery, common;
            NGIN::Async::AsyncFault                                     fault;
            NetError                                                    error;
        };

        explicit Impl(NGIN::IO::Runtime& runtime) : m_loop(NGIN::IO::detail::RuntimeAccess::Loop(runtime)) {}
        ~Impl() { assert(m_pending == 0); }
        void Stop() noexcept { m_stop.store(true, std::memory_order_release); }

        void Cancel(Operation& operation) noexcept
        {
            std::lock_guard lock(m_mutex);
            operation.cancellationRequested = true;
            if (operation.issued && !operation.done)
                (void) ::CancelIoEx(reinterpret_cast<HANDLE>(operation.descriptor), &operation.overlapped);
        }

        // All completion storage and cancellation registrations exist before this
        // publishes an OVERLAPPED or changes native socket state.
        bool Start(Operation& operation) noexcept
        {
            if (operation.cancellationRequested || m_stop.load(std::memory_order_acquire) || operation.lease.IsClosing())
            {
                operation.canceled = true;
                return false;
            }
            int result = SOCKET_ERROR;
            switch (operation.kind)
            {
                case Kind::Send:
                    operation.buffer = {static_cast<ULONG>(operation.source.size()),
                                        reinterpret_cast<char*>(const_cast<NGIN::Byte*>(operation.source.data()))};
                    operation.issued = true;
                    result           = ::WSASend(operation.descriptor, &operation.buffer, 1, nullptr, 0, &operation.overlapped, nullptr);
                    break;
                case Kind::Receive:
                    operation.buffer = {static_cast<ULONG>(operation.destination.size()), reinterpret_cast<char*>(operation.destination.data())};
                    operation.issued = true;
                    result           = ::WSARecv(operation.descriptor, &operation.buffer, 1, nullptr, &operation.flags, &operation.overlapped, nullptr);
                    break;
                case Kind::SendTo:
                    operation.buffer = {static_cast<ULONG>(operation.source.size()),
                                        reinterpret_cast<char*>(const_cast<NGIN::Byte*>(operation.source.data()))};
                    operation.issued = true;
                    result           = ::WSASendTo(operation.descriptor, &operation.buffer, 1, nullptr, 0,
                                                   reinterpret_cast<const sockaddr*>(&operation.address), operation.addressLength, &operation.overlapped, nullptr);
                    break;
                case Kind::ReceiveFrom:
                    operation.buffer        = {static_cast<ULONG>(operation.destination.size()), reinterpret_cast<char*>(operation.destination.data())};
                    operation.addressLength = sizeof(operation.address);
                    operation.issued        = true;
                    result                  = ::WSARecvFrom(operation.descriptor, &operation.buffer, 1, nullptr, &operation.flags,
                                                            reinterpret_cast<sockaddr*>(&operation.address), &operation.addressLength, &operation.overlapped, nullptr);
                    break;
                case Kind::Connect: {
                    const auto connect = detail::GetConnectEx(operation.descriptor);
                    if (!connect || !detail::EnsureBoundForConnectEx(operation.descriptor))
                    {
                        operation.error = detail::LastError();
                        return false;
                    }
                    operation.issued = true;
                    result           = connect(operation.descriptor, reinterpret_cast<const sockaddr*>(&operation.address),
                                               operation.addressLength, nullptr, 0, &operation.initialBytes, &operation.overlapped)
                                               ? 0
                                               : SOCKET_ERROR;
                    break;
                }
                case Kind::Accept: {
                    const auto accept = detail::GetAcceptEx(operation.descriptor);
                    if (!accept)
                    {
                        operation.error = detail::LastError();
                        return false;
                    }
                    WSAPROTOCOL_INFOW protocol {};
                    int               length = sizeof(protocol);
                    if (::getsockopt(operation.descriptor, SOL_SOCKET, SO_PROTOCOL_INFOW,
                                     reinterpret_cast<char*>(&protocol), &length) != 0)
                    {
                        operation.error = detail::LastError();
                        return false;
                    }
                    const SOCKET accepted = ::WSASocketW(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO,
                                                         &protocol, 0, WSA_FLAG_OVERLAPPED);
                    if (accepted == INVALID_SOCKET)
                    {
                        operation.error = detail::LastError();
                        return false;
                    }
                    operation.accepted->Adopt(static_cast<SocketHandle::NativeHandle>(accepted));
                    operation.issued            = true;
                    constexpr DWORD addressSize = sizeof(sockaddr_storage) + 16;
                    result                      = accept(operation.descriptor, accepted, operation.acceptAddresses.data(), 0,
                                                         addressSize, addressSize, &operation.initialBytes, &operation.overlapped)
                                                          ? 0
                                                          : SOCKET_ERROR;
                    break;
                }
            }
            if (result == 0 || ::WSAGetLastError() == WSA_IO_PENDING)
                return true;// Successful synchronous overlapped calls also post a packet.
            operation.error = detail::LastError();
            return false;
        }

        void Complete(Operation& operation, const Poller::Event& event) noexcept
        {
            {
                std::lock_guard lock(m_mutex);
                if (operation.done || event.identifier != operation.identifier || event.operation != &operation.overlapped)
                    return;
                operation.done = true;
                --m_pending;
                int error = event.error;
                if (error != 0 && operation.kind != Kind::Accept && operation.kind != Kind::Connect)
                {
                    DWORD bytes {}, flags {};
                    // Convert a failed socket packet to the WinSock error domain
                    // (notably datagram truncation). The packet is already terminal.
                    if (!::WSAGetOverlappedResult(operation.descriptor, &operation.overlapped, &bytes, FALSE, &flags))
                        error = ::WSAGetLastError();
                }
                operation.bytes     = event.bytes;
                operation.error     = error == 0 ? NetError {} : detail::MapError(error);
                operation.connected = operation.kind == Kind::Connect && error == 0;
                if (error == 0 && !operation.cancellationRequested && !operation.lease.IsClosing() && !m_stop.load(std::memory_order_acquire))
                {
                    if (operation.kind == Kind::Connect)
                    {
                        if (::setsockopt(operation.descriptor, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0) != 0)
                            operation.error = detail::LastError();
                    }
                    else if (operation.kind == Kind::Accept)
                    {
                        const SOCKET accepted    = static_cast<SOCKET>(operation.accepted->VisibleNative());
                        u_long       nonBlocking = 1;
                        if (::setsockopt(accepted, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                                         reinterpret_cast<const char*>(&operation.descriptor), sizeof(operation.descriptor)) != 0 ||
                            ::ioctlsocket(accepted, static_cast<long>(FIONBIO), &nonBlocking) != 0)
                            operation.error = detail::LastError();
                        else
                            operation.accepted->SetNonBlocking(true);
                    }
                    else if (operation.kind == Kind::ReceiveFrom)
                        operation.endpoint = detail::FromSockAddr(operation.address, operation.addressLength);
                }
                m_loop.Unwatch(operation.identifier);
            }
            operation.common.Dispatch();
        }

        void Retire(Operation& operation) noexcept
        {
            // Reset joins cancellation before releasing the lease or native state.
            operation.cancellation.Reset();
            operation.closeCancellation.Reset();
            {
                std::lock_guard lock(m_mutex);
                operation.canceled |= operation.cancellationRequested || m_stop.load(std::memory_order_acquire) ||
                                      m_loop.GetState() != Loop::State::Running || (operation.lease.IsValid() && operation.lease.IsClosing());
            }
            if (operation.kind == Kind::Connect &&
                ((operation.issued && operation.canceled) || (operation.connected && !operation.error.IsOk())))
                operation.lease.RequestClose();
            operation.lease.Reset();
            if (operation.canceled || !operation.error.IsOk() || !operation.fault.IsOk())
                operation.accepted.reset();
        }

        void Deliver(const std::shared_ptr<Operation>& operation) noexcept
        {
            Retire(*operation);
            operation->delivery.Dispatch();
            operation->common.Reset();
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
                if (owner.m_stop.load(std::memory_order_acquire) || owner.m_loop.GetState() != Loop::State::Running || token.IsCancellationRequested())
                {
                    operation->canceled = true;
                    return false;
                }
                auto delivery = executor.ReserveCompletion(NGIN::Execution::WorkItem(continuation));
                if (!delivery)
                {
                    operation->fault = NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed, static_cast<int>(delivery.error()));
                    return false;
                }
                operation->delivery = std::move(*delivery);
                auto common         = owner.m_loop.ReserveOperation(NGIN::Execution::WorkItem([operation] { operation->owner->Deliver(operation); }));
                if (!common)
                {
                    operation->fault = NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed, static_cast<int>(common.error()));
                    return false;
                }
                operation->common         = std::move(*common);
                const bool     reading    = operation->kind == Kind::Receive || operation->kind == Kind::ReceiveFrom || operation->kind == Kind::Accept;
                const unsigned directions = operation->kind == Kind::Connect ? detail::SocketState::Exclusive
                                            : reading                        ? detail::SocketState::Read
                                                                             : detail::SocketState::Write;
                auto           lease      = detail::SocketLease::Acquire(socket, directions);
                if (!lease)
                {
                    operation->error = lease.error();
                    return false;
                }
                operation->lease      = std::move(*lease);
                operation->descriptor = static_cast<detail::NativeSocket>(operation->lease.Native());
                if (!socket->IsNonBlocking())
                {
                    operation->error = NetError {NetErrorCode::InvalidArgument};
                    return false;
                }
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
                const auto cancel = +[](void* raw) noexcept {
                    auto& pending = *static_cast<Operation*>(raw);
                    pending.owner->Cancel(pending);
                    return false;
                };
                const auto registered = token.Register(operation->cancellation, {}, {}, cancel, operation.get());
                if (!registered)
                {
                    operation->fault = NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::CancellationRegistrationFailed, static_cast<int>(registered.error()));
                    return false;
                }
                const auto closeRegistered = operation->lease.CloseToken().Register(operation->closeCancellation, {}, {}, cancel, operation.get());
                if (!closeRegistered)
                {
                    operation->fault = NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::CancellationRegistrationFailed, static_cast<int>(closeRegistered.error()));
                    return false;
                }
                try
                {
                    if (operation->kind == Kind::Accept)
                        operation->accepted = std::make_shared<detail::SocketState>();
                } catch (const std::bad_alloc&)
                {
                    operation->error = NetError {NetErrorCode::ResourceExhausted};
                    return false;
                }
                return true;
            }

            bool await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                const auto operation     = state;
                const bool inlineAllowed = executor.IsCurrent();
                if (Setup(operation, continuation))
                {
                    std::lock_guard lock(owner.m_mutex);
                    auto            watched = owner.m_loop.WatchCompletion(static_cast<std::uintptr_t>(operation->descriptor), &operation->overlapped, operation);
                    if (!watched)
                    {
                        if (watched.error() == std::errc::operation_canceled)
                            operation->canceled = true;
                        else if (watched.error() == std::errc::no_buffer_space || watched.error() == std::errc::not_enough_memory)
                            operation->error = NetError {NetErrorCode::ResourceExhausted};
                        else
                            operation->error = detail::MapError(watched.error().value());
                    }
                    else
                    {
                        operation->identifier = *watched;
                        ++owner.m_pending;
                        if (owner.Start(*operation))
                            return true;
                        operation->done = true;
                        --owner.m_pending;
                        owner.m_loop.Unwatch(operation->identifier);
                    }
                }
                owner.Retire(*operation);
                if (!inlineAllowed && operation->delivery.IsValid())
                {
                    operation->delivery.Dispatch();
                    operation->common.Reset();
                    return true;
                }
                operation->delivery.Reset();
                operation->common.Reset();
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

        Loop&             m_loop;
        std::mutex        m_mutex;
        std::atomic<bool> m_stop {false};
        std::size_t       m_pending {};
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
