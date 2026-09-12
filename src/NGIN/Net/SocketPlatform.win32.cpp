#include "SocketPlatform.hpp"
#include "SocketState.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

namespace NGIN::Net::detail
{
    namespace
    {
        std::once_flag    s_wsaOnce;
        std::atomic<bool> s_wsaOk {false};
    }// namespace

    bool EnsureInitialized() noexcept
    {
        std::call_once(s_wsaOnce, []() {
            WSADATA   data {};
            const int result = ::WSAStartup(MAKEWORD(2, 2), &data);
            s_wsaOk.store(result == 0, std::memory_order_release);
        });
        return s_wsaOk.load(std::memory_order_acquire);
    }

    NetError MapError(int native) noexcept
    {
        NetErrorCode code = NetErrorCode::Unknown;
        switch (native)
        {
            case WSAENOBUFS:
            case ERROR_NOT_ENOUGH_MEMORY:
                code = NetErrorCode::ResourceExhausted;
                break;
            case WSAEINVAL:
                code = NetErrorCode::InvalidArgument;
                break;
            case WSAEAFNOSUPPORT:
                code = NetErrorCode::AddressFamilyNotSupported;
                break;
            case WSAEWOULDBLOCK:
                code = NetErrorCode::WouldBlock;
                break;
            case WSAETIMEDOUT:
                code = NetErrorCode::TimedOut;
                break;
            case WSAECONNRESET:
                code = NetErrorCode::ConnectionReset;
                break;
            case WSAECONNABORTED:
                code = NetErrorCode::Disconnected;
                break;
            case WSAENETUNREACH:
                code = NetErrorCode::HostUnreachable;
                break;
            case WSAEMSGSIZE:
                code = NetErrorCode::MessageTooLarge;
                break;
            case WSAEACCES:
                code = NetErrorCode::PermissionDenied;
                break;
            case WSAECONNREFUSED:
                code = NetErrorCode::Disconnected;
                break;
            case ERROR_SEM_TIMEOUT:
                code = NetErrorCode::TimedOut;
                break;
            case ERROR_NETNAME_DELETED:
                code = NetErrorCode::ConnectionReset;
                break;
            case ERROR_CONNECTION_ABORTED:
                code = NetErrorCode::Disconnected;
                break;
            case ERROR_CONNECTION_REFUSED:
                code = NetErrorCode::Disconnected;
                break;
            case ERROR_NETWORK_UNREACHABLE:
                code = NetErrorCode::HostUnreachable;
                break;
            case ERROR_HOST_UNREACHABLE:
                code = NetErrorCode::HostUnreachable;
                break;
            case ERROR_ACCESS_DENIED:
                code = NetErrorCode::PermissionDenied;
                break;
            default:
                break;
        }
        return NetError {code, native};
    }

    NetError LastError() noexcept
    {
        return MapError(::WSAGetLastError());
    }

    bool IsWouldBlock(const NetError& error) noexcept
    {
        return error.code == NetErrorCode::WouldBlock;
    }

    bool IsInProgress(const NetError& error) noexcept
    {
        return error.native == WSAEINPROGRESS || error.native == WSAEWOULDBLOCK || error.native == WSAEALREADY;
    }

    NativeSocket ToNative(const SocketHandle& handle) noexcept
    {
        return static_cast<NativeSocket>(handle.Native());
    }

    SocketHandle FromNative(NativeSocket socket) noexcept
    {
        try
        {
            return SocketHandle(static_cast<SocketHandle::NativeHandle>(socket));
        } catch (const std::bad_alloc&)
        {
            return {};
        }
    }

    SocketHandle CreateSocket(AddressFamily family,
                              int           type,
                              int           protocol,
                              bool          nonBlocking,
                              NetError&     error) noexcept
    {
        if (!EnsureInitialized())
        {
            error = NetError {NetErrorCode::Unknown, 0};
            return {};
        }

        int af = AF_INET;
        if (family == AddressFamily::V6 || family == AddressFamily::DualStack)
        {
            af = AF_INET6;
        }

        const NativeSocket sock = ::WSASocketW(af, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
        if (sock == InvalidNativeSocket)
        {
            error = LastError();
            return {};
        }

        SocketHandle handle = FromNative(sock);
        if (!handle.IsOpen())
        {
            error = NetError {NetErrorCode::ResourceExhausted};
            return {};
        }
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
        const NativeSocket sock = ToNative(handle);
        u_long             mode = value ? 1UL : 0UL;
        if (::ioctlsocket(sock, static_cast<long>(FIONBIO), &mode) != 0)
            return false;
        if (auto state = SocketHandleAccess::State(handle))
            state->SetNonBlocking(value);
        return true;
    }

    bool SetReuseAddress(SocketHandle& handle, bool value) noexcept
    {
        const NativeSocket sock = ToNative(handle);
        const int          opt  = value ? 1 : 0;
        return ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
    }

    bool SetReusePort(SocketHandle& handle, bool value) noexcept
    {
        (void) handle;
        (void) value;
        return true;
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
        int                how  = SD_BOTH;
        if (mode == ShutdownMode::Receive)
        {
            how = SD_RECEIVE;
        }
        else if (mode == ShutdownMode::Send)
        {
            how = SD_SEND;
        }

        if (::shutdown(sock, how) == 0)
        {
            return {};
        }
        return NGIN::Utilities::Unexpected(LastError());
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

        if (!endpoint.address.IsV6())
        {
            return false;
        }
        sockaddr_in6 addr6 {};
        addr6.sin6_family   = AF_INET6;
        addr6.sin6_port     = htons(endpoint.port);
        addr6.sin6_scope_id = endpoint.scopeId;
        auto bytes          = endpoint.address.Bytes();
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
        endpoint.scopeId = addr6.sin6_scope_id;
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

    AcceptExFn GetAcceptEx(NativeSocket socket) noexcept
    {
        GUID       guid = WSAID_ACCEPTEX;
        AcceptExFn function {};
        DWORD      bytes {};
        if (::WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
                       &function, sizeof(function), &bytes, nullptr, nullptr) != 0)
            return nullptr;
        return function;
    }

    ConnectExFn GetConnectEx(NativeSocket socket) noexcept
    {
        GUID        guid = WSAID_CONNECTEX;
        ConnectExFn function {};
        DWORD       bytes {};
        if (::WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
                       &function, sizeof(function), &bytes, nullptr, nullptr) != 0)
            return nullptr;
        return function;
    }

    bool EnsureBoundForConnectEx(NativeSocket sock) noexcept
    {
        sockaddr_storage storage {};
        int              length = static_cast<int>(sizeof(storage));
        if (::getsockname(sock, reinterpret_cast<sockaddr*>(&storage), &length) != 0)
        {
            const int err = ::WSAGetLastError();
            if (err != WSAEINVAL)
            {
                return false;
            }
            std::memset(&storage, 0, sizeof(storage));
        }

        if (storage.ss_family == AF_INET)
        {
            const auto* addr = reinterpret_cast<const sockaddr_in*>(&storage);
            if (addr->sin_port != 0)
            {
                return true;
            }
        }
        else if (storage.ss_family == AF_INET6)
        {
            const auto* addr = reinterpret_cast<const sockaddr_in6*>(&storage);
            if (addr->sin6_port != 0)
            {
                return true;
            }
        }

        WSAPROTOCOL_INFOW protocol {};
        int               protocolLength = sizeof(protocol);
        if (::getsockopt(sock, SOL_SOCKET, SO_PROTOCOL_INFOW, reinterpret_cast<char*>(&protocol), &protocolLength) != 0)
            return false;
        Endpoint local {};
        if (protocol.iAddressFamily == AF_INET6)
        {
            local.address = IpAddress::AnyV6();
        }
        else
        {
            local.address = IpAddress::AnyV4();
        }
        local.port = 0;

        socklen_t        bindLength = 0;
        sockaddr_storage bindStorage {};
        if (!ToSockAddr(local, bindStorage, bindLength))
        {
            return false;
        }

        return ::bind(sock, reinterpret_cast<sockaddr*>(&bindStorage), bindLength) == 0;
    }

}// namespace NGIN::Net::detail
