#include "SocketPlatform.hpp"

#include <array>
#include <cstring>

namespace NGIN::Net::detail
{
    bool EnsureInitialized() noexcept
    {
        return true;
    }

    NetError MapError(int native) noexcept
    {
        NetErrorCode code = NetErrorCode::Unknown;
        switch (native)
        {
            case EWOULDBLOCK:
#if defined(EAGAIN) && EAGAIN != EWOULDBLOCK
            case EAGAIN:
#endif
                code = NetErrorCode::WouldBlock;
                break;
            case ETIMEDOUT:
                code = NetErrorCode::TimedOut;
                break;
            case ECONNRESET:
                code = NetErrorCode::ConnectionReset;
                break;
            case ECONNABORTED:
                code = NetErrorCode::Disconnected;
                break;
            case ENETUNREACH:
#if defined(EHOSTUNREACH)
            case EHOSTUNREACH:
#endif
                code = NetErrorCode::HostUnreachable;
                break;
            case EMSGSIZE:
                code = NetErrorCode::MessageTooLarge;
                break;
            case EACCES:
#if defined(EPERM)
            case EPERM:
#endif
                code = NetErrorCode::PermissionDenied;
                break;
            case ECONNREFUSED:
                code = NetErrorCode::Disconnected;
                break;
            default:
                break;
        }
        return NetError {code, native};
    }

    NetError LastError() noexcept
    {
        return MapError(errno);
    }

    bool IsWouldBlock(const NetError& error) noexcept
    {
        return error.code == NetErrorCode::WouldBlock;
    }

    bool IsInProgress(const NetError& error) noexcept
    {
        return error.native == EINPROGRESS || error.native == EALREADY;
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
                              int           type,
                              int           protocol,
                              bool          nonBlocking,
                              NetError&     error) noexcept
    {
        int af = AF_INET;
        if (family == AddressFamily::V6 || family == AddressFamily::DualStack)
        {
            af = AF_INET6;
        }

        const NativeSocket sock = ::socket(af, type, protocol);
        if (sock == InvalidNativeSocket)
        {
            error = LastError();
            return {};
        }

        SocketHandle handle = FromNative(sock);
        if (!SetNonBlocking(handle, nonBlocking))
        {
            error = LastError();
            static_cast<void>(CloseSocket(handle));
            return {};
        }

        error = NetError {NetErrorCode::Ok, 0};
        return handle;
    }

    bool SetNonBlocking(SocketHandle& handle, bool value) noexcept
    {
        const NativeSocket sock  = ToNative(handle);
        const int          flags = ::fcntl(sock, F_GETFL, 0);
        if (flags < 0)
        {
            return false;
        }
        const int newFlags = value ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
        return ::fcntl(sock, F_SETFL, newFlags) == 0;
    }

    bool SetReuseAddress(SocketHandle& handle, bool value) noexcept
    {
        const NativeSocket sock = ToNative(handle);
        const int          opt  = value ? 1 : 0;
        return ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
    }

    bool SetReusePort(SocketHandle& handle, bool value) noexcept
    {
#if defined(SO_REUSEPORT)
        const NativeSocket sock = ToNative(handle);
        const int          opt  = value ? 1 : 0;
        return ::setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
#else
        (void) handle;
        (void) value;
        return true;
#endif
    }

    bool SetNoDelay(SocketHandle& handle, bool value) noexcept
    {
        const NativeSocket sock = ToNative(handle);
        const int          opt  = value ? 1 : 0;
        return ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
    }

    bool SetBroadcast(SocketHandle& handle, bool value) noexcept
    {
        const NativeSocket sock = ToNative(handle);
        const int          opt  = value ? 1 : 0;
        return ::setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
    }

    bool SetV6Only(SocketHandle& handle, bool value) noexcept
    {
        const NativeSocket sock = ToNative(handle);
        const int          opt  = value ? 1 : 0;
        return ::setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
    }

    NetExpected<void> ApplySocketOptions(SocketHandle&        handle,
                                         AddressFamily        family,
                                         const SocketOptions& options,
                                         bool                 isTcp,
                                         bool                 isUdp) noexcept
    {
        if (options.reuseAddress && !SetReuseAddress(handle, true))
        {
            return NGIN::Utilities::Unexpected(LastError());
        }
        if (options.reusePort && !SetReusePort(handle, true))
        {
            return NGIN::Utilities::Unexpected(LastError());
        }
        if (isTcp && options.noDelay && !SetNoDelay(handle, true))
        {
            return NGIN::Utilities::Unexpected(LastError());
        }
        if (isUdp && options.broadcast && !SetBroadcast(handle, true))
        {
            return NGIN::Utilities::Unexpected(LastError());
        }

        if (family != AddressFamily::V4)
        {
            bool v6Only = options.v6Only;
            if (family == AddressFamily::V6)
            {
                v6Only = true;
            }

            if (!SetV6Only(handle, v6Only))
            {
                return NGIN::Utilities::Unexpected(LastError());
            }
        }

        return {};
    }

    NetExpected<void> Shutdown(SocketHandle& handle, ShutdownMode mode) noexcept
    {
        const NativeSocket sock = ToNative(handle);
        int                how  = SHUT_RDWR;
        if (mode == ShutdownMode::Receive)
        {
            how = SHUT_RD;
        }
        else if (mode == ShutdownMode::Send)
        {
            how = SHUT_WR;
        }

        if (::shutdown(sock, how) == 0)
        {
            return {};
        }
        return NGIN::Utilities::Unexpected(LastError());
    }

    bool CloseSocket(SocketHandle& handle) noexcept
    {
        const NativeSocket sock = ToNative(handle);
        if (sock == InvalidNativeSocket)
        {
            handle.Reset();
            return true;
        }

        const int result = ::close(sock);
        handle.Reset();
        return result == 0;
    }

    bool ToSockAddr(const Endpoint& endpoint, sockaddr_storage& storage, socklen_t& length) noexcept
    {
        std::memset(&storage, 0, sizeof(storage));
        if (endpoint.address.IsV4())
        {
            sockaddr_in addr {};
            addr.sin_family = AF_INET;
            addr.sin_port   = htons(endpoint.port);
            auto bytes      = endpoint.address.Bytes();
            std::memcpy(&addr.sin_addr, bytes.data(), bytes.size());
            std::memcpy(&storage, &addr, sizeof(addr));
            length = static_cast<socklen_t>(sizeof(sockaddr_in));
            return true;
        }

        sockaddr_in6 addr6 {};
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port   = htons(endpoint.port);
        auto bytes        = endpoint.address.Bytes();
        std::memcpy(&addr6.sin6_addr, bytes.data(), bytes.size());
        std::memcpy(&storage, &addr6, sizeof(addr6));
        length = static_cast<socklen_t>(sizeof(sockaddr_in6));
        return true;
    }

    Endpoint FromSockAddr(const sockaddr_storage& storage, socklen_t length) noexcept
    {
        Endpoint endpoint {};
        if (length >= static_cast<socklen_t>(sizeof(sockaddr_in)) && storage.ss_family == AF_INET)
        {
            sockaddr_in addr {};
            std::memcpy(&addr, &storage, sizeof(addr));
            std::array<NGIN::Byte, IpAddress::V6Size> bytes {};
            std::memcpy(bytes.data(), &addr.sin_addr, IpAddress::V4Size);
            endpoint.address = IpAddress(AddressFamily::V4, bytes);
            endpoint.port    = ntohs(addr.sin_port);
            return endpoint;
        }

        sockaddr_in6 addr6 {};
        std::memcpy(&addr6, &storage, sizeof(addr6));
        std::array<NGIN::Byte, IpAddress::V6Size> bytes {};
        std::memcpy(bytes.data(), &addr6.sin6_addr, IpAddress::V6Size);
        endpoint.address = IpAddress(AddressFamily::V6, bytes);
        endpoint.port    = ntohs(addr6.sin6_port);
        return endpoint;
    }

    NetExpected<void> CheckConnectResult(SocketHandle& handle) noexcept
    {
        const NativeSocket sock  = ToNative(handle);
        int                error = 0;
        socklen_t          len   = static_cast<socklen_t>(sizeof(error));
        if (::getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &len) != 0)
        {
            return NGIN::Utilities::Unexpected(LastError());
        }
        if (error == 0)
        {
            return {};
        }
        return NGIN::Utilities::Unexpected(MapError(error));
    }
}// namespace NGIN::Net::detail

namespace NGIN::Net
{
    void SocketHandle::Close() noexcept
    {
        (void) detail::CloseSocket(*this);
    }
}// namespace NGIN::Net
