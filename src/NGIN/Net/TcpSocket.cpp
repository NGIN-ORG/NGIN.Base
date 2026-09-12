#include <NGIN/Net/Sockets/TcpSocket.hpp>

#include "SocketPlatform.hpp"

#include "NetworkDriver.hpp"
#include "SocketState.hpp"
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>

#if !defined(NGIN_PLATFORM_WINDOWS)
#include <sys/uio.h>
#endif

#include <array>
#include <limits>
#include <vector>

namespace NGIN::Net
{
    NetExpected<void> TcpSocket::Open(AddressFamily family, SocketOptions options) noexcept
    {
        m_handle.Close();
        NetError error {};
        m_handle = detail::CreateSocket(family, SOCK_STREAM, IPPROTO_TCP, options.nonBlocking, error);
        if (error.code != NetErrorCode::Ok)
        {
            return NGIN::Utilities::Unexpected(error);
        }
        m_nonBlocking = options.nonBlocking;

        auto optionResult = detail::ApplySocketOptions(m_handle, family, options, true, false);
        if (!optionResult)
        {
            m_handle.Close();
            return NGIN::Utilities::Unexpected(optionResult.error());
        }
        return {};
    }

    NetExpected<bool> TcpSocket::TryConnect(Endpoint remoteEndpoint) noexcept
    {
        sockaddr_storage storage {};
        socklen_t        length = 0;
        if (!detail::ToSockAddr(remoteEndpoint, storage, length))
        {
            return NGIN::Utilities::Unexpected(NetError {NetErrorCode::Unknown, 0});
        }

        const int result = ::connect(detail::ToNative(m_handle), reinterpret_cast<sockaddr*>(&storage), length);
        if (result == 0)
        {
            return true;
        }

        const auto err = detail::LastError();
        if (detail::IsWouldBlock(err) || detail::IsInProgress(err))
        {
            return NGIN::Utilities::Unexpected(NetError {NetErrorCode::WouldBlock, err.native});
        }

        return NGIN::Utilities::Unexpected(err);
    }

    NGIN::Async::Task<void, NetError> TcpSocket::ConnectAsync(NGIN::Async::TaskContext&      ctx,
                                                              Endpoint                       remoteEndpoint,
                                                              NGIN::Async::CancellationToken token)
    {
        return NetworkDriver::SubmitConnect(ctx, m_runtime, detail::SocketHandleAccess::State(m_handle), remoteEndpoint, std::move(token));
    }

    NetExpected<void> TcpSocket::Connect(Endpoint remoteEndpoint)
    {
        const bool restoreNonBlocking = m_nonBlocking;
        static_cast<void>(detail::SetNonBlocking(m_handle, false));
        sockaddr_storage storage {};
        socklen_t        length = 0;
        if (!detail::ToSockAddr(remoteEndpoint, storage, length))
        {
            static_cast<void>(detail::SetNonBlocking(m_handle, restoreNonBlocking));
            return NGIN::Utilities::Unexpected(NetError {NetErrorCode::Unknown, 0});
        }

        if (::connect(detail::ToNative(m_handle), reinterpret_cast<sockaddr*>(&storage), length) != 0)
        {
            auto error = detail::LastError();
            static_cast<void>(detail::SetNonBlocking(m_handle, restoreNonBlocking));
            return NGIN::Utilities::Unexpected(error);
        }

        static_cast<void>(detail::SetNonBlocking(m_handle, restoreNonBlocking));
        return {};
    }

    NetExpected<NGIN::UInt32> TcpSocket::TrySend(ConstByteSpan data) noexcept
    {
        if (data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return NGIN::Utilities::Unexpected(NetError {NetErrorCode::MessageTooLarge, 0});
        }

        int flags = 0;
#if defined(MSG_NOSIGNAL)
        flags |= MSG_NOSIGNAL;
#endif
        const auto bytes = ::send(detail::ToNative(m_handle),
                                  reinterpret_cast<const char*>(data.data()),
#if defined(NGIN_PLATFORM_WINDOWS)
                                  static_cast<int>(data.size()),
#else
                                  data.size(),
#endif
                                  flags);
        if (bytes < 0)
        {
            return NGIN::Utilities::Unexpected(detail::LastError());
        }

        return static_cast<NGIN::UInt32>(bytes);
    }

    NetExpected<NGIN::UInt32> TcpSocket::TryReceive(ByteSpan destination) noexcept
    {
        if (destination.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return NGIN::Utilities::Unexpected(NetError {NetErrorCode::MessageTooLarge, 0});
        }

        const auto bytes = ::recv(detail::ToNative(m_handle),
                                  reinterpret_cast<char*>(destination.data()),
#if defined(NGIN_PLATFORM_WINDOWS)
                                  static_cast<int>(destination.size()),
#else
                                  destination.size(),
#endif
                                  0);
        if (bytes < 0)
        {
            return NGIN::Utilities::Unexpected(detail::LastError());
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
            return NGIN::Utilities::Unexpected(NetError {NetErrorCode::MessageTooLarge, 0});
        }

        std::array<WSABUF, kStackBuffers> stackBuffers {};
        std::vector<WSABUF>               heapBuffers {};
        WSABUF*                           buffers = stackBuffers.data();
        if (data.size() > stackBuffers.size())
        {
            heapBuffers.resize(data.size());
            buffers = heapBuffers.data();
        }

        for (std::size_t i = 0; i < data.size(); ++i)
        {
            if (data[i].size > std::numeric_limits<ULONG>::max())
            {
                return NGIN::Utilities::Unexpected(NetError {NetErrorCode::MessageTooLarge, 0});
            }
            buffers[i].buf = reinterpret_cast<char*>(const_cast<NGIN::Byte*>(data[i].data));
            buffers[i].len = static_cast<ULONG>(data[i].size);
        }

        DWORD      bytes  = 0;
        DWORD      flags  = 0;
        const auto sock   = detail::ToNative(m_handle);
        const int  result = ::WSASend(sock, buffers, static_cast<DWORD>(data.size()), &bytes, flags, nullptr, nullptr);
        if (result != 0)
        {
            return NGIN::Utilities::Unexpected(detail::LastError());
        }
        return static_cast<NGIN::UInt32>(bytes);
#else
        constexpr std::size_t kStackBuffers = 16;
        const auto            maxIov        = static_cast<std::size_t>(IOV_MAX);
        if (data.size() > maxIov)
        {
            return NGIN::Utilities::Unexpected(NetError {NetErrorCode::MessageTooLarge, 0});
        }

        std::array<iovec, kStackBuffers> stackBuffers {};
        std::vector<iovec>               heapBuffers {};
        iovec*                           buffers = stackBuffers.data();
        if (data.size() > stackBuffers.size())
        {
            heapBuffers.resize(data.size());
            buffers = heapBuffers.data();
        }

        for (std::size_t i = 0; i < data.size(); ++i)
        {
            buffers[i].iov_base = const_cast<NGIN::Byte*>(data[i].data);
            buffers[i].iov_len  = data[i].size;
        }

        const auto sock  = detail::ToNative(m_handle);
        const auto bytes = ::writev(sock, buffers, static_cast<int>(data.size()));
        if (bytes < 0)
        {
            return NGIN::Utilities::Unexpected(detail::LastError());
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
            return NGIN::Utilities::Unexpected(NetError {NetErrorCode::MessageTooLarge, 0});
        }

        std::array<WSABUF, kStackBuffers> stackBuffers {};
        std::vector<WSABUF>               heapBuffers {};
        WSABUF*                           buffers = stackBuffers.data();
        if (destination.size() > stackBuffers.size())
        {
            heapBuffers.resize(destination.size());
            buffers = heapBuffers.data();
        }

        for (std::size_t i = 0; i < destination.size(); ++i)
        {
            if (destination[i].size > std::numeric_limits<ULONG>::max())
            {
                return NGIN::Utilities::Unexpected(NetError {NetErrorCode::MessageTooLarge, 0});
            }
            buffers[i].buf = reinterpret_cast<char*>(destination[i].data);
            buffers[i].len = static_cast<ULONG>(destination[i].size);
        }

        DWORD      bytes  = 0;
        DWORD      flags  = 0;
        const auto sock   = detail::ToNative(m_handle);
        const int  result = ::WSARecv(sock, buffers, static_cast<DWORD>(destination.size()), &bytes, &flags, nullptr, nullptr);
        if (result != 0)
        {
            return NGIN::Utilities::Unexpected(detail::LastError());
        }
        return static_cast<NGIN::UInt32>(bytes);
#else
        constexpr std::size_t kStackBuffers = 16;
        const auto            maxIov        = static_cast<std::size_t>(IOV_MAX);
        if (destination.size() > maxIov)
        {
            return NGIN::Utilities::Unexpected(NetError {NetErrorCode::MessageTooLarge, 0});
        }

        std::array<iovec, kStackBuffers> stackBuffers {};
        std::vector<iovec>               heapBuffers {};
        iovec*                           buffers = stackBuffers.data();
        if (destination.size() > stackBuffers.size())
        {
            heapBuffers.resize(destination.size());
            buffers = heapBuffers.data();
        }

        for (std::size_t i = 0; i < destination.size(); ++i)
        {
            buffers[i].iov_base = destination[i].data;
            buffers[i].iov_len  = destination[i].size;
        }

        const auto sock  = detail::ToNative(m_handle);
        const auto bytes = ::readv(sock, buffers, static_cast<int>(destination.size()));
        if (bytes < 0)
        {
            return NGIN::Utilities::Unexpected(detail::LastError());
        }
        return static_cast<NGIN::UInt32>(bytes);
#endif
    }

    NGIN::Async::Task<NGIN::UInt32, NetError> TcpSocket::SendAsync(NGIN::Async::TaskContext&      ctx,
                                                                   ConstByteSpan                  data,
                                                                   NGIN::Async::CancellationToken token)
    {
        return NetworkDriver::SubmitSend(ctx, m_runtime, detail::SocketHandleAccess::State(m_handle), data, std::move(token));
    }

    NGIN::Async::Task<NGIN::UInt32, NetError> TcpSocket::ReceiveAsync(NGIN::Async::TaskContext&      ctx,
                                                                      ByteSpan                       destination,
                                                                      NGIN::Async::CancellationToken token)
    {
        return NetworkDriver::SubmitReceive(ctx, m_runtime, detail::SocketHandleAccess::State(m_handle), destination, std::move(token));
    }

    NetExpected<void> TcpSocket::Shutdown(ShutdownMode mode) noexcept
    {
        return detail::Shutdown(m_handle, mode);
    }

    void TcpSocket::Close() noexcept
    {
        m_handle.Close();
    }
}// namespace NGIN::Net
