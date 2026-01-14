#include <NGIN/Net/Sockets/TcpListener.hpp>

#include "SocketPlatform.hpp"

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Net/Runtime/NetworkDriver.hpp>

#include <utility>

namespace NGIN::Net
{
    [[nodiscard]] static NGIN::Async::AsyncError ToAsyncError(NetError error) noexcept
    {
        using NGIN::Async::AsyncErrorCode;
        AsyncErrorCode code = AsyncErrorCode::Fault;
        switch (error.code)
        {
            case NetErrorCode::TimedOut: code = AsyncErrorCode::TimedOut; break;
            case NetErrorCode::MessageTooLarge: code = AsyncErrorCode::InvalidArgument; break;
            case NetErrorCode::WouldBlock: code = AsyncErrorCode::InvalidState; break;
            case NetErrorCode::Ok: code = AsyncErrorCode::Ok; break;
            default: break;
        }

        const int native = (error.native != 0) ? error.native : static_cast<int>(error.code);
        return NGIN::Async::MakeAsyncError(code, native);
    }

    NetExpected<void> TcpListener::Open(AddressFamily family, SocketOptions options) noexcept
    {
        m_handle.Close();
        NetError error {};
        m_handle = detail::CreateSocket(family, SOCK_STREAM, IPPROTO_TCP, options.nonBlocking, error);
        if (error.code != NetErrorCode::Ok)
        {
            return std::unexpected(error);
        }

        auto optionResult = detail::ApplySocketOptions(m_handle, family, options, true, false);
        if (!optionResult)
        {
            m_handle.Close();
            return std::unexpected(optionResult.error());
        }
        return {};
    }

    NetExpected<void> TcpListener::Bind(Endpoint localEndpoint) noexcept
    {
        sockaddr_storage storage {};
        socklen_t length = 0;
        if (!detail::ToSockAddr(localEndpoint, storage, length))
        {
            return std::unexpected(NetError {NetErrorCode::Unknown, 0});
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

        TcpSocket socket(std::move(detail::FromNative(sock)), true);
        (void)detail::SetNonBlocking(socket.Handle(), true);
        return socket;
    }

    NGIN::Async::Task<TcpSocket> TcpListener::AcceptAsync(NGIN::Async::TaskContext& ctx,
                                                          NetworkDriver& driver,
                                                          NGIN::Async::CancellationToken token)
    {
#if defined(NGIN_PLATFORM_WINDOWS)
        auto handleResult = co_await driver.SubmitAccept(ctx, m_handle, token);
        if (!handleResult)
        {
            co_return std::unexpected(handleResult.error());
        }
        co_return TcpSocket(std::move(*handleResult), true);
#else
        for (;;)
        {
            auto result = TryAccept();
            if (result)
            {
                co_return std::move(*result);
            }

            if (result.error().code != NetErrorCode::WouldBlock)
            {
                co_return std::unexpected(ToAsyncError(result.error()));
            }

            auto waitResult = co_await driver.WaitUntilReadable(ctx, m_handle, token);
            if (!waitResult)
            {
                co_return std::unexpected(waitResult.error());
            }
        }
#endif
    }

    void TcpListener::Close() noexcept
    {
        m_handle.Close();
    }
}

