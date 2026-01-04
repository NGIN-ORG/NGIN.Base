#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <vector>

#if defined(NGIN_PLATFORM_WINDOWS)
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
#endif

#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ThisThread.hpp>
#include <NGIN/Net/Runtime/NetworkDriver.hpp>
#include <NGIN/Net/Sockets/TcpListener.hpp>
#include <NGIN/Net/Sockets/UdpSocket.hpp>
#include <NGIN/Net/Types/BufferPool.hpp>
#include <NGIN/Net/Types/Endpoint.hpp>
#include <NGIN/Net/Types/IpAddress.hpp>
#include <NGIN/Net/Types/NetError.hpp>
#include <NGIN/Units.hpp>

namespace NGIN::Net
{
    namespace
    {
        NGIN::UInt16 GetBoundPort(const SocketHandle& handle)
        {
            sockaddr_storage storage {};
#if defined(NGIN_PLATFORM_WINDOWS)
            int length = static_cast<int>(sizeof(storage));
            const auto socket = static_cast<SOCKET>(handle.Native());
            if (::getsockname(socket, reinterpret_cast<sockaddr*>(&storage), &length) != 0)
            {
                return 0;
            }
#else
            socklen_t length = static_cast<socklen_t>(sizeof(storage));
            const auto socket = static_cast<int>(handle.Native());
            if (::getsockname(socket, reinterpret_cast<sockaddr*>(&storage), &length) != 0)
            {
                return 0;
            }
#endif

            if (storage.ss_family == AF_INET)
            {
                const auto* addr = reinterpret_cast<const sockaddr_in*>(&storage);
                return static_cast<NGIN::UInt16>(ntohs(addr->sin_port));
            }
            if (storage.ss_family == AF_INET6)
            {
                const auto* addr = reinterpret_cast<const sockaddr_in6*>(&storage);
                return static_cast<NGIN::UInt16>(ntohs(addr->sin6_port));
            }

            return 0;
        }

        void SleepBrief() noexcept
        {
            NGIN::Execution::ThisThread::SleepFor(NGIN::Units::Milliseconds(1.0));
        }

        template<typename Predicate>
        bool PumpUntil(NGIN::Execution::CooperativeScheduler& scheduler,
                       NGIN::Net::NetworkDriver& driver,
                       Predicate&& predicate)
        {
            for (int attempt = 0; attempt < 512; ++attempt)
            {
                driver.PollOnce();
                scheduler.RunUntilIdle();
                if (predicate())
                {
                    return true;
                }
                SleepBrief();
            }
            return false;
        }
    }

    TEST_CASE("Net.Udp.TryReceiveWouldBlock")
    {
        UdpSocket socket;
        auto      openResult = socket.Open(AddressFamily::V4);
        REQUIRE(openResult);

        Endpoint local {IpAddress::AnyV4(), 0};
        auto     bindResult = socket.Bind(local);
        REQUIRE(bindResult);

        std::array<NGIN::Byte, 256> storage {};
        auto                        recvResult = socket.TryReceiveFrom(ByteSpan {storage.data(), storage.size()});
        REQUIRE_FALSE(recvResult);
        REQUIRE(recvResult.error().code == NetErrc::WouldBlock);

        socket.Close();
    }

    TEST_CASE("Net.TcpListener.TryAcceptWouldBlock")
    {
        TcpListener listener;
        auto        openResult = listener.Open(AddressFamily::V4);
        REQUIRE(openResult);

        Endpoint local {IpAddress::AnyV4(), 0};
        auto     bindResult = listener.Bind(local);
        REQUIRE(bindResult);

        auto listenResult = listener.Listen(16);
        REQUIRE(listenResult);

        auto acceptResult = listener.TryAccept();
        REQUIRE_FALSE(acceptResult);
        REQUIRE(acceptResult.error().code == NetErrc::WouldBlock);

        listener.Close();
    }

    TEST_CASE("Net.BufferPool.RentRelease")
    {
        BufferPool<> pool;
        auto         buffer = pool.Rent(128);
        REQUIRE(buffer.IsValid());
        REQUIRE(buffer.capacity >= 128);
    }

    TEST_CASE("Net.Udp.LoopbackSendReceive")
    {
        UdpSocket receiver;
        REQUIRE(receiver.Open(AddressFamily::V4));
        REQUIRE(receiver.Bind({IpAddress::AnyV4(), 0}));

        const auto port = GetBoundPort(receiver.Handle());
        REQUIRE(port != 0);

        UdpSocket sender;
        REQUIRE(sender.Open(AddressFamily::V4));

        const char payload[] = "udp-ping";
        std::array<NGIN::Byte, 64> recvBuffer {};

        const auto sendResult = sender.TrySendTo({IpAddress::LoopbackV4(), port},
                                                 ConstByteSpan {reinterpret_cast<const NGIN::Byte*>(payload),
                                                               sizeof(payload)});
        REQUIRE(sendResult);

        bool received = false;
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            auto recvResult = receiver.TryReceiveFrom(ByteSpan {recvBuffer.data(), recvBuffer.size()});
            if (recvResult)
            {
                const auto bytes = recvResult->bytesReceived;
                REQUIRE(bytes == sizeof(payload));
                REQUIRE(std::memcmp(recvBuffer.data(), payload, sizeof(payload)) == 0);
                received = true;
                break;
            }
            REQUIRE(recvResult.error().code == NetErrc::WouldBlock);
            SleepBrief();
        }

        REQUIRE(received);

        sender.Close();
        receiver.Close();
    }

    TEST_CASE("Net.Tcp.LoopbackConnectSendReceive")
    {
        TcpListener listener;
        REQUIRE(listener.Open(AddressFamily::V4));
        REQUIRE(listener.Bind({IpAddress::AnyV4(), 0}));
        REQUIRE(listener.Listen(16));

        const auto port = GetBoundPort(listener.Handle());
        REQUIRE(port != 0);

        TcpSocket client;
        REQUIRE(client.Open(AddressFamily::V4));
        REQUIRE(client.ConnectBlocking({IpAddress::LoopbackV4(), port}));

        TcpSocket server;
        bool accepted = false;
        for (int attempt = 0; attempt < 128; ++attempt)
        {
            auto acceptResult = listener.TryAccept();
            if (acceptResult)
            {
                server = std::move(*acceptResult);
                accepted = true;
                break;
            }
            REQUIRE(acceptResult.error().code == NetErrc::WouldBlock);
            SleepBrief();
        }
        REQUIRE(accepted);

        const char payload[] = "tcp-ping";
        const auto sendResult = client.TrySend(ConstByteSpan {reinterpret_cast<const NGIN::Byte*>(payload),
                                                              sizeof(payload)});
        REQUIRE(sendResult);

        std::array<NGIN::Byte, 64> recvBuffer {};
        bool received = false;
        for (int attempt = 0; attempt < 128; ++attempt)
        {
            auto recvResult = server.TryReceive(ByteSpan {recvBuffer.data(), recvBuffer.size()});
            if (recvResult)
            {
                REQUIRE(*recvResult == sizeof(payload));
                REQUIRE(std::memcmp(recvBuffer.data(), payload, sizeof(payload)) == 0);
                received = true;
                break;
            }
            REQUIRE(recvResult.error().code == NetErrc::WouldBlock);
            SleepBrief();
        }
        REQUIRE(received);

        client.Close();
        server.Close();
        listener.Close();
    }

    TEST_CASE("Net.Udp.AsyncLoopbackSendReceive")
    {
        NGIN::Execution::CooperativeScheduler scheduler;
        NGIN::Async::TaskContext              ctx(scheduler);
        auto                                  driver = NetworkDriver::Create({.workerThreads = 0});

        UdpSocket receiver;
        REQUIRE(receiver.Open(AddressFamily::V4));
        REQUIRE(receiver.Bind({IpAddress::AnyV4(), 0}));

        const auto port = GetBoundPort(receiver.Handle());
        REQUIRE(port != 0);

        UdpSocket sender;
        REQUIRE(sender.Open(AddressFamily::V4));

        const char payload[] = "udp-async";
        std::array<NGIN::Byte, 64> recvBuffer {};

        auto recvTask = receiver.ReceiveFromAsync(ctx,
                                                  *driver,
                                                  ByteSpan {recvBuffer.data(), recvBuffer.size()},
                                                  ctx.GetCancellationToken());
        recvTask.Start(ctx);

        auto sendTask = sender.SendToAsync(ctx,
                                           *driver,
                                           {IpAddress::LoopbackV4(), port},
                                           ConstByteSpan {reinterpret_cast<const NGIN::Byte*>(payload),
                                                         sizeof(payload)},
                                           ctx.GetCancellationToken());
        sendTask.Start(ctx);

        REQUIRE(PumpUntil(scheduler, *driver, [&]() { return recvTask.IsCompleted() && sendTask.IsCompleted(); }));

        REQUIRE(sendTask.Get() == sizeof(payload));
        const auto result = recvTask.Get();
        REQUIRE(result.bytesReceived == sizeof(payload));
        REQUIRE(std::memcmp(recvBuffer.data(), payload, sizeof(payload)) == 0);

        sender.Close();
        receiver.Close();
    }

    TEST_CASE("Net.Tcp.AsyncLoopbackConnectSendReceive")
    {
        NGIN::Execution::CooperativeScheduler scheduler;
        NGIN::Async::TaskContext              ctx(scheduler);
        auto                                  driver = NetworkDriver::Create({.workerThreads = 0});

        TcpListener listener;
        REQUIRE(listener.Open(AddressFamily::V4));
        REQUIRE(listener.Bind({IpAddress::AnyV4(), 0}));
        REQUIRE(listener.Listen(16));

        const auto port = GetBoundPort(listener.Handle());
        REQUIRE(port != 0);

        TcpSocket client;
        REQUIRE(client.Open(AddressFamily::V4));

        auto acceptTask = listener.AcceptAsync(ctx, *driver, ctx.GetCancellationToken());
        acceptTask.Start(ctx);

        auto connectTask = client.ConnectAsync(ctx, *driver, {IpAddress::LoopbackV4(), port}, ctx.GetCancellationToken());
        connectTask.Start(ctx);

        REQUIRE(PumpUntil(scheduler, *driver, [&]() { return acceptTask.IsCompleted() && connectTask.IsCompleted(); }));

        connectTask.Get();
        TcpSocket server = acceptTask.Get();

        const char payload[] = "tcp-async";
        std::array<NGIN::Byte, 64> recvBuffer {};

        auto recvTask = server.ReceiveAsync(ctx,
                                            *driver,
                                            ByteSpan {recvBuffer.data(), recvBuffer.size()},
                                            ctx.GetCancellationToken());
        recvTask.Start(ctx);

        auto sendTask = client.SendAsync(ctx,
                                         *driver,
                                         ConstByteSpan {reinterpret_cast<const NGIN::Byte*>(payload),
                                                       sizeof(payload)},
                                         ctx.GetCancellationToken());
        sendTask.Start(ctx);

        REQUIRE(PumpUntil(scheduler, *driver, [&]() { return recvTask.IsCompleted() && sendTask.IsCompleted(); }));

        REQUIRE(sendTask.Get() == sizeof(payload));
        REQUIRE(recvTask.Get() == sizeof(payload));
        REQUIRE(std::memcmp(recvBuffer.data(), payload, sizeof(payload)) == 0);

        client.Close();
        server.Close();
        listener.Close();
    }

    TEST_CASE("Net.Udp.AsyncReceiveCancelled")
    {
        NGIN::Execution::CooperativeScheduler scheduler;
        NGIN::Async::TaskContext              ctx(scheduler);
        auto                                  driver = NetworkDriver::Create({.workerThreads = 0});

        UdpSocket socket;
        REQUIRE(socket.Open(AddressFamily::V4));
        REQUIRE(socket.Bind({IpAddress::AnyV4(), 0}));

        std::array<NGIN::Byte, 64> recvBuffer {};
        NGIN::Async::CancellationSource cancel;

        auto recvTask = socket.ReceiveFromAsync(ctx,
                                                *driver,
                                                ByteSpan {recvBuffer.data(), recvBuffer.size()},
                                                cancel.GetToken());
        recvTask.Start(ctx);

        driver->PollOnce();
        scheduler.RunUntilIdle();

        cancel.Cancel();

        REQUIRE(PumpUntil(scheduler, *driver, [&]() { return recvTask.IsCompleted(); }));
        REQUIRE_THROWS_AS(recvTask.Get(), NGIN::Async::TaskCanceled);

        socket.Close();
    }

    TEST_CASE("Net.Tcp.AsyncAcceptCancelled")
    {
        NGIN::Execution::CooperativeScheduler scheduler;
        NGIN::Async::TaskContext              ctx(scheduler);
        auto                                  driver = NetworkDriver::Create({.workerThreads = 0});

        TcpListener listener;
        REQUIRE(listener.Open(AddressFamily::V4));
        REQUIRE(listener.Bind({IpAddress::AnyV4(), 0}));
        REQUIRE(listener.Listen(8));

        NGIN::Async::CancellationSource cancel;
        auto acceptTask = listener.AcceptAsync(ctx, *driver, cancel.GetToken());
        acceptTask.Start(ctx);

        driver->PollOnce();
        scheduler.RunUntilIdle();

        cancel.Cancel();

        REQUIRE(PumpUntil(scheduler, *driver, [&]() { return acceptTask.IsCompleted(); }));
        REQUIRE_THROWS_AS(acceptTask.Get(), NGIN::Async::TaskCanceled);

        listener.Close();
    }

    TEST_CASE("Net.Tcp.AsyncReceiveCancelled")
    {
        NGIN::Execution::CooperativeScheduler scheduler;
        NGIN::Async::TaskContext              ctx(scheduler);
        auto                                  driver = NetworkDriver::Create({.workerThreads = 0});

        TcpListener listener;
        REQUIRE(listener.Open(AddressFamily::V4));
        REQUIRE(listener.Bind({IpAddress::AnyV4(), 0}));
        REQUIRE(listener.Listen(8));

        const auto port = GetBoundPort(listener.Handle());
        REQUIRE(port != 0);

        TcpSocket client;
        REQUIRE(client.Open(AddressFamily::V4));
        REQUIRE(client.ConnectBlocking({IpAddress::LoopbackV4(), port}));

        TcpSocket server;
        bool accepted = false;
        for (int attempt = 0; attempt < 128; ++attempt)
        {
            auto acceptResult = listener.TryAccept();
            if (acceptResult)
            {
                server = std::move(*acceptResult);
                accepted = true;
                break;
            }
            REQUIRE(acceptResult.error().code == NetErrc::WouldBlock);
            SleepBrief();
        }
        REQUIRE(accepted);

        std::array<NGIN::Byte, 64> recvBuffer {};
        NGIN::Async::CancellationSource cancel;

        auto recvTask = server.ReceiveAsync(ctx,
                                            *driver,
                                            ByteSpan {recvBuffer.data(), recvBuffer.size()},
                                            cancel.GetToken());
        recvTask.Start(ctx);

        driver->PollOnce();
        scheduler.RunUntilIdle();

        cancel.Cancel();

        REQUIRE(PumpUntil(scheduler, *driver, [&]() { return recvTask.IsCompleted(); }));
        REQUIRE_THROWS_AS(recvTask.Get(), NGIN::Async::TaskCanceled);

        client.Close();
        server.Close();
        listener.Close();
    }

    TEST_CASE("Net.Tcp.AsyncReceiveEOF")
    {
        NGIN::Execution::CooperativeScheduler scheduler;
        NGIN::Async::TaskContext              ctx(scheduler);
        auto                                  driver = NetworkDriver::Create({.workerThreads = 0});

        TcpListener listener;
        REQUIRE(listener.Open(AddressFamily::V4));
        REQUIRE(listener.Bind({IpAddress::AnyV4(), 0}));
        REQUIRE(listener.Listen(8));

        const auto port = GetBoundPort(listener.Handle());
        REQUIRE(port != 0);

        TcpSocket client;
        REQUIRE(client.Open(AddressFamily::V4));
        REQUIRE(client.ConnectBlocking({IpAddress::LoopbackV4(), port}));

        TcpSocket server;
        bool accepted = false;
        for (int attempt = 0; attempt < 128; ++attempt)
        {
            auto acceptResult = listener.TryAccept();
            if (acceptResult)
            {
                server = std::move(*acceptResult);
                accepted = true;
                break;
            }
            REQUIRE(acceptResult.error().code == NetErrc::WouldBlock);
            SleepBrief();
        }
        REQUIRE(accepted);

        std::array<NGIN::Byte, 64> recvBuffer {};
        auto recvTask = client.ReceiveAsync(ctx,
                                            *driver,
                                            ByteSpan {recvBuffer.data(), recvBuffer.size()},
                                            ctx.GetCancellationToken());
        recvTask.Start(ctx);

        driver->PollOnce();
        scheduler.RunUntilIdle();

        server.Close();

        REQUIRE(PumpUntil(scheduler, *driver, [&]() { return recvTask.IsCompleted(); }));
        REQUIRE(recvTask.Get() == 0);

        client.Close();
        listener.Close();
    }

    TEST_CASE("Net.Tcp.PartialReceive")
    {
        NGIN::Execution::CooperativeScheduler scheduler;
        NGIN::Async::TaskContext              ctx(scheduler);
        auto                                  driver = NetworkDriver::Create({.workerThreads = 0});

        TcpListener listener;
        REQUIRE(listener.Open(AddressFamily::V4));
        REQUIRE(listener.Bind({IpAddress::AnyV4(), 0}));
        REQUIRE(listener.Listen(8));

        const auto port = GetBoundPort(listener.Handle());
        REQUIRE(port != 0);

        TcpSocket client;
        REQUIRE(client.Open(AddressFamily::V4));
        REQUIRE(client.ConnectBlocking({IpAddress::LoopbackV4(), port}));

        TcpSocket server;
        bool accepted = false;
        for (int attempt = 0; attempt < 128; ++attempt)
        {
            auto acceptResult = listener.TryAccept();
            if (acceptResult)
            {
                server = std::move(*acceptResult);
                accepted = true;
                break;
            }
            REQUIRE(acceptResult.error().code == NetErrc::WouldBlock);
            SleepBrief();
        }
        REQUIRE(accepted);

        std::vector<NGIN::Byte> payload(4096);
        for (std::size_t i = 0; i < payload.size(); ++i)
        {
            payload[i] = static_cast<NGIN::Byte>(i & 0xFF);
        }

        auto sendTask = client.SendAsync(ctx,
                                         *driver,
                                         ConstByteSpan {payload.data(), payload.size()},
                                         ctx.GetCancellationToken());
        sendTask.Start(ctx);

        std::array<NGIN::Byte, 256> recvBuffer {};
        std::size_t totalReceived = 0;
        while (totalReceived < payload.size())
        {
            auto recvTask = server.ReceiveAsync(ctx,
                                                *driver,
                                                ByteSpan {recvBuffer.data(), recvBuffer.size()},
                                                ctx.GetCancellationToken());
            recvTask.Start(ctx);

            REQUIRE(PumpUntil(scheduler, *driver, [&]() { return recvTask.IsCompleted(); }));
            const auto bytes = recvTask.Get();
            REQUIRE(bytes > 0);
            REQUIRE(std::memcmp(recvBuffer.data(), payload.data() + totalReceived, bytes) == 0);
            totalReceived += bytes;
        }

        REQUIRE(PumpUntil(scheduler, *driver, [&]() { return sendTask.IsCompleted(); }));
        REQUIRE(sendTask.Get() == payload.size());

        client.Close();
        server.Close();
        listener.Close();
    }

    TEST_CASE("Net.Tcp.ConnectBlockingRefused")
    {
        TcpListener listener;
        REQUIRE(listener.Open(AddressFamily::V4));
        REQUIRE(listener.Bind({IpAddress::AnyV4(), 0}));
        REQUIRE(listener.Listen(8));

        const auto port = GetBoundPort(listener.Handle());
        REQUIRE(port != 0);
        listener.Close();

        TcpSocket client;
        REQUIRE(client.Open(AddressFamily::V4));
        auto result = client.ConnectBlocking({IpAddress::LoopbackV4(), port});
        REQUIRE_FALSE(result);
        const bool refused = result.error().code == NetErrc::Disconnected ||
                              result.error().code == NetErrc::ConnectionReset;
        REQUIRE(refused);

        client.Close();
    }

    TEST_CASE("Net.Tcp.AsyncAcceptCloseWhilePending")
    {
        NGIN::Execution::CooperativeScheduler scheduler;
        NGIN::Async::TaskContext              ctx(scheduler);
        auto                                  driver = NetworkDriver::Create({.workerThreads = 0});

        TcpListener listener;
        REQUIRE(listener.Open(AddressFamily::V4));
        REQUIRE(listener.Bind({IpAddress::AnyV4(), 0}));
        REQUIRE(listener.Listen(8));

        NGIN::Async::CancellationSource cancel;
        auto acceptTask = listener.AcceptAsync(ctx, *driver, cancel.GetToken());
        acceptTask.Start(ctx);

        driver->PollOnce();
        scheduler.RunUntilIdle();

        listener.Close();
        cancel.Cancel();

        REQUIRE(PumpUntil(scheduler, *driver, [&]() { return acceptTask.IsCompleted(); }));

        bool failed = false;
        try
        {
            auto socket = acceptTask.Get();
            (void)socket;
        }
        catch (...)
        {
            failed = true;
        }
        REQUIRE(failed);
    }

    TEST_CASE("Net.Tcp.AsyncReceiveCloseWhilePending")
    {
        NGIN::Execution::CooperativeScheduler scheduler;
        NGIN::Async::TaskContext              ctx(scheduler);
        auto                                  driver = NetworkDriver::Create({.workerThreads = 0});

        TcpListener listener;
        REQUIRE(listener.Open(AddressFamily::V4));
        REQUIRE(listener.Bind({IpAddress::AnyV4(), 0}));
        REQUIRE(listener.Listen(8));

        const auto port = GetBoundPort(listener.Handle());
        REQUIRE(port != 0);

        TcpSocket client;
        REQUIRE(client.Open(AddressFamily::V4));
        REQUIRE(client.ConnectBlocking({IpAddress::LoopbackV4(), port}));

        TcpSocket server;
        bool accepted = false;
        for (int attempt = 0; attempt < 128; ++attempt)
        {
            auto acceptResult = listener.TryAccept();
            if (acceptResult)
            {
                server = std::move(*acceptResult);
                accepted = true;
                break;
            }
            REQUIRE(acceptResult.error().code == NetErrc::WouldBlock);
            SleepBrief();
        }
        REQUIRE(accepted);

        std::array<NGIN::Byte, 64> recvBuffer {};
        NGIN::Async::CancellationSource cancel;

        auto recvTask = server.ReceiveAsync(ctx,
                                            *driver,
                                            ByteSpan {recvBuffer.data(), recvBuffer.size()},
                                            cancel.GetToken());
        recvTask.Start(ctx);

        driver->PollOnce();
        scheduler.RunUntilIdle();

        server.Close();
        cancel.Cancel();

        REQUIRE(PumpUntil(scheduler, *driver, [&]() { return recvTask.IsCompleted(); }));

        bool failed = false;
        try
        {
            (void)recvTask.Get();
        }
        catch (...)
        {
            failed = true;
        }
        REQUIRE(failed);

        client.Close();
        listener.Close();
    }

    TEST_CASE("Net.Tcp.DualStackV6ListenerV4Client")
    {
        TcpListener listener;
        auto openResult = listener.Open(AddressFamily::DualStack);
        if (!openResult)
        {
            SUCCEED("DualStack not supported");
            return;
        }

        auto bindResult = listener.Bind({IpAddress::AnyV6(), 0});
        if (!bindResult)
        {
            SUCCEED("V6 bind not available");
            return;
        }
        REQUIRE(listener.Listen(8));

        const auto port = GetBoundPort(listener.Handle());
        REQUIRE(port != 0);

        TcpSocket client;
        REQUIRE(client.Open(AddressFamily::V4));
        REQUIRE(client.ConnectBlocking({IpAddress::LoopbackV4(), port}));

        TcpSocket server;
        bool accepted = false;
        for (int attempt = 0; attempt < 128; ++attempt)
        {
            auto acceptResult = listener.TryAccept();
            if (acceptResult)
            {
                server = std::move(*acceptResult);
                accepted = true;
                break;
            }
            REQUIRE(acceptResult.error().code == NetErrc::WouldBlock);
            SleepBrief();
        }
        REQUIRE(accepted);

        const char payload[] = "v4-to-v6";
        REQUIRE(client.TrySend(ConstByteSpan {reinterpret_cast<const NGIN::Byte*>(payload), sizeof(payload)}));

        std::array<NGIN::Byte, 64> recvBuffer {};
        bool received = false;
        for (int attempt = 0; attempt < 128; ++attempt)
        {
            auto recvResult = server.TryReceive(ByteSpan {recvBuffer.data(), recvBuffer.size()});
            if (recvResult)
            {
                REQUIRE(*recvResult == sizeof(payload));
                REQUIRE(std::memcmp(recvBuffer.data(), payload, sizeof(payload)) == 0);
                received = true;
                break;
            }
            REQUIRE(recvResult.error().code == NetErrc::WouldBlock);
            SleepBrief();
        }
        REQUIRE(received);

        client.Close();
        server.Close();
        listener.Close();
    }

    TEST_CASE("Net.Tcp.AsyncConnectRefused")
    {
        NGIN::Execution::CooperativeScheduler scheduler;
        NGIN::Async::TaskContext              ctx(scheduler);
        auto                                  driver = NetworkDriver::Create({.workerThreads = 0});

        TcpListener listener;
        REQUIRE(listener.Open(AddressFamily::V4));
        REQUIRE(listener.Bind({IpAddress::AnyV4(), 0}));
        REQUIRE(listener.Listen(8));

        const auto port = GetBoundPort(listener.Handle());
        REQUIRE(port != 0);
        listener.Close();

        TcpSocket client;
        REQUIRE(client.Open(AddressFamily::V4));

        auto connectTask = client.ConnectAsync(ctx, *driver, {IpAddress::LoopbackV4(), port}, ctx.GetCancellationToken());
        connectTask.Start(ctx);

        REQUIRE(PumpUntil(scheduler, *driver, [&]() { return connectTask.IsCompleted(); }));

        bool failed = false;
        try
        {
            connectTask.Get();
        }
        catch (...)
        {
            failed = true;
        }
        REQUIRE(failed);

        client.Close();
    }
}// namespace NGIN::Net
