/// @file NetworkDriver.hpp
/// @brief Async I/O driver for socket readiness.
#pragma once

#include <memory>

#if defined(NGIN_PLATFORM_WINDOWS)
  #include <NGIN/Net/Types/Buffer.hpp>
  #include <NGIN/Net/Types/Endpoint.hpp>
#endif

#include <NGIN/Primitives.hpp>
#include <NGIN/Units.hpp>

namespace NGIN::Async
{
    class TaskContext;
    class CancellationToken;
    template<typename T>
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
        NGIN::UInt32                workerThreads {0};
        bool                        busyPoll {false};
        NGIN::Units::Milliseconds   pollInterval {1.0};
    };

    /// @brief Explicit async runtime for socket readiness.
    class NetworkDriver final
    {
    public:
        NetworkDriver(const NetworkDriver&)            = delete;
        NetworkDriver& operator=(const NetworkDriver&) = delete;
        NetworkDriver(NetworkDriver&&)                 = delete;
        NetworkDriver& operator=(NetworkDriver&&)      = delete;

        ~NetworkDriver();

        static std::unique_ptr<NetworkDriver> Create(NetworkDriverOptions options);

        void Run();
        void PollOnce();
        void Stop();

        NGIN::Async::Task<void> WaitUntilReadable(NGIN::Async::TaskContext& ctx,
                                                  SocketHandle& handle,
                                                  NGIN::Async::CancellationToken token);
        NGIN::Async::Task<void> WaitUntilWritable(NGIN::Async::TaskContext& ctx,
                                                  SocketHandle& handle,
                                                  NGIN::Async::CancellationToken token);

    private:
        NetworkDriver();

#if defined(NGIN_PLATFORM_WINDOWS)
        friend class TcpSocket;
        friend class UdpSocket;
        friend class TcpListener;

        NGIN::Async::Task<NGIN::UInt32> SubmitSend(NGIN::Async::TaskContext& ctx,
                                                   SocketHandle& handle,
                                                   ConstByteSpan data,
                                                   NGIN::Async::CancellationToken token);
        NGIN::Async::Task<NGIN::UInt32> SubmitReceive(NGIN::Async::TaskContext& ctx,
                                                      SocketHandle& handle,
                                                      ByteSpan destination,
                                                      NGIN::Async::CancellationToken token);
        NGIN::Async::Task<NGIN::UInt32> SubmitSendTo(NGIN::Async::TaskContext& ctx,
                                                     SocketHandle& handle,
                                                     Endpoint remoteEndpoint,
                                                     ConstByteSpan data,
                                                     NGIN::Async::CancellationToken token);
        NGIN::Async::Task<DatagramReceiveResult> SubmitReceiveFrom(NGIN::Async::TaskContext& ctx,
                                                                   SocketHandle& handle,
                                                                   ByteSpan destination,
                                                                   NGIN::Async::CancellationToken token);
        NGIN::Async::Task<void> SubmitConnect(NGIN::Async::TaskContext& ctx,
                                              SocketHandle& handle,
                                              Endpoint remoteEndpoint,
                                              NGIN::Async::CancellationToken token);
        NGIN::Async::Task<SocketHandle> SubmitAccept(NGIN::Async::TaskContext& ctx,
                                                     SocketHandle& handle,
                                                     NGIN::Async::CancellationToken token);
#endif

        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}// namespace NGIN::Net
