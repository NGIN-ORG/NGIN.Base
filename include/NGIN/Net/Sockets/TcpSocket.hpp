/// @file TcpSocket.hpp
/// @brief TCP socket wrapper.
#pragma once

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/IO/Runtime.hpp>
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

    /// @brief TCP socket with non-blocking Try* operations.
    /// @note Async operations require a bound, non-stopped runtime; otherwise they fault with InvalidTaskUsage.
    /// Their optional token is linked with TaskContext cancellation. Async tasks capture
    /// stable socket state when created; later moves preserve pending operations. Close or
    /// destruction cancels pending operations and defers native release until backend access
    /// ends. Keep buffers and contexts alive until completion. Async use requires
    /// nonblocking sockets. Synchronize moves and synchronous methods with other member calls.
    /// One async read and one async write may overlap; same-direction overlap reports
    /// OperationInProgress. A TCP connect reserves both directions until completion;
    /// canceling an in-flight connect closes the socket.
    class NGIN_NET_API TcpSocket final
    {
    public:
        /// @brief Constructs a closed TCP socket.
        TcpSocket() noexcept = default;
        /// @brief Binds asynchronous operations to a borrowed I/O runtime without starting workers.
        /// @note The runtime must outlive this socket and its operations. Accepted sockets inherit it.
        explicit TcpSocket(NGIN::IO::Runtime& runtime) noexcept : m_runtime(&runtime) {}
        /// @brief Returns the borrowed runtime, or null for an unbound synchronous socket.
        [[nodiscard]] NGIN::IO::Runtime* GetRuntime() const noexcept { return m_runtime; }

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
        /// @brief Asynchronously connects using the bound runtime and context cancellation.
        NGIN::Async::Task<void, NetError> ConnectAsync(NGIN::Async::TaskContext&      ctx,
                                                       Endpoint                       remoteEndpoint,
                                                       NGIN::Async::CancellationToken token = {});

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
                                                            ConstByteSpan                  data,
                                                            NGIN::Async::CancellationToken token = {});
        /// @brief Asynchronously receives bytes, waiting for readiness as required.
        NGIN::Async::Task<NGIN::UInt32, NetError> ReceiveAsync(NGIN::Async::TaskContext&      ctx,
                                                               ByteSpan                       destination,
                                                               NGIN::Async::CancellationToken token = {});

        /// @brief Disables reads, writes, or both directions on the connected socket.
        NetExpected<void> Shutdown(ShutdownMode mode) noexcept;
        /// @brief Closes the socket; calling Close() repeatedly is safe.
        void Close() noexcept;

        /// @brief Returns mutable access to the owned native-handle wrapper.
        [[nodiscard]] SocketHandle& Handle() noexcept { return m_handle; }
        /// @brief Returns the owned native-handle wrapper.
        [[nodiscard]] const SocketHandle& Handle() const noexcept { return m_handle; }

    private:
        explicit TcpSocket(SocketHandle&& handle, bool nonBlocking, NGIN::IO::Runtime* runtime) noexcept
            : m_runtime(runtime), m_handle(std::move(handle)), m_nonBlocking(nonBlocking)
        {
        }

        friend class TcpListener;
        friend class NetworkDriver;

        NGIN::IO::Runtime* m_runtime {nullptr};
        SocketHandle       m_handle {};
        bool               m_nonBlocking {true};
    };
}// namespace NGIN::Net
