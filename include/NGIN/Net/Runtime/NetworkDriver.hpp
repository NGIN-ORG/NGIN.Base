/// @file NetworkDriver.hpp
/// @brief Async I/O driver for socket readiness.
#pragma once

#include <memory>

#if defined(NGIN_PLATFORM_WINDOWS)
#include <NGIN/Net/Types/Buffer.hpp>
#include <NGIN/Net/Types/Endpoint.hpp>
#endif

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
    class TcpSocket;
    class TcpListener;
    class UdpSocket;
    class SocketHandle;
    struct DatagramReceiveResult;

    /// @brief Network driver configuration.
    struct NetworkDriverOptions final
    {
        NGIN::UInt32              workerThreads {0};
        bool                      busyPoll {false};
        NGIN::Units::Milliseconds pollInterval {1.0};
    };

    /// @brief Explicit async runtime for socket readiness.
    class NGIN_NET_API NetworkDriver final
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
        ~NetworkDriver();

        /// @brief Creates a network driver using the requested worker and polling policy.
        static std::unique_ptr<NetworkDriver> Create(NetworkDriverOptions options);

        /// @brief Runs the driver loop until Stop() is requested.
        void Run();
        /// @brief Performs one non-blocking or configured-interval poll cycle.
        void PollOnce();
        /// @brief Requests termination of a running driver loop.
        void Stop();

        /// @brief Asynchronously waits until a socket can be read or cancellation occurs.
        NGIN::Async::Task<void, NetError> WaitUntilReadable(NGIN::Async::TaskContext&      ctx,
                                                            SocketHandle&                  handle,
                                                            NGIN::Async::CancellationToken token);
        /// @brief Asynchronously waits until a socket can be written or cancellation occurs.
        NGIN::Async::Task<void, NetError> WaitUntilWritable(NGIN::Async::TaskContext&      ctx,
                                                            SocketHandle&                  handle,
                                                            NGIN::Async::CancellationToken token);

    private:
        NetworkDriver();

#if defined(NGIN_PLATFORM_WINDOWS)
        friend class TcpSocket;
        friend class UdpSocket;
        friend class TcpListener;

        NGIN::Async::Task<NGIN::UInt32, NetError>          SubmitSend(NGIN::Async::TaskContext&      ctx,
                                                                      SocketHandle&                  handle,
                                                                      ConstByteSpan                  data,
                                                                      NGIN::Async::CancellationToken token);
        NGIN::Async::Task<NGIN::UInt32, NetError>          SubmitReceive(NGIN::Async::TaskContext&      ctx,
                                                                         SocketHandle&                  handle,
                                                                         ByteSpan                       destination,
                                                                         NGIN::Async::CancellationToken token);
        NGIN::Async::Task<NGIN::UInt32, NetError>          SubmitSendTo(NGIN::Async::TaskContext&      ctx,
                                                                        SocketHandle&                  handle,
                                                                        Endpoint                       remoteEndpoint,
                                                                        ConstByteSpan                  data,
                                                                        NGIN::Async::CancellationToken token);
        NGIN::Async::Task<DatagramReceiveResult, NetError> SubmitReceiveFrom(NGIN::Async::TaskContext&      ctx,
                                                                             SocketHandle&                  handle,
                                                                             ByteSpan                       destination,
                                                                             NGIN::Async::CancellationToken token);
        NGIN::Async::Task<void, NetError>                  SubmitConnect(NGIN::Async::TaskContext&      ctx,
                                                                         SocketHandle&                  handle,
                                                                         Endpoint                       remoteEndpoint,
                                                                         NGIN::Async::CancellationToken token);
        NGIN::Async::Task<SocketHandle, NetError>          SubmitAccept(NGIN::Async::TaskContext&      ctx,
                                                                        SocketHandle&                  handle,
                                                                        NGIN::Async::CancellationToken token);
#endif

        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}// namespace NGIN::Net
