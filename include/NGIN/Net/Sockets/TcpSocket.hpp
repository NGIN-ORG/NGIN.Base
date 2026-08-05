/// @file TcpSocket.hpp
/// @brief TCP socket wrapper.
#pragma once

#include <NGIN/Net/Sockets/SocketHandle.hpp>
#include <NGIN/Net/Types/AddressFamily.hpp>
#include <NGIN/Net/Types/Buffer.hpp>
#include <NGIN/Net/Types/Endpoint.hpp>
#include <NGIN/Net/Types/NetError.hpp>
#include <NGIN/Net/Types/ShutdownMode.hpp>

#include <NGIN/Net/Types/SocketOptions.hpp>
#include <utility>

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

    /// @brief TCP socket with non-blocking Try* operations.
    class NGIN_NET_API TcpSocket final
    {
    public:
        /// @brief Constructs a closed TCP socket.
        TcpSocket() noexcept = default;

        /// @brief TCP sockets are non-copyable because they uniquely own a native socket.
        TcpSocket(const TcpSocket&) = delete;
        /// @brief TCP sockets are non-copy-assignable because they uniquely own a native socket.
        TcpSocket& operator=(const TcpSocket&) = delete;
        /// @brief Transfers native socket ownership from another socket.
        TcpSocket(TcpSocket&&) noexcept = default;
        /// @brief Transfers native socket ownership from another socket.
        TcpSocket& operator=(TcpSocket&&) noexcept = default;

        /// @brief Creates a non-blocking TCP socket with the requested family and options.
        NetExpected<void> Open(AddressFamily family  = AddressFamily::DualStack,
                               SocketOptions options = {}) noexcept;

        /// @brief Attempts a non-blocking connection.
        /// @return True when connected, false while connection remains in progress.
        NetExpected<bool> TryConnect(Endpoint remoteEndpoint) noexcept;
        /// @brief Asynchronously connects using driver readiness and cancellation.
        NGIN::Async::Task<void, NetError> ConnectAsync(NGIN::Async::TaskContext&      ctx,
                                                       NetworkDriver&                 driver,
                                                       Endpoint                       remoteEndpoint,
                                                       NGIN::Async::CancellationToken token);

        /// @brief Connects synchronously to a remote endpoint.
        NetExpected<void> Connect(Endpoint remoteEndpoint);

        /// @brief Attempts to send bytes without blocking.
        NetExpected<NGIN::UInt32> TrySend(ConstByteSpan data) noexcept;
        /// @brief Attempts to receive bytes without blocking.
        NetExpected<NGIN::UInt32> TryReceive(ByteSpan destination) noexcept;
        /// @brief Attempts a scatter/gather send without blocking.
        NetExpected<NGIN::UInt32> TrySendSegments(BufferSegmentSpan data) noexcept;
        /// @brief Attempts a scatter/gather receive without blocking.
        NetExpected<NGIN::UInt32> TryReceiveSegments(MutableBufferSegmentSpan destination) noexcept;

        /// @brief Asynchronously sends bytes, retrying readiness until progress or cancellation.
        NGIN::Async::Task<NGIN::UInt32, NetError> SendAsync(NGIN::Async::TaskContext&      ctx,
                                                            NetworkDriver&                 driver,
                                                            ConstByteSpan                  data,
                                                            NGIN::Async::CancellationToken token);
        /// @brief Asynchronously receives bytes, waiting for readiness as required.
        NGIN::Async::Task<NGIN::UInt32, NetError> ReceiveAsync(NGIN::Async::TaskContext&      ctx,
                                                               NetworkDriver&                 driver,
                                                               ByteSpan                       destination,
                                                               NGIN::Async::CancellationToken token);

        /// @brief Disables reads, writes, or both directions on the connected socket.
        NetExpected<void> Shutdown(ShutdownMode mode) noexcept;
        /// @brief Closes the socket; calling Close() repeatedly is safe.
        void Close() noexcept;

        /// @brief Returns mutable access to the owned native-handle wrapper.
        [[nodiscard]] SocketHandle& Handle() noexcept { return m_handle; }
        /// @brief Returns the owned native-handle wrapper.
        [[nodiscard]] const SocketHandle& Handle() const noexcept { return m_handle; }

    private:
        explicit TcpSocket(SocketHandle&& handle, bool nonBlocking) noexcept
            : m_handle(std::move(handle)), m_nonBlocking(nonBlocking)
        {
        }

        friend class TcpListener;

        SocketHandle m_handle {};
        bool         m_nonBlocking {true};
    };
}// namespace NGIN::Net
