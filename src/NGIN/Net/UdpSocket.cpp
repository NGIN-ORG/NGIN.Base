#include <NGIN/Net/Sockets/UdpSocket.hpp>

#include "SocketPlatform.hpp"

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Net/Runtime/NetworkDriver.hpp>

#include <limits>
#include <stdexcept>

namespace NGIN::Net
{
    NetExpected<void> UdpSocket::Open(AddressFamily family) noexcept
    {
        NetError error {};
        const bool dualStack = family == AddressFamily::DualStack;
        m_handle = detail::CreateSocket(family, SOCK_DGRAM, IPPROTO_UDP, dualStack, error);
        if (error.code != NetErrc::Ok)
        {
            return std::unexpected(error);
        }
        return {};
    }

    NetExpected<void> UdpSocket::Bind(Endpoint localEndpoint) noexcept
    {
        sockaddr_storage storage {};
        socklen_t length = 0;
        if (!detail::ToSockAddr(localEndpoint, storage, length))
        {
            return std::unexpected(NetError {NetErrc::Unknown, 0});
        }

        if (::bind(detail::ToNative(m_handle), reinterpret_cast<sockaddr*>(&storage), length) != 0)
        {
            return std::unexpected(detail::LastError());
        }
        return {};
    }

    NetExpected<void> UdpSocket::Connect(Endpoint remoteEndpoint) noexcept
    {
        sockaddr_storage storage {};
        socklen_t length = 0;
        if (!detail::ToSockAddr(remoteEndpoint, storage, length))
        {
            return std::unexpected(NetError {NetErrc::Unknown, 0});
        }

        if (::connect(detail::ToNative(m_handle), reinterpret_cast<sockaddr*>(&storage), length) != 0)
        {
            return std::unexpected(detail::LastError());
        }
        return {};
    }

    void UdpSocket::Close() noexcept
    {
        m_handle.Close();
    }

    NetExpected<NGIN::UInt32> UdpSocket::TrySendTo(Endpoint remoteEndpoint, ConstByteSpan payload) noexcept
    {
        if (payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return std::unexpected(NetError {NetErrc::MessageTooLarge, 0});
        }

        sockaddr_storage storage {};
        socklen_t length = 0;
        if (!detail::ToSockAddr(remoteEndpoint, storage, length))
        {
            return std::unexpected(NetError {NetErrc::Unknown, 0});
        }

        const int bytes = ::sendto(detail::ToNative(m_handle),
                                   reinterpret_cast<const char*>(payload.data()),
                                   static_cast<int>(payload.size()),
                                   0,
                                   reinterpret_cast<const sockaddr*>(&storage),
                                   length);
        if (bytes < 0)
        {
            return std::unexpected(detail::LastError());
        }

        return static_cast<NGIN::UInt32>(bytes);
    }

    NetExpected<DatagramReceiveResult> UdpSocket::TryReceiveFrom(ByteSpan destination) noexcept
    {
        if (destination.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return std::unexpected(NetError {NetErrc::MessageTooLarge, 0});
        }

        sockaddr_storage storage {};
        socklen_t length = static_cast<socklen_t>(sizeof(storage));
        const int bytes = ::recvfrom(detail::ToNative(m_handle),
                                     reinterpret_cast<char*>(destination.data()),
                                     static_cast<int>(destination.size()),
                                     0,
                                     reinterpret_cast<sockaddr*>(&storage),
                                     &length);
        if (bytes < 0)
        {
            return std::unexpected(detail::LastError());
        }

        DatagramReceiveResult result {};
        result.remoteEndpoint = detail::FromSockAddr(storage, length);
        result.bytesReceived = static_cast<NGIN::UInt32>(bytes);
        return result;
    }

    NGIN::Async::Task<NGIN::UInt32> UdpSocket::SendToAsync(NGIN::Async::TaskContext& ctx,
                                                          NetworkDriver& driver,
                                                          Endpoint remoteEndpoint,
                                                          ConstByteSpan payload,
                                                          NGIN::Async::CancellationToken token)
    {
#if defined(NGIN_PLATFORM_WINDOWS)
        co_return co_await driver.SubmitSendTo(ctx, m_handle, remoteEndpoint, payload, token);
#else
        for (;;)
        {
            auto result = TrySendTo(remoteEndpoint, payload);
            if (result)
            {
                co_return *result;
            }

            if (result.error().code != NetErrc::WouldBlock)
            {
                throw std::runtime_error("UdpSocket::SendToAsync failed");
            }

            co_await driver.WaitUntilWritable(ctx, m_handle, token);
        }
#endif
    }

    NGIN::Async::Task<DatagramReceiveResult> UdpSocket::ReceiveFromAsync(NGIN::Async::TaskContext& ctx,
                                                                         NetworkDriver& driver,
                                                                         ByteSpan destination,
                                                                         NGIN::Async::CancellationToken token)
    {
#if defined(NGIN_PLATFORM_WINDOWS)
        co_return co_await driver.SubmitReceiveFrom(ctx, m_handle, destination, token);
#else
        for (;;)
        {
            auto result = TryReceiveFrom(destination);
            if (result)
            {
                co_return *result;
            }

            if (result.error().code != NetErrc::WouldBlock)
            {
                throw std::runtime_error("UdpSocket::ReceiveFromAsync failed");
            }

            co_await driver.WaitUntilReadable(ctx, m_handle, token);
        }
#endif
    }
}
