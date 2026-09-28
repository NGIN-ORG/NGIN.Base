#include <NGIN/Async/ExecutorTransitions.hpp>
#include <NGIN/Net/Resolve.hpp>

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
            NGIN::Async::StopReason                                      stopReason {NGIN::Async::StopReason::Requested};
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

            bool await_suspend(std::coroutine_handle<> awaiting)
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
                auto delivery         = resumeExecutor.ReserveCompletion(NGIN::Execution::WorkItem(awaiting));
                if (!delivery)
                {
                    state->completion.status = AsyncResolveStatus::Fault;
                    state->completion.fault  = NGIN::Async::MakeAsyncFault(
                            NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed, static_cast<int>(delivery.error()));
                    return false;
                }
                state->delivery = std::move(*delivery);
                struct CommitSetup final
                {
                    std::shared_ptr<State> state;
                    ~CommitSetup() { state->Signal(1U); }
                } commit {state};
                try
                {
                    if (!driverExecutor.IsValid() || !resumeExecutor.IsValid())
                    {
                        state->CompleteFault(
                                NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::InvalidTaskUsage));
                        return true;
                    }
                    if (contextCancellation.IsCancellationRequested() || cancellation.IsCancellationRequested())
                    {
                        state->Complete(AsyncResolveStatus::Canceled, contextCancellation.IsCancellationRequested() ? contextCancellation.GetStopReason() : cancellation.GetStopReason());
                        return true;
                    }
                    if (options.timeout && options.timeout->count() <= 0)
                    {
                        state->Complete(AsyncResolveStatus::TimedOut);
                        return true;
                    }

                    const NGIN::Async::CancellationRegistrationResult contextRegistrationResult =
                            RegisterCancellation(contextCancellation, state->contextRegistration, state.get());
                    if (!contextRegistrationResult)
                    {
                        state->CompleteFault(NGIN::Async::MakeAsyncFault(
                                NGIN::Async::AsyncFaultCode::CancellationRegistrationFailed,
                                static_cast<int>(contextRegistrationResult.error())));
                        return true;
                    }
                    if (state->done.load(std::memory_order_acquire))
                    {
                        return true;
                    }
                    const NGIN::Async::CancellationRegistrationResult explicitRegistrationResult =
                            RegisterCancellation(cancellation, state->explicitRegistration, state.get());
                    if (!explicitRegistrationResult)
                    {
                        state->CompleteFault(NGIN::Async::MakeAsyncFault(
                                NGIN::Async::AsyncFaultCode::CancellationRegistrationFailed,
                                static_cast<int>(explicitRegistrationResult.error())));
                        return true;
                    }
                    if (state->done.load(std::memory_order_acquire))
                    {
                        return true;
                    }

                    const std::optional<std::chrono::milliseconds> timeout = options.timeout;
                    if (timeout)
                    {
                        const NGIN::UInt64 milliseconds  = static_cast<NGIN::UInt64>(timeout->count());
                        const NGIN::UInt64 now           = NGIN::Time::MonotonicClock::Now().ToNanoseconds();
                        const NGIN::UInt64 maximumAdd    = (std::numeric_limits<NGIN::UInt64>::max)() - now;
                        const NGIN::UInt64 add           = milliseconds > maximumAdd / 1'000'000ull
                                                                   ? maximumAdd
                                                                   : milliseconds * 1'000'000ull;
                        auto               timeoutResult = resumeExecutor.ScheduleTimer(
                                NGIN::Execution::WorkItem([state] { state->Complete(AsyncResolveStatus::TimedOut); }),
                                NGIN::Time::TimePoint::FromNanoseconds(now + add));
                        if (!timeoutResult)
                        {
                            state->CompleteFault(NGIN::Async::MakeAsyncFault(
                                    NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed,
                                    static_cast<int>(timeoutResult.error())));
                            return true;
                        }
                        state->timer = std::move(*timeoutResult);
                    }
                    if (state->done.load(std::memory_order_acquire))
                        return true;

                    const NGIN::Execution::ScheduleResult dispatchResult = driverExecutor.Execute(
                            [state, queued = QueuedLookup(state), host = std::move(host), service = std::move(service), options = std::move(options)]() mutable {
                                queued.Started();
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
                    if (!dispatchResult)
                    {
                        state->CompleteFault(NGIN::Async::MakeAsyncFault(
                                NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed,
                                static_cast<int>(dispatchResult.error())));
                        return true;
                    }

                    return true;
                } catch (...)
                {
                    state->CompleteFault(NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::UnhandledException));
                    return true;
                }
            }

            [[nodiscard]] AsyncResolveCompletion await_resume() noexcept
            {
                return std::move(m_state->completion);
            }

        private:
            struct State final : std::enable_shared_from_this<State>
            {
                struct CancellationRequest
                {
                    State*                                state {};
                    NGIN::Async::CancellationToken        token;
                    NGIN::Async::CancellationRegistration registration;
                };

                std::atomic<bool>                      done {false};
                std::atomic<unsigned>                  publication {0};
                NGIN::Execution::CompletionReservation delivery;
                NGIN::Execution::TimerRegistration     timer;
                NGIN::Execution::ExecutorRef           resumeExecutor {};
                std::coroutine_handle<>                awaiting {};
                CancellationRequest                    contextRegistration {};
                CancellationRequest                    explicitRegistration {};
                AsyncResolveCompletion                 completion {};

                void Signal(unsigned flag) noexcept
                {
                    const unsigned previous = publication.fetch_or(flag, std::memory_order_acq_rel);
                    const unsigned observed = previous | flag;
                    // Discard before setup commits waits for the submission result so
                    // an admission failure preserves its specific scheduling error.
                    if ((observed & 5U) == 5U && !(observed & 2U))
                        CompleteFault(NGIN::Async::MakeAsyncFault(
                                NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed,
                                static_cast<int>(NGIN::Execution::ScheduleError::Stopped)));
                    if ((flag & 3U) && (observed & 3U) == 3U && !(previous & flag))
                        Deliver();
                }

                void Resume() noexcept { Signal(2U); }

                void Deliver() noexcept
                {
                    auto keepAlive = shared_from_this();
                    contextRegistration.registration.Reset();
                    explicitRegistration.registration.Reset();
                    timer.Cancel();
                    auto                      ticket = std::move(delivery);
                    NGIN::Execution::WorkItem work(awaiting);
                    if (!resumeExecutor.TryDispatchInline(work))
                        ticket.Dispatch();
                }

                void Complete(AsyncResolveStatus status, NGIN::Async::StopReason reason = NGIN::Async::StopReason::Requested) noexcept
                {
                    bool expected = false;
                    if (!done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                    {
                        return;
                    }
                    completion.status     = status;
                    completion.stopReason = reason;
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

            struct QueuedLookup final
            {
                explicit QueuedLookup(std::shared_ptr<State> value) noexcept : state(std::move(value)) {}
                QueuedLookup(QueuedLookup&&) noexcept = default;
                QueuedLookup(const QueuedLookup&)     = delete;
                ~QueuedLookup()
                {
                    if (state)
                        state->Signal(4U);
                }
                void                   Started() noexcept { state.reset(); }
                std::shared_ptr<State> state;
            };

            [[nodiscard]] static NGIN::Async::CancellationRegistrationResult RegisterCancellation(
                    const NGIN::Async::CancellationToken& token,
                    State::CancellationRequest&           request,
                    State*                                state)
            {
                request.state = state;
                request.token = token;
                return token.Register(
                        request.registration,
                        {},
                        {},
                        +[](void* rawState) noexcept -> bool {
                            auto& triggered = *static_cast<State::CancellationRequest*>(rawState);
                            triggered.state->Complete(AsyncResolveStatus::Canceled, triggered.token.GetStopReason());
                            return false;
                        },
                        &request);
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

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 15
// GCC 15's coroutine lowering loses the engagement proof after the explicit
// has_value() check below and diagnoses std::vector's move as uninitialized.
// Keep the suppression local to the affected coroutine and compiler release.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    NGIN::Async::Task<std::vector<ResolvedAddress>, ResolveError> ResolveAsync(
            NGIN::Async::TaskContext&      context,
            ResolverDriver&                driver,
            std::string                    host,
            std::string                    service,
            ResolveOptions                 options,
            NGIN::Async::CancellationToken cancellation)
    {
        co_await NGIN::Async::SwitchTo(context.GetExecutor());
        AsyncResolveCompletion completion = co_await ResolveAwaiter(
                context,
                driver,
                std::move(host),
                std::move(service),
                std::move(options),
                std::move(cancellation));
        switch (completion.status)
        {
            case AsyncResolveStatus::Canceled:
                co_await NGIN::Async::Stopped(completion.stopReason);
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

        ResolveExpected<std::vector<ResolvedAddress>> result = std::move(*completion.result);
        if (!result.has_value())
        {
            co_return NGIN::Utilities::Unexpected<ResolveError>(std::move(result).error());
        }
        co_return std::move(result);
    }
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 15
#pragma GCC diagnostic pop
#endif
}// namespace NGIN::Net
