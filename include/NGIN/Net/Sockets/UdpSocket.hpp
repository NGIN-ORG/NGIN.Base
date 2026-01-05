/// @file UdpSocket.hpp
/// @brief UDP socket wrapper.
#pragma once

#include <NGIN/Net/Sockets/SocketHandle.hpp>
#include <NGIN/Net/Types/AddressFamily.hpp>
#include <NGIN/Net/Types/Buffer.hpp>
#include <NGIN/Net/Types/Endpoint.hpp>
#include <NGIN/Net/Types/NetError.hpp>
#include <NGIN/Net/Types/SocketOptions.hpp>

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

    /// @brief Result for UDP receive operations.
    struct DatagramReceiveResult final
    {
        Endpoint     remoteEndpoint {};
        NGIN::UInt32 bytesReceived {0};
    };

    /// @brief UDP socket with non-blocking Try* operations.
    class UdpSocket final
    {
    public:
        UdpSocket() noexcept = default;
        UdpSocket(const UdpSocket&)            = delete;
        UdpSocket& operator=(const UdpSocket&) = delete;
        UdpSocket(UdpSocket&&) noexcept        = default;
        UdpSocket& operator=(UdpSocket&&) noexcept = default;

        NetExpected<void> Open(AddressFamily family = AddressFamily::DualStack,
                               SocketOptions options = {}) noexcept;
        NetExpected<void> Bind(Endpoint localEndpoint) noexcept;
        NetExpected<void> Connect(Endpoint remoteEndpoint) noexcept;
        void Close() noexcept;

        NetExpected<NGIN::UInt32> TrySendTo(Endpoint remoteEndpoint, ConstByteSpan payload) noexcept;
        NetExpected<DatagramReceiveResult> TryReceiveFrom(ByteSpan destination) noexcept;
        NetExpected<NGIN::UInt32> TrySendToSegments(Endpoint remoteEndpoint, BufferSegmentSpan payload) noexcept;
        NetExpected<DatagramReceiveResult> TryReceiveFromSegments(MutableBufferSegmentSpan destination) noexcept;

        NGIN::Async::Task<NGIN::UInt32> SendToAsync(NGIN::Async::TaskContext& ctx,
                                                   NetworkDriver& driver,
                                                   Endpoint remoteEndpoint,
                                                   ConstByteSpan payload,
                                                   NGIN::Async::CancellationToken token);
        NGIN::Async::Task<DatagramReceiveResult> ReceiveFromAsync(NGIN::Async::TaskContext& ctx,
                                                                  NetworkDriver& driver,
                                                                  ByteSpan destination,
                                                                  NGIN::Async::CancellationToken token);

        [[nodiscard]] SocketHandle& Handle() noexcept { return m_handle; }
        [[nodiscard]] const SocketHandle& Handle() const noexcept { return m_handle; }

    private:
        SocketHandle m_handle {};
    };
}// namespace NGIN::Net
