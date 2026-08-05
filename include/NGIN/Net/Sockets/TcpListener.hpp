/// @file TcpListener.hpp
/// @brief TCP listener socket wrapper.
#pragma once

#include <NGIN/Net/Sockets/SocketHandle.hpp>
#include <NGIN/Net/Sockets/TcpSocket.hpp>
#include <NGIN/Net/Types/AddressFamily.hpp>
#include <NGIN/Net/Types/Endpoint.hpp>
#include <NGIN/Net/Types/NetError.hpp>
#include <NGIN/Net/Types/SocketOptions.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Async
{
    class TaskContext;
    class CancellationToken;
    template<typename T, typename E>
    class Task;
}// namespace NGIN::Async

namespace NGIN::Net
{
    class NetworkDriver;

    /// @brief TCP listen socket with non-blocking accept.
    class NGIN_NET_API TcpListener final
    {
    public:
        /// @brief Constructs a closed listener.
        TcpListener() noexcept = default;
        /// @brief Listeners are non-copyable because they uniquely own a native socket.
        TcpListener(const TcpListener&) = delete;
        /// @brief Listeners are non-copy-assignable because they uniquely own a native socket.
        TcpListener& operator=(const TcpListener&) = delete;
        /// @brief Transfers native socket ownership from another listener.
        TcpListener(TcpListener&&) noexcept = default;
        /// @brief Transfers native socket ownership from another listener.
        TcpListener& operator=(TcpListener&&) noexcept = default;

        /// @brief Creates a non-blocking TCP socket with the requested family and options.
        NetExpected<void> Open(AddressFamily family  = AddressFamily::DualStack,
                               SocketOptions options = {}) noexcept;
        /// @brief Binds the listener socket to a local endpoint.
        NetExpected<void> Bind(Endpoint localEndpoint) noexcept;
        /// @brief Begins listening with the requested pending-connection backlog.
        NetExpected<void> Listen(NGIN::Int32 backlog = 128) noexcept;
        /// @brief Attempts to accept one connection without blocking.
        NetExpected<TcpSocket> TryAccept() noexcept;

        /// @brief Asynchronously accepts one connection using driver readiness and cancellation.
        NGIN::Async::Task<TcpSocket, NetError> AcceptAsync(NGIN::Async::TaskContext&      ctx,
                                                           NetworkDriver&                 driver,
                                                           NGIN::Async::CancellationToken token);

        /// @brief Closes the listener; calling Close() repeatedly is safe.
        void Close() noexcept;

        /// @brief Returns mutable access to the owned native-handle wrapper.
        [[nodiscard]] SocketHandle& Handle() noexcept { return m_handle; }
        /// @brief Returns the owned native-handle wrapper.
        [[nodiscard]] const SocketHandle& Handle() const noexcept { return m_handle; }

    private:
        SocketHandle m_handle {};
    };
}// namespace NGIN::Net
