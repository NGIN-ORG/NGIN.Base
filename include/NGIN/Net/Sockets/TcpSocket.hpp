/// @file TcpSocket.hpp
/// @brief TCP socket wrapper.
#pragma once

#include <NGIN/Net/Sockets/SocketHandle.hpp>
#include <NGIN/Net/Types/AddressFamily.hpp>
#include <NGIN/Net/Types/Buffer.hpp>
#include <NGIN/Net/Types/Endpoint.hpp>
#include <NGIN/Net/Types/NetError.hpp>
#include <NGIN/Net/Types/ShutdownMode.hpp>

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

    /// @brief TCP socket with non-blocking Try* operations.
    class TcpSocket final
    {
    public:
        TcpSocket() noexcept = default;

        NetExpected<void> Open(AddressFamily family = AddressFamily::DualStack) noexcept;

        NetExpected<bool> TryConnect(Endpoint remoteEndpoint) noexcept;
        NGIN::Async::Task<void> ConnectAsync(NGIN::Async::TaskContext& ctx,
                                             NetworkDriver& driver,
                                             Endpoint remoteEndpoint,
                                             NGIN::Async::CancellationToken token);

        NetExpected<void> ConnectBlocking(Endpoint remoteEndpoint);

        NetExpected<NGIN::UInt32> TrySend(ConstByteSpan data) noexcept;
        NetExpected<NGIN::UInt32> TryReceive(ByteSpan destination) noexcept;
        NetExpected<NGIN::UInt32> TrySendSegments(BufferSegmentSpan data) noexcept;
        NetExpected<NGIN::UInt32> TryReceiveSegments(MutableBufferSegmentSpan destination) noexcept;

        NGIN::Async::Task<NGIN::UInt32> SendAsync(NGIN::Async::TaskContext& ctx,
                                                  NetworkDriver& driver,
                                                  ConstByteSpan data,
                                                  NGIN::Async::CancellationToken token);
        NGIN::Async::Task<NGIN::UInt32> ReceiveAsync(NGIN::Async::TaskContext& ctx,
                                                     NetworkDriver& driver,
                                                     ByteSpan destination,
                                                     NGIN::Async::CancellationToken token);

        NetExpected<void> Shutdown(ShutdownMode mode) noexcept;
        void Close() noexcept;

        [[nodiscard]] SocketHandle& Handle() noexcept { return m_handle; }
        [[nodiscard]] const SocketHandle& Handle() const noexcept { return m_handle; }

    private:
        explicit TcpSocket(SocketHandle handle) noexcept
            : m_handle(handle)
        {
        }

        friend class TcpListener;

        SocketHandle m_handle {};
    };
}// namespace NGIN::Net
