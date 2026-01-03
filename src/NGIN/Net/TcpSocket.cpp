#include <NGIN/Net/Sockets/TcpSocket.hpp>

#include "SocketPlatform.hpp"

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Net/Runtime/NetworkDriver.hpp>

#include <limits>
#include <stdexcept>

namespace NGIN::Net
{
    NetExpected<void> TcpSocket::Open(AddressFamily family) noexcept
    {
        NetError error {};
        const bool dualStack = family == AddressFamily::DualStack;
        m_handle = detail::CreateSocket(family, SOCK_STREAM, IPPROTO_TCP, dualStack, error);
        if (error.code != NetErrc::Ok)
        {
            return std::unexpected(error);
        }
        return {};
    }

    NetExpected<bool> TcpSocket::TryConnect(Endpoint remoteEndpoint) noexcept
    {
        sockaddr_storage storage {};
        socklen_t length = 0;
        if (!detail::ToSockAddr(remoteEndpoint, storage, length))
        {
            return std::unexpected(NetError {NetErrc::Unknown, 0});
        }

        const int result = ::connect(detail::ToNative(m_handle), reinterpret_cast<sockaddr*>(&storage), length);
        if (result == 0)
        {
            return true;
        }

        const auto err = detail::LastError();
        if (detail::IsWouldBlock(err) || detail::IsInProgress(err))
        {
            return std::unexpected(NetError {NetErrc::WouldBlock, err.native});
        }

        return std::unexpected(err);
    }

    NGIN::Async::Task<void> TcpSocket::ConnectAsync(NGIN::Async::TaskContext& ctx,
                                                    NetworkDriver& driver,
                                                    Endpoint remoteEndpoint,
                                                    NGIN::Async::CancellationToken token)
    {
#if defined(NGIN_PLATFORM_WINDOWS)
        if (!detail::EnsureBoundForConnectEx(m_handle, remoteEndpoint))
        {
            throw std::runtime_error("TcpSocket::ConnectAsync failed to bind for ConnectEx");
        }

        co_await driver.SubmitConnect(ctx, m_handle, remoteEndpoint, token);
        co_return;
#else
        for (;;)
        {
            auto result = TryConnect(remoteEndpoint);
            if (result)
            {
                co_return;
            }

            if (result.error().code != NetErrc::WouldBlock)
            {
                throw std::runtime_error("TcpSocket::ConnectAsync failed");
            }

            co_await driver.WaitUntilWritable(ctx, m_handle, token);
            auto connectResult = detail::CheckConnectResult(m_handle);
            if (connectResult)
            {
                co_return;
            }

            if (connectResult.error().code != NetErrc::WouldBlock)
            {
                throw std::runtime_error("TcpSocket::ConnectAsync failed");
            }
        }
#endif
    }

    NetExpected<void> TcpSocket::ConnectBlocking(Endpoint remoteEndpoint)
    {
        static_cast<void>(detail::SetNonBlocking(m_handle, false));
        sockaddr_storage storage {};
        socklen_t length = 0;
        if (!detail::ToSockAddr(remoteEndpoint, storage, length))
        {
            static_cast<void>(detail::SetNonBlocking(m_handle, true));
            return std::unexpected(NetError {NetErrc::Unknown, 0});
        }

        if (::connect(detail::ToNative(m_handle), reinterpret_cast<sockaddr*>(&storage), length) != 0)
        {
            auto error = detail::LastError();
            static_cast<void>(detail::SetNonBlocking(m_handle, true));
            return std::unexpected(error);
        }

        static_cast<void>(detail::SetNonBlocking(m_handle, true));
        return {};
    }

    NetExpected<NGIN::UInt32> TcpSocket::TrySend(ConstByteSpan data) noexcept
    {
        if (data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return std::unexpected(NetError {NetErrc::MessageTooLarge, 0});
        }

        int flags = 0;
#if defined(MSG_NOSIGNAL)
        flags |= MSG_NOSIGNAL;
#endif
        const int bytes = ::send(detail::ToNative(m_handle),
                                 reinterpret_cast<const char*>(data.data()),
                                 static_cast<int>(data.size()),
                                 flags);
        if (bytes < 0)
        {
            return std::unexpected(detail::LastError());
        }

        return static_cast<NGIN::UInt32>(bytes);
    }

    NetExpected<NGIN::UInt32> TcpSocket::TryReceive(ByteSpan destination) noexcept
    {
        if (destination.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return std::unexpected(NetError {NetErrc::MessageTooLarge, 0});
        }

        const int bytes = ::recv(detail::ToNative(m_handle),
                                 reinterpret_cast<char*>(destination.data()),
                                 static_cast<int>(destination.size()),
                                 0);
        if (bytes < 0)
        {
            return std::unexpected(detail::LastError());
        }

        return static_cast<NGIN::UInt32>(bytes);
    }

    NGIN::Async::Task<NGIN::UInt32> TcpSocket::SendAsync(NGIN::Async::TaskContext& ctx,
                                                        NetworkDriver& driver,
                                                        ConstByteSpan data,
                                                        NGIN::Async::CancellationToken token)
    {
#if defined(NGIN_PLATFORM_WINDOWS)
        co_return co_await driver.SubmitSend(ctx, m_handle, data, token);
#else
        for (;;)
        {
            auto result = TrySend(data);
            if (result)
            {
                co_return *result;
            }

            if (result.error().code != NetErrc::WouldBlock)
            {
                throw std::runtime_error("TcpSocket::SendAsync failed");
            }

            co_await driver.WaitUntilWritable(ctx, m_handle, token);
        }
#endif
    }

    NGIN::Async::Task<NGIN::UInt32> TcpSocket::ReceiveAsync(NGIN::Async::TaskContext& ctx,
                                                           NetworkDriver& driver,
                                                           ByteSpan destination,
                                                           NGIN::Async::CancellationToken token)
    {
#if defined(NGIN_PLATFORM_WINDOWS)
        co_return co_await driver.SubmitReceive(ctx, m_handle, destination, token);
#else
        for (;;)
        {
            auto result = TryReceive(destination);
            if (result)
            {
                co_return *result;
            }

            if (result.error().code != NetErrc::WouldBlock)
            {
                throw std::runtime_error("TcpSocket::ReceiveAsync failed");
            }

            co_await driver.WaitUntilReadable(ctx, m_handle, token);
        }
#endif
    }

    NetExpected<void> TcpSocket::Shutdown(ShutdownMode mode) noexcept
    {
        return detail::Shutdown(m_handle, mode);
    }

    void TcpSocket::Close() noexcept
    {
        m_handle.Close();
    }
}
