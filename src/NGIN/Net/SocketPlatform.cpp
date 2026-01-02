#include "SocketPlatform.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <expected>
#include <mutex>

namespace NGIN::Net::detail
{
#if defined(NGIN_PLATFORM_WINDOWS)
    namespace
    {
        std::once_flag s_wsaOnce;
        std::atomic<bool> s_wsaOk {false};
    }
#endif

    bool EnsureInitialized() noexcept
    {
#if defined(NGIN_PLATFORM_WINDOWS)
        std::call_once(s_wsaOnce, []() {
            WSADATA data {};
            const int result = ::WSAStartup(MAKEWORD(2, 2), &data);
            s_wsaOk.store(result == 0, std::memory_order_release);
        });
        return s_wsaOk.load(std::memory_order_acquire);
#else
        return true;
#endif
    }

    NetError MapError(int native) noexcept
    {
        NetErrc code = NetErrc::Unknown;
#if defined(NGIN_PLATFORM_WINDOWS)
        switch (native)
        {
            case WSAEWOULDBLOCK: code = NetErrc::WouldBlock; break;
            case WSAETIMEDOUT: code = NetErrc::TimedOut; break;
            case WSAECONNRESET: code = NetErrc::ConnectionReset; break;
            case WSAECONNABORTED: code = NetErrc::Disconnected; break;
            case WSAENETUNREACH: code = NetErrc::HostUnreachable; break;
            case WSAEMSGSIZE: code = NetErrc::MessageTooLarge; break;
            case WSAEACCES: code = NetErrc::PermissionDenied; break;
            case WSAECONNREFUSED: code = NetErrc::Disconnected; break;
            default: break;
        }
#else
        switch (native)
        {
            case EWOULDBLOCK:
#if defined(EAGAIN) && EAGAIN != EWOULDBLOCK
            case EAGAIN:
#endif
                code = NetErrc::WouldBlock; break;
            case ETIMEDOUT: code = NetErrc::TimedOut; break;
            case ECONNRESET: code = NetErrc::ConnectionReset; break;
            case ECONNABORTED: code = NetErrc::Disconnected; break;
            case ENETUNREACH:
#if defined(EHOSTUNREACH)
            case EHOSTUNREACH:
#endif
                code = NetErrc::HostUnreachable; break;
            case EMSGSIZE: code = NetErrc::MessageTooLarge; break;
            case EACCES:
#if defined(EPERM)
            case EPERM:
#endif
                code = NetErrc::PermissionDenied; break;
            case ECONNREFUSED: code = NetErrc::Disconnected; break;
            default: break;
        }
#endif
        return NetError {code, native};
    }

    NetError LastError() noexcept
    {
#if defined(NGIN_PLATFORM_WINDOWS)
        return MapError(::WSAGetLastError());
#else
        return MapError(errno);
#endif
    }

    bool IsWouldBlock(const NetError& error) noexcept
    {
        return error.code == NetErrc::WouldBlock;
    }

    bool IsInProgress(const NetError& error) noexcept
    {
#if defined(NGIN_PLATFORM_WINDOWS)
        return error.native == WSAEINPROGRESS || error.native == WSAEWOULDBLOCK || error.native == WSAEALREADY;
#else
        return error.native == EINPROGRESS || error.native == EALREADY;
#endif
    }

    NativeSocket ToNative(const SocketHandle& handle) noexcept
    {
        return static_cast<NativeSocket>(handle.Native());
    }

    SocketHandle FromNative(NativeSocket socket) noexcept
    {
        return SocketHandle(static_cast<SocketHandle::NativeHandle>(socket));
    }

    SocketHandle CreateSocket(AddressFamily family,
                              int type,
                              int protocol,
                              bool dualStack,
                              NetError& error) noexcept
    {
        if (!EnsureInitialized())
        {
            error = NetError {NetErrc::Unknown, 0};
            return {};
        }

        int af = AF_INET;
        if (family == AddressFamily::V6 || family == AddressFamily::DualStack)
        {
            af = AF_INET6;
        }

        const NativeSocket sock =
#if defined(NGIN_PLATFORM_WINDOWS)
                ::WSASocketW(af, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
#else
                ::socket(af, type, protocol);
#endif
        if (sock == InvalidNativeSocket)
        {
            error = LastError();
            return {};
        }

        SocketHandle handle = FromNative(sock);
        if (!SetNonBlocking(handle, true))
        {
            error = LastError();
            static_cast<void>(CloseSocket(handle));
            return {};
        }

        if (af == AF_INET6 && dualStack)
        {
            (void)SetV6Only(handle, false);
        }

        error = NetError {NetErrc::Ok, 0};
        return handle;
    }

    bool SetNonBlocking(SocketHandle& handle, bool value) noexcept
    {
        const NativeSocket sock = ToNative(handle);
#if defined(NGIN_PLATFORM_WINDOWS)
        u_long mode = value ? 1UL : 0UL;
        return ::ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
        const int flags = ::fcntl(sock, F_GETFL, 0);
        if (flags < 0)
        {
            return false;
        }
        const int newFlags = value ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
        return ::fcntl(sock, F_SETFL, newFlags) == 0;
#endif
    }

    bool SetReuseAddress(SocketHandle& handle, bool value) noexcept
    {
        const NativeSocket sock = ToNative(handle);
        const int opt = value ? 1 : 0;
        return ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
    }

    bool SetReusePort(SocketHandle& handle, bool value) noexcept
    {
#if defined(SO_REUSEPORT)
        const NativeSocket sock = ToNative(handle);
        const int opt = value ? 1 : 0;
        return ::setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
#else
        (void)handle;
        (void)value;
        return false;
#endif
    }

    bool SetNoDelay(SocketHandle& handle, bool value) noexcept
    {
        const NativeSocket sock = ToNative(handle);
        const int opt = value ? 1 : 0;
        return ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
    }

    bool SetBroadcast(SocketHandle& handle, bool value) noexcept
    {
        const NativeSocket sock = ToNative(handle);
        const int opt = value ? 1 : 0;
        return ::setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
    }

    bool SetV6Only(SocketHandle& handle, bool value) noexcept
    {
        const NativeSocket sock = ToNative(handle);
        const int opt = value ? 1 : 0;
        return ::setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
    }

    NetExpected<void> Shutdown(SocketHandle& handle, ShutdownMode mode) noexcept
    {
        const NativeSocket sock = ToNative(handle);
#if defined(NGIN_PLATFORM_WINDOWS)
        int how = SD_BOTH;
        if (mode == ShutdownMode::Receive)
        {
            how = SD_RECEIVE;
        }
        else if (mode == ShutdownMode::Send)
        {
            how = SD_SEND;
        }
#else
        int how = SHUT_RDWR;
        if (mode == ShutdownMode::Receive)
        {
            how = SHUT_RD;
        }
        else if (mode == ShutdownMode::Send)
        {
            how = SHUT_WR;
        }
#endif
        if (::shutdown(sock, how) == 0)
        {
            return {};
        }
        return std::unexpected(LastError());
    }

    bool CloseSocket(SocketHandle& handle) noexcept
    {
        const NativeSocket sock = ToNative(handle);
        if (sock == InvalidNativeSocket)
        {
            return true;
        }
#if defined(NGIN_PLATFORM_WINDOWS)
        const int result = ::closesocket(sock);
#else
        const int result = ::close(sock);
#endif
        handle = SocketHandle();
        return result == 0;
    }

    bool ToSockAddr(const Endpoint& endpoint, sockaddr_storage& storage, socklen_t& length) noexcept
    {
        std::memset(&storage, 0, sizeof(storage));
        if (endpoint.address.IsV4())
        {
            sockaddr_in addr {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(endpoint.port);
            auto bytes = endpoint.address.Bytes();
            std::memcpy(&addr.sin_addr, bytes.data(), bytes.size());
            std::memcpy(&storage, &addr, sizeof(addr));
            length = static_cast<socklen_t>(sizeof(sockaddr_in));
            return true;
        }

        sockaddr_in6 addr6 {};
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons(endpoint.port);
        auto bytes = endpoint.address.Bytes();
        std::memcpy(&addr6.sin6_addr, bytes.data(), bytes.size());
        std::memcpy(&storage, &addr6, sizeof(addr6));
        length = static_cast<socklen_t>(sizeof(sockaddr_in6));
        return true;
    }

    Endpoint FromSockAddr(const sockaddr_storage& storage, socklen_t length) noexcept
    {
        Endpoint endpoint {};
        if (length >= sizeof(sockaddr_in) && storage.ss_family == AF_INET)
        {
            sockaddr_in addr {};
            std::memcpy(&addr, &storage, sizeof(addr));
            std::array<NGIN::Byte, IpAddress::V6Size> bytes {};
            std::memcpy(bytes.data(), &addr.sin_addr, IpAddress::V4Size);
            endpoint.address = IpAddress(AddressFamily::V4, bytes);
            endpoint.port = ntohs(addr.sin_port);
            return endpoint;
        }

        sockaddr_in6 addr6 {};
        std::memcpy(&addr6, &storage, sizeof(addr6));
        std::array<NGIN::Byte, IpAddress::V6Size> bytes {};
        std::memcpy(bytes.data(), &addr6.sin6_addr, IpAddress::V6Size);
        endpoint.address = IpAddress(AddressFamily::V6, bytes);
        endpoint.port = ntohs(addr6.sin6_port);
        return endpoint;
    }

    NetExpected<void> CheckConnectResult(SocketHandle& handle) noexcept
    {
        const NativeSocket sock = ToNative(handle);
        int error = 0;
        socklen_t len = static_cast<socklen_t>(sizeof(error));
        if (::getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &len) != 0)
        {
            return std::unexpected(LastError());
        }
        if (error == 0)
        {
            return {};
        }
        return std::unexpected(MapError(error));
    }
}

namespace NGIN::Net
{
    void SocketHandle::Close() noexcept
    {
        (void)detail::CloseSocket(*this);
    }
}
