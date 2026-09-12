#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>
#include <NGIN/IO/Runtime.hpp>
#include <NGIN/IO/RuntimeRunner.hpp>
#include <NGIN/Net/Sockets/UdpSocket.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#if defined(__linux__)
#include <sys/resource.h>
#endif

namespace
{
    using Clock = std::chrono::steady_clock;

    double Microseconds(Clock::duration elapsed)
    {
        return std::chrono::duration<double, std::micro>(elapsed).count();
    }

    void Report(const char* name, std::vector<double> samples)
    {
        std::sort(samples.begin(), samples.end());
        const auto percentile = [&](std::size_t percent) { return samples[(samples.size() - 1) * percent / 100]; };
        double     sum        = 0;
        for (double value: samples)
            sum += value;
        std::cout << name << " samples=" << samples.size() << " p50_us=" << percentile(50)
                  << " p95_us=" << percentile(95) << " p99_us=" << percentile(99)
                  << " mean_us=" << sum / static_cast<double>(samples.size()) << '\n';
    }

    template<typename Completion>
    void RequireSuccess(const Completion& result)
    {
        if (!result.Succeeded())
            throw std::runtime_error("benchmark operation failed");
    }

    NGIN::Async::Task<void> Yield(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.YieldNow();
    }

    NGIN::Async::Task<void, NGIN::Net::NetError> Exchange(
            NGIN::Async::TaskContext& ctx, NGIN::Net::UdpSocket& receiver,
            NGIN::Net::UdpSocket& sender, NGIN::Net::Endpoint endpoint)
    {
        std::array<NGIN::Byte, 64> buffer {};
        (void) co_await sender.SendToAsync(ctx, endpoint, buffer);
        (void) co_await receiver.ReceiveFromAsync(ctx, buffer);
    }

#if defined(__linux__)
    void ReportIdle(const char* name)
    {
        rusage before {}, after {};
        ::getrusage(RUSAGE_SELF, &before);
        const auto start = Clock::now();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ::getrusage(RUSAGE_SELF, &after);
        const auto cpu = [](const rusage& usage) {
            return static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1e6 +
                   static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec);
        };
        std::size_t threads = 0;
        for ([[maybe_unused]] const auto& entry: std::filesystem::directory_iterator("/proc/self/task"))
            ++threads;
        std::cout << name << " wall_us=" << Microseconds(Clock::now() - start)
                  << " cpu_us=" << cpu(after) - cpu(before) << " threads=" << threads
                  << " voluntary_switches=" << after.ru_nvcsw - before.ru_nvcsw
                  << " maxrss_kib=" << after.ru_maxrss << '\n';
    }
#endif
}// namespace

int main()
{
    try
    {
        constexpr std::size_t iterations = 2000;
        std::cout << "IORuntime baseline workload: 2000 sequential operations; 64-byte UDP; one continuation worker\n";
        NGIN::Execution::ThreadPoolScheduler scheduler(1);
        NGIN::Async::TaskContext             ctx(scheduler);
        std::vector<double>                  samples;
        samples.reserve(iterations);
        for (std::size_t i = 0; i < iterations; ++i)
        {
            const auto start = Clock::now();
            RequireSuccess(NGIN::Async::SyncWait(ctx, Yield(ctx)));
            samples.push_back(Microseconds(Clock::now() - start));
        }
        Report("spawn_yield_join", samples);

        const auto           coldStart = Clock::now();
        NGIN::IO::Runtime    runtime;
        NGIN::IO::RuntimeRunner runner(runtime);
        NGIN::Net::UdpSocket receiver(runtime), sender(runtime);
        if (!receiver.Open(NGIN::Net::AddressFamily::V4) ||
            !receiver.Bind({NGIN::Net::IpAddress::LoopbackV4(), 0}) ||
            !sender.Open(NGIN::Net::AddressFamily::V4))
            throw std::runtime_error("socket setup failed");
        sockaddr_in address {};
#if defined(_WIN32)
        int          addressSize  = sizeof(address);
        const SOCKET nativeSocket = static_cast<SOCKET>(receiver.Handle().Native());
#else
        socklen_t addressSize  = sizeof(address);
        const int nativeSocket = static_cast<int>(receiver.Handle().Native());
#endif
        if (::getsockname(nativeSocket, reinterpret_cast<sockaddr*>(&address), &addressSize) != 0)
            throw std::runtime_error("endpoint lookup failed");
        const NGIN::Net::Endpoint endpoint {NGIN::Net::IpAddress::LoopbackV4(), ntohs(address.sin_port)};
        RequireSuccess(NGIN::Async::SyncWait(ctx, Exchange(ctx, receiver, sender, endpoint)));
        std::cout << "network_cold_us=" << Microseconds(Clock::now() - coldStart) << '\n';
        samples.clear();
        for (std::size_t i = 0; i < iterations; ++i)
        {
            const auto start = Clock::now();
            RequireSuccess(NGIN::Async::SyncWait(ctx, Exchange(ctx, receiver, sender, endpoint)));
            samples.push_back(Microseconds(Clock::now() - start));
        }
        Report("udp_ready_exchange", samples);
#if defined(__linux__)
        ReportIdle("network_idle");
#endif
        NGIN::Async::CancellationSource cancellation;
        NGIN::Async::TaskContext        cancelContext(scheduler, cancellation.GetToken());
        std::array<NGIN::Byte, 64>      pendingBuffer {};
        auto                            pending = NGIN::Async::Spawn(cancelContext, receiver.ReceiveFromAsync(cancelContext, pendingBuffer));
#if defined(__linux__)
        ReportIdle("network_pending_idle");
#endif
        const auto cancelStart = Clock::now();
        cancellation.Cancel();
        auto join = [](NGIN::Async::Operation<NGIN::Net::DatagramReceiveResult, NGIN::Net::NetError>& operation)
                -> NGIN::Async::Task<void> { (void) co_await operation; };
        RequireSuccess(NGIN::Async::SyncWait(ctx, join(pending)));
        if (!pending.IsCanceled())
            throw std::runtime_error("pending receive did not cancel");
        std::cout << "network_cancel_us=" << Microseconds(Clock::now() - cancelStart) << '\n';
        const auto stopStart = Clock::now();
        runner.Shutdown();
        std::cout << "network_stop_us=" << Microseconds(Clock::now() - stopStart) << '\n';

        for (const auto preference: {NGIN::IO::Runtime::FileBackendPreference::Auto,
                                     NGIN::IO::Runtime::FileBackendPreference::Fallback})
        {
            NGIN::IO::Runtime         filesRuntime({.files = {.backendPreference = preference}});
            NGIN::IO::RuntimeRunner filesRunner(filesRuntime);
            NGIN::IO::LocalFileSystem files(filesRuntime);
            const auto                start = Clock::now();
            RequireSuccess(NGIN::Async::SyncWait(ctx, files.GetInfoAsync(ctx, NGIN::IO::Path("."))));
            std::cout << "file_backend=" << static_cast<int>(filesRuntime.GetFileBackend())
                      << " requested=" << static_cast<int>(preference)
                      << " cold_us=" << Microseconds(Clock::now() - start) << '\n';
            samples.clear();
            for (std::size_t i = 0; i < iterations; ++i)
            {
                const auto operationStart = Clock::now();
                RequireSuccess(NGIN::Async::SyncWait(ctx, files.GetInfoAsync(ctx, NGIN::IO::Path("."))));
                samples.push_back(Microseconds(Clock::now() - operationStart));
            }
            Report("file_stat", samples);
#if defined(__linux__)
            ReportIdle("files_idle");
#endif
        }
    } catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
