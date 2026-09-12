/// @file NetworkDriver.hpp
/// @brief Async I/O driver for socket readiness.
#pragma once

#include "../IO/RuntimeBackend.hpp"
#include <memory>

#include <NGIN/Net/Types/Buffer.hpp>
#include <NGIN/Net/Types/Endpoint.hpp>

#include <NGIN/Defines.hpp>
#include <NGIN/Net/Types/NetError.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Units.hpp>

namespace NGIN::Async
{
    class TaskContext;
    class CancellationToken;
    template<typename T, typename E>
    class Task;
}// namespace NGIN::Async

namespace NGIN::Net
{
    namespace detail
    {
        class SocketState;
    }
    std::shared_ptr<class NetworkDriver> AcquireNetworkDriver(NGIN::IO::Runtime& runtime);
    class TcpSocket;
    class TcpListener;
    class UdpSocket;
    class SocketHandle;
    struct DatagramReceiveResult;

    /// @brief Explicit async runtime for socket readiness.
    class NetworkDriver final : public NGIN::IO::detail::RuntimeService
    {
    public:
        /// @brief Drivers are non-copyable because they own runtime and platform state.
        NetworkDriver(const NetworkDriver&) = delete;
        /// @brief Drivers are non-copy-assignable because they own runtime and platform state.
        NetworkDriver& operator=(const NetworkDriver&) = delete;
        /// @brief Drivers are immovable because registered operations retain their address.
        NetworkDriver(NetworkDriver&&) = delete;
        /// @brief Drivers are non-move-assignable because registered operations retain their address.
        NetworkDriver& operator=(NetworkDriver&&) = delete;

        /// @brief Stops the driver and releases all platform resources.
        ~NetworkDriver() override;

        explicit NetworkDriver(NGIN::IO::Runtime& runtime);

        /// @brief Requests cancellation on the runtime's loop; creates no driver thread.
        void Stop() noexcept override;

    private:
        friend class TcpSocket;
        friend class UdpSocket;
        friend class TcpListener;

        static NGIN::Async::Task<NGIN::UInt32, NetError> SubmitSend(
                NGIN::Async::TaskContext& ctx, NGIN::IO::Runtime* runtime,
                std::shared_ptr<detail::SocketState> socket, ConstByteSpan data,
                NGIN::Async::CancellationToken token);
        static NGIN::Async::Task<NGIN::UInt32, NetError> SubmitReceive(
                NGIN::Async::TaskContext& ctx, NGIN::IO::Runtime* runtime,
                std::shared_ptr<detail::SocketState> socket, ByteSpan destination,
                NGIN::Async::CancellationToken token);
        static NGIN::Async::Task<NGIN::UInt32, NetError> SubmitSendTo(
                NGIN::Async::TaskContext& ctx, NGIN::IO::Runtime* runtime,
                std::shared_ptr<detail::SocketState> socket, Endpoint remoteEndpoint, ConstByteSpan data,
                NGIN::Async::CancellationToken token);
        static NGIN::Async::Task<DatagramReceiveResult, NetError> SubmitReceiveFrom(
                NGIN::Async::TaskContext& ctx, NGIN::IO::Runtime* runtime,
                std::shared_ptr<detail::SocketState> socket, ByteSpan destination,
                NGIN::Async::CancellationToken token);
        static NGIN::Async::Task<void, NetError> SubmitConnect(
                NGIN::Async::TaskContext& ctx, NGIN::IO::Runtime* runtime,
                std::shared_ptr<detail::SocketState> socket, Endpoint remoteEndpoint,
                NGIN::Async::CancellationToken token);
        static NGIN::Async::Task<TcpSocket, NetError> SubmitAccept(
                NGIN::Async::TaskContext& ctx, NGIN::IO::Runtime* runtime,
                std::shared_ptr<detail::SocketState> socket, NGIN::Async::CancellationToken token);

        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}// namespace NGIN::Net
