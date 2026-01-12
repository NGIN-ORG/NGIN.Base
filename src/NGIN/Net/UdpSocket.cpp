#include <NGIN/Net/Sockets/UdpSocket.hpp>

#include "SocketPlatform.hpp"

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Net/Runtime/NetworkDriver.hpp>

#if !defined(NGIN_PLATFORM_WINDOWS)
  #include <sys/uio.h>
#endif

#include <array>
#include <limits>
#include <vector>
#include <stdexcept>

namespace NGIN::Net
{
    NetExpected<void> UdpSocket::Open(AddressFamily family, SocketOptions options) noexcept
    {
        m_handle.Close();
        NetError error {};
        m_handle = detail::CreateSocket(family, SOCK_DGRAM, IPPROTO_UDP, options.nonBlocking, error);
        if (error.code != NetErrc::Ok)
        {
            return std::unexpected(error);
        }

        auto optionResult = detail::ApplySocketOptions(m_handle, family, options, false, true);
        if (!optionResult)
        {
            m_handle.Close();
            return std::unexpected(optionResult.error());
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
                                   payload.size(),
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
                                     destination.size(),
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

    NetExpected<NGIN::UInt32> UdpSocket::TrySendToSegments(Endpoint remoteEndpoint, BufferSegmentSpan payload) noexcept
    {
        if (payload.empty())
        {
            return static_cast<NGIN::UInt32>(0);
        }

        sockaddr_storage storage {};
        socklen_t length = 0;
        if (!detail::ToSockAddr(remoteEndpoint, storage, length))
        {
            return std::unexpected(NetError {NetErrc::Unknown, 0});
        }

#if defined(NGIN_PLATFORM_WINDOWS)
        constexpr std::size_t kStackBuffers = 16;
        if (payload.size() > std::numeric_limits<DWORD>::max())
        {
            return std::unexpected(NetError {NetErrc::MessageTooLarge, 0});
        }

        std::array<WSABUF, kStackBuffers> stackBuffers {};
        std::vector<WSABUF> heapBuffers {};
        WSABUF* buffers = stackBuffers.data();
        if (payload.size() > stackBuffers.size())
        {
            heapBuffers.resize(payload.size());
            buffers = heapBuffers.data();
        }

        for (std::size_t i = 0; i < payload.size(); ++i)
        {
            if (payload[i].size > std::numeric_limits<ULONG>::max())
            {
                return std::unexpected(NetError {NetErrc::MessageTooLarge, 0});
            }
            buffers[i].buf = reinterpret_cast<char*>(const_cast<NGIN::Byte*>(payload[i].data));
            buffers[i].len = static_cast<ULONG>(payload[i].size);
        }

        DWORD bytes = 0;
        const auto sock = detail::ToNative(m_handle);
        const int result = ::WSASendTo(sock,
                                       buffers,
                                       static_cast<DWORD>(payload.size()),
                                       &bytes,
                                       0,
                                       reinterpret_cast<const sockaddr*>(&storage),
                                       static_cast<int>(length),
                                       nullptr,
                                       nullptr);
        if (result != 0)
        {
            return std::unexpected(detail::LastError());
        }
        return static_cast<NGIN::UInt32>(bytes);
#else
        constexpr std::size_t kStackBuffers = 16;
        const auto maxIov = static_cast<std::size_t>(IOV_MAX);
        if (payload.size() > maxIov)
        {
            return std::unexpected(NetError {NetErrc::MessageTooLarge, 0});
        }

        std::array<iovec, kStackBuffers> stackBuffers {};
        std::vector<iovec> heapBuffers {};
        iovec* buffers = stackBuffers.data();
        if (payload.size() > stackBuffers.size())
        {
            heapBuffers.resize(payload.size());
            buffers = heapBuffers.data();
        }

        for (std::size_t i = 0; i < payload.size(); ++i)
        {
            buffers[i].iov_base = const_cast<NGIN::Byte*>(payload[i].data);
            buffers[i].iov_len = payload[i].size;
        }

        msghdr msg {};
        msg.msg_name = &storage;
        msg.msg_namelen = length;
        msg.msg_iov = buffers;
        msg.msg_iovlen = payload.size();

        const auto sock = detail::ToNative(m_handle);
        const auto bytes = ::sendmsg(sock, &msg, 0);
        if (bytes < 0)
        {
            return std::unexpected(detail::LastError());
        }
        return static_cast<NGIN::UInt32>(bytes);
#endif
    }

    NetExpected<DatagramReceiveResult> UdpSocket::TryReceiveFromSegments(MutableBufferSegmentSpan destination) noexcept
    {
        if (destination.empty())
        {
            return DatagramReceiveResult {Endpoint {}, 0};
        }

        sockaddr_storage storage {};
        socklen_t length = static_cast<socklen_t>(sizeof(storage));

#if defined(NGIN_PLATFORM_WINDOWS)
        constexpr std::size_t kStackBuffers = 16;
        if (destination.size() > std::numeric_limits<DWORD>::max())
        {
            return std::unexpected(NetError {NetErrc::MessageTooLarge, 0});
        }

        std::array<WSABUF, kStackBuffers> stackBuffers {};
        std::vector<WSABUF> heapBuffers {};
        WSABUF* buffers = stackBuffers.data();
        if (destination.size() > stackBuffers.size())
        {
            heapBuffers.resize(destination.size());
            buffers = heapBuffers.data();
        }

        for (std::size_t i = 0; i < destination.size(); ++i)
        {
            if (destination[i].size > std::numeric_limits<ULONG>::max())
            {
                return std::unexpected(NetError {NetErrc::MessageTooLarge, 0});
            }
            buffers[i].buf = reinterpret_cast<char*>(destination[i].data);
            buffers[i].len = static_cast<ULONG>(destination[i].size);
        }

        DWORD bytes = 0;
        DWORD flags = 0;
        const auto sock = detail::ToNative(m_handle);
        const int result = ::WSARecvFrom(sock,
                                         buffers,
                                         static_cast<DWORD>(destination.size()),
                                         &bytes,
                                         &flags,
                                         reinterpret_cast<sockaddr*>(&storage),
                                         reinterpret_cast<int*>(&length),
                                         nullptr,
                                         nullptr);
        if (result != 0)
        {
            return std::unexpected(detail::LastError());
        }

        return DatagramReceiveResult {detail::FromSockAddr(storage, length), static_cast<NGIN::UInt32>(bytes)};
#else
        constexpr std::size_t kStackBuffers = 16;
        const auto maxIov = static_cast<std::size_t>(IOV_MAX);
        if (destination.size() > maxIov)
        {
            return std::unexpected(NetError {NetErrc::MessageTooLarge, 0});
        }

        std::array<iovec, kStackBuffers> stackBuffers {};
        std::vector<iovec> heapBuffers {};
        iovec* buffers = stackBuffers.data();
        if (destination.size() > stackBuffers.size())
        {
            heapBuffers.resize(destination.size());
            buffers = heapBuffers.data();
        }

        for (std::size_t i = 0; i < destination.size(); ++i)
        {
            buffers[i].iov_base = destination[i].data;
            buffers[i].iov_len = destination[i].size;
        }

        msghdr msg {};
        msg.msg_name = &storage;
        msg.msg_namelen = length;
        msg.msg_iov = buffers;
        msg.msg_iovlen = destination.size();

        const auto sock = detail::ToNative(m_handle);
        const auto bytes = ::recvmsg(sock, &msg, 0);
        if (bytes < 0)
        {
            return std::unexpected(detail::LastError());
        }

        return DatagramReceiveResult {detail::FromSockAddr(storage, length), static_cast<NGIN::UInt32>(bytes)};
#endif
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
                throw std::system_error(ToErrorCode(result.error()), "UdpSocket::SendToAsync failed");
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
                throw std::system_error(ToErrorCode(result.error()), "UdpSocket::ReceiveFromAsync failed");
            }

            co_await driver.WaitUntilReadable(ctx, m_handle, token);
        }
#endif
    }
}
