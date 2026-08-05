/// @file Resolver.hpp
/// @brief Synchronous and explicitly driven asynchronous name resolution.
#pragma once

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Defines.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/Net/Types/AddressFamily.hpp>
#include <NGIN/Net/Types/Endpoint.hpp>
#include <NGIN/Net/Types/NetError.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::Net
{
    enum class ResolveSocketType : NGIN::UInt8
    {
        Any,
        Stream,
        Datagram,
    };

    struct ResolveOptions final
    {
        AddressFamily                            family {AddressFamily::DualStack};
        ResolveSocketType                        socketType {ResolveSocketType::Any};
        bool                                     passive {false};
        bool                                     numericHost {false};
        bool                                     numericService {false};
        bool                                     requestCanonicalName {false};
        std::optional<std::chrono::milliseconds> timeout {};
    };

    struct ResolvedAddress final
    {
        Endpoint          endpoint {};
        ResolveSocketType socketType {ResolveSocketType::Any};
        int               protocol {0};
        std::string       canonicalName {};

        [[nodiscard]] bool operator==(const ResolvedAddress&) const = default;
    };

    struct ResolveError final
    {
        NetError    network {};
        int         resolverCode {0};
        std::string diagnostic {};
    };

    template<typename T>
    using ResolveExpected = NGIN::Utilities::Expected<T, ResolveError>;

    [[nodiscard]] NGIN_BASE_API ResolveExpected<std::vector<ResolvedAddress>> Resolve(
            std::string_view      host,
            std::string_view      service,
            const ResolveOptions& options = {});

    class NGIN_BASE_API ResolverDriver final
    {
    public:
        struct Options final
        {
            NGIN::UInt32 workerThreads {1};
        };

        ResolverDriver();
        explicit ResolverDriver(Options options);
        ~ResolverDriver();

        ResolverDriver(const ResolverDriver&)            = delete;
        ResolverDriver& operator=(const ResolverDriver&) = delete;
        ResolverDriver(ResolverDriver&&)                 = delete;
        ResolverDriver& operator=(ResolverDriver&&)      = delete;

        [[nodiscard]] NGIN::Execution::ExecutorRef GetExecutor() noexcept;
        [[nodiscard]] bool                         HasBackend() const noexcept;

    private:
        Options                                               m_options {};
        std::shared_ptr<NGIN::Execution::ThreadPoolScheduler> m_scheduler {};
    };

    [[nodiscard]] NGIN_BASE_API NGIN::Async::Task<std::vector<ResolvedAddress>, ResolveError> ResolveAsync(
            NGIN::Async::TaskContext&      context,
            ResolverDriver&                driver,
            std::string                    host,
            std::string                    service,
            ResolveOptions                 options      = {},
            NGIN::Async::CancellationToken cancellation = {});
}// namespace NGIN::Net
