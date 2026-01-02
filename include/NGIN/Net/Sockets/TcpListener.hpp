/// @file TcpListener.hpp
/// @brief TCP listener socket wrapper.
#pragma once

#include <NGIN/Net/Sockets/SocketHandle.hpp>
#include <NGIN/Net/Sockets/TcpSocket.hpp>
#include <NGIN/Net/Types/AddressFamily.hpp>
#include <NGIN/Net/Types/Endpoint.hpp>
#include <NGIN/Net/Types/NetError.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Async
{
    class TaskContext;
    class CancellationToken;
    template<typename T>
    class Task;
}// namespace NGIN::Async

namespace NGIN::Net
{
    class NetworkDriver;

    /// @brief TCP listen socket with non-blocking accept.
    class TcpListener final
    {
    public:
        NetExpected<void> Open(AddressFamily family = AddressFamily::DualStack) noexcept;
        NetExpected<void> Bind(Endpoint localEndpoint) noexcept;
        NetExpected<void> Listen(NGIN::Int32 backlog = 128) noexcept;
        NetExpected<TcpSocket> TryAccept() noexcept;

        NGIN::Async::Task<TcpSocket> AcceptAsync(NGIN::Async::TaskContext& ctx,
                                                 NetworkDriver& driver,
                                                 NGIN::Async::CancellationToken token);

        void Close() noexcept;

        [[nodiscard]] SocketHandle& Handle() noexcept { return m_handle; }
        [[nodiscard]] const SocketHandle& Handle() const noexcept { return m_handle; }

    private:
        SocketHandle m_handle {};
    };
}// namespace NGIN::Net
