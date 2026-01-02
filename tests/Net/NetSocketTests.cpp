#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>

#if defined(NGIN_PLATFORM_WINDOWS)
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
#endif

#include <NGIN/Execution/ThisThread.hpp>
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
}// namespace NGIN::Net
