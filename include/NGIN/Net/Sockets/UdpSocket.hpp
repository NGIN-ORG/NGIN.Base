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
    template<typename T, typename E>
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
    class NGIN_NET_API UdpSocket final
    {
    public:
        /// @brief Constructs a closed UDP socket.
        UdpSocket() noexcept = default;
        /// @brief UDP sockets are non-copyable because they uniquely own a native socket.
        UdpSocket(const UdpSocket&) = delete;
        /// @brief UDP sockets are non-copy-assignable because they uniquely own a native socket.
        UdpSocket& operator=(const UdpSocket&) = delete;
        /// @brief Transfers native socket ownership from another socket.
        UdpSocket(UdpSocket&&) noexcept = default;
        /// @brief Transfers native socket ownership from another socket.
        UdpSocket& operator=(UdpSocket&&) noexcept = default;

        /// @brief Creates a non-blocking UDP socket with the requested family and options.
        NetExpected<void> Open(AddressFamily family  = AddressFamily::DualStack,
                               SocketOptions options = {}) noexcept;
        /// @brief Binds the socket to a local endpoint.
        NetExpected<void> Bind(Endpoint localEndpoint) noexcept;
        /// @brief Sets the default peer for connected datagram operations.
        NetExpected<void> Connect(Endpoint remoteEndpoint) noexcept;
        /// @brief Closes the socket; calling Close() repeatedly is safe.
        void Close() noexcept;

        /// @brief Attempts to send one datagram without blocking.
        NetExpected<NGIN::UInt32> TrySendTo(Endpoint remoteEndpoint, ConstByteSpan payload) noexcept;
        /// @brief Attempts to receive one datagram without blocking.
        NetExpected<DatagramReceiveResult> TryReceiveFrom(ByteSpan destination) noexcept;
        /// @brief Attempts to send one scatter/gather datagram without blocking.
        NetExpected<NGIN::UInt32> TrySendToSegments(Endpoint remoteEndpoint, BufferSegmentSpan payload) noexcept;
        /// @brief Attempts to receive one datagram into scatter/gather buffers without blocking.
        NetExpected<DatagramReceiveResult> TryReceiveFromSegments(MutableBufferSegmentSpan destination) noexcept;

        /// @brief Asynchronously sends one datagram using driver readiness and cancellation.
        NGIN::Async::Task<NGIN::UInt32, NetError> SendToAsync(NGIN::Async::TaskContext&      ctx,
                                                              NetworkDriver&                 driver,
                                                              Endpoint                       remoteEndpoint,
                                                              ConstByteSpan                  payload,
                                                              NGIN::Async::CancellationToken token);
        /// @brief Asynchronously receives one datagram using driver readiness and cancellation.
        NGIN::Async::Task<DatagramReceiveResult, NetError> ReceiveFromAsync(NGIN::Async::TaskContext&      ctx,
                                                                            NetworkDriver&                 driver,
                                                                            ByteSpan                       destination,
                                                                            NGIN::Async::CancellationToken token);

        /// @brief Returns mutable access to the owned native-handle wrapper.
        [[nodiscard]] SocketHandle& Handle() noexcept { return m_handle; }
        /// @brief Returns the owned native-handle wrapper.
        [[nodiscard]] const SocketHandle& Handle() const noexcept { return m_handle; }

    private:
        SocketHandle m_handle {};
    };
}// namespace NGIN::Net
