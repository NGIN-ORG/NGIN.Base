#include <NGIN/Net/Sockets/TcpSocket.hpp>

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

    NetExpected<void> TcpSocket::Open(AddressFamily family, SocketOptions options) noexcept
    {
        m_handle.Close();
        NetError error {};
        m_handle = detail::CreateSocket(family, SOCK_STREAM, IPPROTO_TCP, options.nonBlocking, error);
        if (error.code != NetErrorCode::Ok)
        {
            return std::unexpected(error);
        }
        m_nonBlocking = options.nonBlocking;

        auto optionResult = detail::ApplySocketOptions(m_handle, family, options, true, false);
        if (!optionResult)
        {
            m_handle.Close();
            return std::unexpected(optionResult.error());
        }
        return {};
    }

    NetExpected<bool> TcpSocket::TryConnect(Endpoint remoteEndpoint) noexcept
    {
        sockaddr_storage storage {};
        socklen_t length = 0;
        if (!detail::ToSockAddr(remoteEndpoint, storage, length))
        {
            return std::unexpected(NetError {NetErrorCode::Unknown, 0});
        }

        const int result = ::connect(detail::ToNative(m_handle), reinterpret_cast<sockaddr*>(&storage), length);
        if (result == 0)
        {
            return true;
        }

        const auto err = detail::LastError();
        if (detail::IsWouldBlock(err) || detail::IsInProgress(err))
        {
            return std::unexpected(NetError {NetErrorCode::WouldBlock, err.native});
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
            co_await NGIN::Async::Task<void>::ReturnError(ToAsyncError(detail::LastError()));
            co_return;
        }

        auto result = co_await driver.SubmitConnect(ctx, m_handle, remoteEndpoint, token);
        if (!result)
        {
            co_await NGIN::Async::Task<void>::ReturnError(result.error());
            co_return;
        }
        co_return;
#else
        for (;;)
        {
            auto result = TryConnect(remoteEndpoint);
            if (result)
            {
                co_return;
            }

            if (result.error().code != NetErrorCode::WouldBlock)
            {
                co_await NGIN::Async::Task<void>::ReturnError(ToAsyncError(result.error()));
                co_return;
            }

            auto waitResult = co_await driver.WaitUntilWritable(ctx, m_handle, token);
            if (!waitResult)
            {
                co_await NGIN::Async::Task<void>::ReturnError(waitResult.error());
                co_return;
            }
            auto connectResult = detail::CheckConnectResult(m_handle);
            if (connectResult)
            {
                co_return;
            }

            if (connectResult.error().code != NetErrorCode::WouldBlock)
            {
                co_await NGIN::Async::Task<void>::ReturnError(ToAsyncError(connectResult.error()));
                co_return;
            }
        }
#endif
    }

    NetExpected<void> TcpSocket::Connect(Endpoint remoteEndpoint)
    {
        const bool restoreNonBlocking = m_nonBlocking;
        static_cast<void>(detail::SetNonBlocking(m_handle, false));
        sockaddr_storage storage {};
        socklen_t length = 0;
        if (!detail::ToSockAddr(remoteEndpoint, storage, length))
        {
            static_cast<void>(detail::SetNonBlocking(m_handle, restoreNonBlocking));
            return std::unexpected(NetError {NetErrorCode::Unknown, 0});
        }

        if (::connect(detail::ToNative(m_handle), reinterpret_cast<sockaddr*>(&storage), length) != 0)
        {
            auto error = detail::LastError();
            static_cast<void>(detail::SetNonBlocking(m_handle, restoreNonBlocking));
            return std::unexpected(error);
        }

        static_cast<void>(detail::SetNonBlocking(m_handle, restoreNonBlocking));
        return {};
    }

    NetExpected<NGIN::UInt32> TcpSocket::TrySend(ConstByteSpan data) noexcept
    {
        if (data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return std::unexpected(NetError {NetErrorCode::MessageTooLarge, 0});
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
            return std::unexpected(NetError {NetErrorCode::MessageTooLarge, 0});
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

    NetExpected<NGIN::UInt32> TcpSocket::TrySendSegments(BufferSegmentSpan data) noexcept
    {
        if (data.empty())
        {
            return static_cast<NGIN::UInt32>(0);
        }

#if defined(NGIN_PLATFORM_WINDOWS)
        constexpr std::size_t kStackBuffers = 16;
        if (data.size() > std::numeric_limits<DWORD>::max())
        {
            return std::unexpected(NetError {NetErrorCode::MessageTooLarge, 0});
        }

        std::array<WSABUF, kStackBuffers> stackBuffers {};
        std::vector<WSABUF> heapBuffers {};
        WSABUF* buffers = stackBuffers.data();
        if (data.size() > stackBuffers.size())
        {
            heapBuffers.resize(data.size());
            buffers = heapBuffers.data();
        }

        for (std::size_t i = 0; i < data.size(); ++i)
        {
            if (data[i].size > std::numeric_limits<ULONG>::max())
            {
                return std::unexpected(NetError {NetErrorCode::MessageTooLarge, 0});
            }
            buffers[i].buf = reinterpret_cast<char*>(const_cast<NGIN::Byte*>(data[i].data));
            buffers[i].len = static_cast<ULONG>(data[i].size);
        }

        DWORD bytes = 0;
        DWORD flags = 0;
        const auto sock = detail::ToNative(m_handle);
        const int result = ::WSASend(sock, buffers, static_cast<DWORD>(data.size()), &bytes, flags, nullptr, nullptr);
        if (result != 0)
        {
            return std::unexpected(detail::LastError());
        }
        return static_cast<NGIN::UInt32>(bytes);
#else
        constexpr std::size_t kStackBuffers = 16;
        const auto maxIov = static_cast<std::size_t>(IOV_MAX);
        if (data.size() > maxIov)
        {
            return std::unexpected(NetError {NetErrorCode::MessageTooLarge, 0});
        }

        std::array<iovec, kStackBuffers> stackBuffers {};
        std::vector<iovec> heapBuffers {};
        iovec* buffers = stackBuffers.data();
        if (data.size() > stackBuffers.size())
        {
            heapBuffers.resize(data.size());
            buffers = heapBuffers.data();
        }

        for (std::size_t i = 0; i < data.size(); ++i)
        {
            buffers[i].iov_base = const_cast<NGIN::Byte*>(data[i].data);
            buffers[i].iov_len = data[i].size;
        }

        const auto sock = detail::ToNative(m_handle);
        const auto bytes = ::writev(sock, buffers, static_cast<int>(data.size()));
        if (bytes < 0)
        {
            return std::unexpected(detail::LastError());
        }
        return static_cast<NGIN::UInt32>(bytes);
#endif
    }

    NetExpected<NGIN::UInt32> TcpSocket::TryReceiveSegments(MutableBufferSegmentSpan destination) noexcept
    {
        if (destination.empty())
        {
            return static_cast<NGIN::UInt32>(0);
        }

#if defined(NGIN_PLATFORM_WINDOWS)
        constexpr std::size_t kStackBuffers = 16;
        if (destination.size() > std::numeric_limits<DWORD>::max())
        {
            return std::unexpected(NetError {NetErrorCode::MessageTooLarge, 0});
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
                return std::unexpected(NetError {NetErrorCode::MessageTooLarge, 0});
            }
            buffers[i].buf = reinterpret_cast<char*>(destination[i].data);
            buffers[i].len = static_cast<ULONG>(destination[i].size);
        }

        DWORD bytes = 0;
        DWORD flags = 0;
        const auto sock = detail::ToNative(m_handle);
        const int result = ::WSARecv(sock, buffers, static_cast<DWORD>(destination.size()), &bytes, &flags, nullptr, nullptr);
        if (result != 0)
        {
            return std::unexpected(detail::LastError());
        }
        return static_cast<NGIN::UInt32>(bytes);
#else
        constexpr std::size_t kStackBuffers = 16;
        const auto maxIov = static_cast<std::size_t>(IOV_MAX);
        if (destination.size() > maxIov)
        {
            return std::unexpected(NetError {NetErrorCode::MessageTooLarge, 0});
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

        const auto sock = detail::ToNative(m_handle);
        const auto bytes = ::readv(sock, buffers, static_cast<int>(destination.size()));
        if (bytes < 0)
        {
            return std::unexpected(detail::LastError());
        }
        return static_cast<NGIN::UInt32>(bytes);
#endif
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

            if (result.error().code != NetErrorCode::WouldBlock)
            {
                co_return std::unexpected(ToAsyncError(result.error()));
            }

            auto waitResult = co_await driver.WaitUntilWritable(ctx, m_handle, token);
            if (!waitResult)
            {
                co_return std::unexpected(waitResult.error());
            }
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

    NetExpected<void> TcpSocket::Shutdown(ShutdownMode mode) noexcept
    {
        return detail::Shutdown(m_handle, mode);
    }

    void TcpSocket::Close() noexcept
    {
        m_handle.Close();
    }
}

