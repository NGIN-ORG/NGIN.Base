#include <NGIN/Net/Sockets/TcpListener.hpp>

#include "SocketPlatform.hpp"

#include "NetworkDriver.hpp"
#include "SocketState.hpp"
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>

#include <utility>

namespace NGIN::Net
{
    NetExpected<void> TcpListener::Open(AddressFamily family, SocketOptions options) noexcept
    {
        m_handle.Close();
        NetError error {};
        m_handle = detail::CreateSocket(family, SOCK_STREAM, IPPROTO_TCP, options.nonBlocking, error);
        if (error.code != NetErrorCode::Ok)
        {
            return NGIN::Utilities::Unexpected(error);
        }

        auto optionResult = detail::ApplySocketOptions(m_handle, family, options, true, false);
        if (!optionResult)
        {
            m_handle.Close();
            return NGIN::Utilities::Unexpected(optionResult.error());
        }
        return {};
    }

    NetExpected<void> TcpListener::Bind(Endpoint localEndpoint) noexcept
    {
        sockaddr_storage storage {};
        socklen_t        length = 0;
        if (!detail::ToSockAddr(localEndpoint, storage, length))
        {
            return NGIN::Utilities::Unexpected(NetError {NetErrorCode::Unknown, 0});
        }

        if (::bind(detail::ToNative(m_handle), reinterpret_cast<sockaddr*>(&storage), length) != 0)
        {
            return NGIN::Utilities::Unexpected(detail::LastError());
        }
        return {};
    }

    NetExpected<void> TcpListener::Listen(NGIN::Int32 backlog) noexcept
    {
        if (::listen(detail::ToNative(m_handle), backlog) != 0)
        {
            return NGIN::Utilities::Unexpected(detail::LastError());
        }
        return {};
    }

    NetExpected<TcpSocket> TcpListener::TryAccept() noexcept
    {
        sockaddr_storage storage {};
        socklen_t        length = static_cast<socklen_t>(sizeof(storage));
        const auto       sock   = ::accept(detail::ToNative(m_handle), reinterpret_cast<sockaddr*>(&storage), &length);
        if (sock == detail::InvalidNativeSocket)
        {
            return NGIN::Utilities::Unexpected(detail::LastError());
        }

        auto accepted = detail::FromNative(sock);
        if (!accepted.IsOpen())
            return NGIN::Utilities::Unexpected(NetError {NetErrorCode::ResourceExhausted});
        TcpSocket socket(std::move(accepted), true, m_runtime);
        if (!detail::SetNonBlocking(socket.Handle(), true))
            return NGIN::Utilities::Unexpected(detail::LastError());
        return socket;
    }

    NGIN::Async::Task<TcpSocket, NetError> TcpListener::AcceptAsync(NGIN::Async::TaskContext&      ctx,
                                                                    NGIN::Async::CancellationToken token)
    {
        return NetworkDriver::SubmitAccept(ctx, m_runtime, detail::SocketHandleAccess::State(m_handle), std::move(token));
    }

    void TcpListener::Close() noexcept
    {
        m_handle.Close();
    }
}// namespace NGIN::Net
