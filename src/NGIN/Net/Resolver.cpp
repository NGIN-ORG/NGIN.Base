#include <NGIN/Net/Resolver.hpp>

#include "SocketPlatform.hpp"

#include <NGIN/Async/AsyncFault.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/TimePoint.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <coroutine>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <utility>

#if defined(NGIN_PLATFORM_WINDOWS)
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#endif

namespace NGIN::Net
{
    namespace
    {
        class AddressInfo final
        {
        public:
            AddressInfo() = default;
            ~AddressInfo()
            {
                if (m_value)
                {
                    ::freeaddrinfo(m_value);
                }
            }

            AddressInfo(const AddressInfo&)            = delete;
            AddressInfo& operator=(const AddressInfo&) = delete;

            [[nodiscard]] addrinfo** Put() noexcept { return &m_value; }
            [[nodiscard]] addrinfo*  Get() const noexcept { return m_value; }

        private:
            addrinfo* m_value {nullptr};
        };

        [[nodiscard]] int NativeFamily(AddressFamily family) noexcept
        {
            switch (family)
            {
                case AddressFamily::V4:
                    return AF_INET;
                case AddressFamily::V6:
                    return AF_INET6;
                case AddressFamily::DualStack:
                    return AF_UNSPEC;
            }
            return AF_UNSPEC;
        }

        [[nodiscard]] int NativeSocketType(ResolveSocketType type) noexcept
        {
            switch (type)
            {
                case ResolveSocketType::Any:
                    return 0;
                case ResolveSocketType::Stream:
                    return SOCK_STREAM;
                case ResolveSocketType::Datagram:
                    return SOCK_DGRAM;
            }
            return 0;
        }

        [[nodiscard]] ResolveSocketType FromNativeSocketType(int type) noexcept
        {
            switch (type)
            {
                case SOCK_STREAM:
                    return ResolveSocketType::Stream;
                case SOCK_DGRAM:
                    return ResolveSocketType::Datagram;
                default:
                    return ResolveSocketType::Any;
            }
        }

        [[nodiscard]] ResolveError MakeResolveError(NetErrorCode code, int resolverCode, std::string diagnostic)
        {
            ResolveError error;
            error.network      = NetError {code};
            error.resolverCode = resolverCode;
            error.diagnostic   = std::move(diagnostic);
            return error;
        }

        [[nodiscard]] ResolveError MapResolveError(int code)
        {
            NetErrorCode networkCode = NetErrorCode::Unknown;
            switch (code)
            {
#if defined(EAI_AGAIN)
                case EAI_AGAIN:
                    networkCode = NetErrorCode::WouldBlock;
                    break;
#endif
#if defined(EAI_NONAME)
                case EAI_NONAME:
                    networkCode = NetErrorCode::NameNotFound;
                    break;
#endif
#if defined(EAI_SERVICE)
                case EAI_SERVICE:
                    networkCode = NetErrorCode::ServiceNotFound;
                    break;
#endif
#if defined(EAI_FAMILY)
                case EAI_FAMILY:
                    networkCode = NetErrorCode::AddressFamilyNotSupported;
                    break;
#endif
#if defined(EAI_BADFLAGS)
                case EAI_BADFLAGS:
                    networkCode = NetErrorCode::InvalidArgument;
                    break;
#endif
                default:
                    break;
            }

            ResolveError error;
            error.network      = NetError {networkCode};
            error.resolverCode = code;
#if defined(NGIN_PLATFORM_WINDOWS)
            if (const char* message = ::gai_strerrorA(code))
#else
            if (const char* message = ::gai_strerror(code))
#endif
            {
                error.diagnostic = message;
            }
            if (error.diagnostic.empty())
            {
                error.diagnostic = "name resolution failed";
            }
            return error;
        }

        enum class AsyncResolveStatus : NGIN::UInt8
        {
            Result,
            Canceled,
            TimedOut,
            Fault,
        };

        struct AsyncResolveCompletion final
        {
            AsyncResolveStatus                                           status {AsyncResolveStatus::Fault};
            std::optional<ResolveExpected<std::vector<ResolvedAddress>>> result {};
            std::optional<NGIN::Async::AsyncFault>                       fault {};
        };

        class ResolveAwaiter final
        {
        public:
            ResolveAwaiter(
                    NGIN::Async::TaskContext&      context,
                    ResolverDriver&                driver,
                    std::string                    host,
                    std::string                    service,
                    ResolveOptions                 options,
                    NGIN::Async::CancellationToken cancellation)
                : m_driver(driver), m_resumeExecutor(context.GetExecutor()), m_contextCancellation(context.GetCancellationToken()), m_cancellation(std::move(cancellation)), m_host(std::move(host)), m_service(std::move(service)), m_options(std::move(options)), m_state(std::make_shared<State>())
            {
            }

            [[nodiscard]] bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> awaiting)
            {
                auto state               = m_state;
                auto driverExecutor      = m_driver.GetExecutor();
                auto resumeExecutor      = m_resumeExecutor;
                auto contextCancellation = m_contextCancellation;
                auto cancellation        = m_cancellation;
                auto host                = std::move(m_host);
                auto service             = std::move(m_service);
                auto options             = std::move(m_options);

                state->resumeExecutor = resumeExecutor;
                state->awaiting       = awaiting;
                if (!driverExecutor.IsValid() || !resumeExecutor.IsValid())
                {
                    state->CompleteFault(
                            NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::InvalidTaskUsage));
                    return;
                }
                if (contextCancellation.IsCancellationRequested() || cancellation.IsCancellationRequested())
                {
                    state->Complete(AsyncResolveStatus::Canceled);
                    return;
                }
                if (options.timeout && options.timeout->count() <= 0)
                {
                    state->Complete(AsyncResolveStatus::TimedOut);
                    return;
                }

                RegisterCancellation(contextCancellation, state->contextRegistration, state.get());
                if (state->done.load(std::memory_order_acquire))
                {
                    return;
                }
                RegisterCancellation(cancellation, state->explicitRegistration, state.get());
                if (state->done.load(std::memory_order_acquire))
                {
                    return;
                }

                const auto timeout = options.timeout;
                driverExecutor.Execute(
                        [state, host = std::move(host), service = std::move(service), options = std::move(options)]() mutable {
                            if (state->done.load(std::memory_order_acquire))
                            {
                                return;
                            }
                            try
                            {
                                auto value = Resolve(host, service, options);
                                state->CompleteResult(std::move(value));
                            } catch (...)
                            {
                                state->CompleteFault(
                                        NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure));
                            }
                        });

                if (timeout)
                {
                    const auto milliseconds = static_cast<NGIN::UInt64>(timeout->count());
                    const auto now          = NGIN::Time::MonotonicClock::Now().ToNanoseconds();
                    const auto maximumAdd   = (std::numeric_limits<NGIN::UInt64>::max)() - now;
                    const auto add          = milliseconds > maximumAdd / 1'000'000ull
                                                      ? maximumAdd
                                                      : milliseconds * 1'000'000ull;
                    resumeExecutor.ExecuteAt(
                            [state] { state->Complete(AsyncResolveStatus::TimedOut); },
                            NGIN::Time::TimePoint::FromNanoseconds(now + add));
                }
            }

            [[nodiscard]] AsyncResolveCompletion await_resume() noexcept
            {
                return std::move(m_state->completion);
            }

        private:
            struct State final
            {
                std::atomic<bool>                     done {false};
                NGIN::Execution::ExecutorRef          resumeExecutor {};
                std::coroutine_handle<>               awaiting {};
                NGIN::Async::CancellationRegistration contextRegistration {};
                NGIN::Async::CancellationRegistration explicitRegistration {};
                AsyncResolveCompletion                completion {};

                void Resume() noexcept
                {
                    contextRegistration.Reset();
                    explicitRegistration.Reset();
                    if (resumeExecutor.IsValid())
                    {
                        resumeExecutor.Execute(awaiting);
                    }
                    else
                    {
                        awaiting.resume();
                    }
                }

                void Complete(AsyncResolveStatus status) noexcept
                {
                    bool expected = false;
                    if (!done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                    {
                        return;
                    }
                    completion.status = status;
                    Resume();
                }

                void CompleteResult(ResolveExpected<std::vector<ResolvedAddress>> result)
                {
                    bool expected = false;
                    if (!done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                    {
                        return;
                    }
                    completion.status = AsyncResolveStatus::Result;
                    completion.result.emplace(std::move(result));
                    Resume();
                }

                void CompleteFault(NGIN::Async::AsyncFault fault) noexcept
                {
                    bool expected = false;
                    if (!done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                    {
                        return;
                    }
                    completion.status = AsyncResolveStatus::Fault;
                    completion.fault.emplace(std::move(fault));
                    Resume();
                }
            };

            static void RegisterCancellation(
                    const NGIN::Async::CancellationToken&  token,
                    NGIN::Async::CancellationRegistration& registration,
                    State*                                 state)
            {
                token.Register(
                        registration,
                        {},
                        {},
                        +[](void* rawState) noexcept -> bool {
                            static_cast<State*>(rawState)->Complete(AsyncResolveStatus::Canceled);
                            return false;
                        },
                        state);
            }

            ResolverDriver&                m_driver;
            NGIN::Execution::ExecutorRef   m_resumeExecutor {};
            NGIN::Async::CancellationToken m_contextCancellation {};
            NGIN::Async::CancellationToken m_cancellation {};
            std::string                    m_host {};
            std::string                    m_service {};
            ResolveOptions                 m_options {};
            std::shared_ptr<State>         m_state {};
        };
    }// namespace

    ResolveExpected<std::vector<ResolvedAddress>> Resolve(
            std::string_view      host,
            std::string_view      service,
            const ResolveOptions& options)
    {
        if (host.find('\0') != std::string_view::npos || service.find('\0') != std::string_view::npos ||
            (host.empty() && service.empty()))
        {
            return NGIN::Utilities::Unexpected<ResolveError>(
                    MakeResolveError(NetErrorCode::InvalidArgument, 0, "resolver host and service are invalid"));
        }
        if (options.timeout && options.timeout->count() < 0)
        {
            return NGIN::Utilities::Unexpected<ResolveError>(
                    MakeResolveError(NetErrorCode::InvalidArgument, 0, "resolver timeout must not be negative"));
        }
#if defined(NGIN_PLATFORM_WINDOWS)
        if (!detail::EnsureInitialized())
        {
            return NGIN::Utilities::Unexpected<ResolveError>(
                    MakeResolveError(NetErrorCode::Unknown, ::WSAGetLastError(), "failed to initialize Winsock"));
        }
#endif

        const std::string hostStorage {host};
        const std::string serviceStorage {service};
        addrinfo          hints {};
        hints.ai_family   = NativeFamily(options.family);
        hints.ai_socktype = NativeSocketType(options.socketType);
        hints.ai_flags    = (options.passive ? AI_PASSIVE : 0) |
                            (options.numericHost ? AI_NUMERICHOST : 0) |
                            (options.numericService ? AI_NUMERICSERV : 0) |
                            (options.requestCanonicalName ? AI_CANONNAME : 0);

        AddressInfo addresses;
        const int   result = ::getaddrinfo(
                host.empty() ? nullptr : hostStorage.c_str(),
                service.empty() ? nullptr : serviceStorage.c_str(),
                &hints,
                addresses.Put());
        if (result != 0)
        {
            return NGIN::Utilities::Unexpected<ResolveError>(MapResolveError(result));
        }

        std::vector<ResolvedAddress> resolved;
        for (const addrinfo* current = addresses.Get(); current; current = current->ai_next)
        {
            if (!current->ai_addr ||
                (current->ai_family != AF_INET && current->ai_family != AF_INET6) ||
                current->ai_addrlen > static_cast<decltype(current->ai_addrlen)>(sizeof(sockaddr_storage)))
            {
                continue;
            }
            sockaddr_storage storage {};
            std::memcpy(&storage, current->ai_addr, current->ai_addrlen);
            ResolvedAddress item;
            item.endpoint   = detail::FromSockAddr(storage, static_cast<socklen_t>(current->ai_addrlen));
            item.socketType = FromNativeSocketType(current->ai_socktype);
            item.protocol   = current->ai_protocol;
            if (current->ai_canonname)
            {
                item.canonicalName = current->ai_canonname;
            }
            if (std::find(resolved.begin(), resolved.end(), item) == resolved.end())
            {
                resolved.push_back(std::move(item));
            }
        }
        if (resolved.empty())
        {
            return NGIN::Utilities::Unexpected<ResolveError>(
                    MakeResolveError(NetErrorCode::NameNotFound, 0, "resolver returned no usable addresses"));
        }
        return resolved;
    }

    ResolverDriver::ResolverDriver()
        : ResolverDriver(Options {})
    {
    }

    ResolverDriver::ResolverDriver(Options options)
        : m_options(options), m_scheduler(std::make_shared<NGIN::Execution::ThreadPoolScheduler>(
                                      static_cast<std::size_t>(options.workerThreads == 0 ? 1 : options.workerThreads)))
    {
    }

    ResolverDriver::~ResolverDriver() = default;

    NGIN::Execution::ExecutorRef ResolverDriver::GetExecutor() noexcept
    {
        return m_scheduler ? NGIN::Execution::ExecutorRef::From(*m_scheduler) : NGIN::Execution::ExecutorRef {};
    }

    bool ResolverDriver::HasBackend() const noexcept
    {
        return static_cast<bool>(m_scheduler);
    }

    NGIN::Async::Task<std::vector<ResolvedAddress>, ResolveError> ResolveAsync(
            NGIN::Async::TaskContext&      context,
            ResolverDriver&                driver,
            std::string                    host,
            std::string                    service,
            ResolveOptions                 options,
            NGIN::Async::CancellationToken cancellation)
    {
        auto completion = co_await ResolveAwaiter(
                context,
                driver,
                std::move(host),
                std::move(service),
                std::move(options),
                std::move(cancellation));
        switch (completion.status)
        {
            case AsyncResolveStatus::Canceled:
                co_await NGIN::Async::Canceled();
                co_return std::vector<ResolvedAddress> {};
            case AsyncResolveStatus::TimedOut:
                co_return NGIN::Utilities::Unexpected<ResolveError>(
                        MakeResolveError(NetErrorCode::TimedOut, 0, "name resolution timed out"));
            case AsyncResolveStatus::Fault:
                co_await NGIN::Async::Faulted(completion.fault.value_or(
                        NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::UnknownRuntimeFailure)));
                co_return std::vector<ResolvedAddress> {};
            case AsyncResolveStatus::Result:
                break;
        }

        auto result = std::move(*completion.result);
        if (!result.HasValue())
        {
            co_return NGIN::Utilities::Unexpected<ResolveError>(std::move(result).TakeError());
        }
        co_return std::move(result).TakeValue();
    }
}// namespace NGIN::Net
