#include <NGIN/Net/Sockets/TcpListener.hpp>

#include "SocketPlatform.hpp"

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Net/Runtime/NetworkDriver.hpp>

#include <stdexcept>
#include <utility>

namespace NGIN::Net
{
    NetExpected<void> TcpListener::Open(AddressFamily family) noexcept
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

    NetExpected<void> TcpListener::Bind(Endpoint localEndpoint) noexcept
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

    NetExpected<void> TcpListener::Listen(NGIN::Int32 backlog) noexcept
    {
        if (::listen(detail::ToNative(m_handle), backlog) != 0)
        {
            return std::unexpected(detail::LastError());
        }
        return {};
    }

    NetExpected<TcpSocket> TcpListener::TryAccept() noexcept
    {
        sockaddr_storage storage {};
        socklen_t length = static_cast<socklen_t>(sizeof(storage));
        const auto sock = ::accept(detail::ToNative(m_handle), reinterpret_cast<sockaddr*>(&storage), &length);
        if (sock == detail::InvalidNativeSocket)
        {
            return std::unexpected(detail::LastError());
        }

        TcpSocket socket(detail::FromNative(sock));
        (void)detail::SetNonBlocking(socket.Handle(), true);
        return socket;
    }

    NGIN::Async::Task<TcpSocket> TcpListener::AcceptAsync(NGIN::Async::TaskContext& ctx,
                                                          NetworkDriver& driver,
                                                          NGIN::Async::CancellationToken token)
    {
#if defined(NGIN_PLATFORM_WINDOWS)
        auto handle = co_await driver.SubmitAccept(ctx, m_handle, token);
        co_return TcpSocket(handle);
#else
        for (;;)
        {
            auto result = TryAccept();
            if (result)
            {
                co_return std::move(*result);
            }

            if (result.error().code != NetErrc::WouldBlock)
            {
                throw std::runtime_error("TcpListener::AcceptAsync failed");
            }

            co_await driver.WaitUntilReadable(ctx, m_handle, token);
        }
#endif
    }

    void TcpListener::Close() noexcept
    {
        m_handle.Close();
    }
}
