#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/Runtime.hpp>
#include <NGIN/IO/RuntimeRunner.hpp>
#include <NGIN/Net/Sockets/UdpSocket.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
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
    using namespace NGIN;
    using Clock   = std::chrono::steady_clock;
    using Receive = Async::Operation<Net::DatagramReceiveResult, Net::NetError>;

    double Microseconds(Clock::duration elapsed)
    {
        return std::chrono::duration<double, std::micro>(elapsed).count();
    }

    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    Net::Endpoint BoundEndpoint(Net::UdpSocket& socket)
    {
        sockaddr_in address {};
#if defined(_WIN32)
        int          length = sizeof(address);
        const SOCKET native = static_cast<SOCKET>(socket.Handle().Native());
#else
        socklen_t length = sizeof(address);
        const int native = static_cast<int>(socket.Handle().Native());
#endif
        Require(::getsockname(native, reinterpret_cast<sockaddr*>(&address), &length) == 0, "endpoint lookup failed");
        return {Net::IpAddress::LoopbackV4(), ntohs(address.sin_port)};
    }

    Async::Task<void> Barrier(Async::TaskContext& context)
    {
        co_await context.YieldNow();
    }

    Async::Task<Net::DatagramReceiveResult, Net::NetError> Join(Async::TaskContext&, Receive& operation)
    {
        co_return co_await operation;
    }

    struct Receiver final
    {
        explicit Receiver(IO::Runtime& runtime) : socket(runtime)
        {
            // Every receiver needs its own endpoint: UDP address reuse can bind
            // the same ephemeral port and route a packet to an idle peer.
            Require(socket.Open(Net::AddressFamily::V4, {.reuseAddress = false}).has_value(), "receiver open failed");
            Require(socket.Bind({Net::IpAddress::LoopbackV4(), 0}).has_value(), "receiver bind failed");
            endpoint = BoundEndpoint(socket);
        }
        Net::UdpSocket         socket;
        Net::Endpoint          endpoint;
        std::array<Byte, 64>   buffer {};
        std::optional<Receive> operation;
    };

    // The fixture joins every admitted receive before destroying any buffer,
    // including when a capacity or native failure aborts a measurement.
    class Receivers final
    {
    public:
        Receivers(IO::Runtime& runtime, Execution::ThreadPoolScheduler& scheduler, std::size_t count)
            : context(scheduler, cancellation.GetToken()), joining(scheduler)
        {
            receivers.reserve(count);
            std::set<UInt16> ports;
            for (std::size_t index = 0; index != count; ++index)
            {
                receivers.push_back(std::make_unique<Receiver>(runtime));
                Require(ports.insert(receivers.back()->endpoint.port).second, "receiver endpoints must be distinct");
            }
        }
        ~Receivers()
        {
            cancellation.Cancel();
            for (auto& receiver: receivers)
            {
                if (receiver->operation)
                    (void) Async::SyncWait(joining, Join(joining, *receiver->operation));
            }
        }
        void Start(std::size_t index)
        {
            auto& receiver = *receivers[index];
            receiver.operation.emplace(Async::Spawn(context, receiver.socket.ReceiveFromAsync(context, receiver.buffer)));
        }
        void DrainSubmissions()
        {
            Require(Async::SyncWait(joining, Barrier(joining)).Succeeded(), "submission barrier failed");
        }
        void StartAll()
        {
            for (std::size_t index = 0; index != receivers.size(); ++index)
                Start(index);
            DrainSubmissions();
            for (const auto& receiver: receivers)
                Require(!receiver->operation->IsCompleted(), "receive admission failed before any data was sent");
        }
        void Complete(std::size_t index)
        {
            auto&      receiver = *receivers[index];
            const auto result   = Async::SyncWait(joining, Join(joining, *receiver.operation));
            receiver.operation.reset();
            Require(result.Succeeded() && result.Value().bytesReceived == receiver.buffer.size(), "receive failed or truncated");
        }
        void CancelAndJoin()
        {
            cancellation.Cancel();
            for (auto& receiver: receivers)
            {
                if (!receiver->operation)
                    continue;
                const auto result = Async::SyncWait(joining, Join(joining, *receiver->operation));
                receiver->operation.reset();
                Require(result.IsCanceled(), "pending receive did not cancel");
            }
        }
        const Net::Endpoint& Endpoint(std::size_t index) const { return receivers[index]->endpoint; }

    private:
        Async::CancellationSource              cancellation;
        Async::TaskContext                     context, joining;
        std::vector<std::unique_ptr<Receiver>> receivers;
    };

    void Idle(std::size_t count)
    {
#if defined(__linux__)
        rusage before {}, after {};
        ::getrusage(RUSAGE_SELF, &before);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ::getrusage(RUSAGE_SELF, &after);
        const auto cpu = [](const rusage& usage) {
            return static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1e6 +
                   static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec);
        };
        std::size_t threads {};
        for ([[maybe_unused]] const auto& entry: std::filesystem::directory_iterator("/proc/self/task"))
            ++threads;
        std::cout << "idle sockets=" << count << " seconds=1 cpu_us=" << cpu(after) - cpu(before)
                  << " voluntary_switches=" << after.ru_nvcsw - before.ru_nvcsw
                  << " threads=" << threads << " maxrss_kib=" << after.ru_maxrss << '\n';
#else
        (void) count;
#endif
    }
}// namespace

int main()
{
    try
    {
        std::cout << std::unitbuf;
        constexpr std::size_t iterations = 2000;
        constexpr std::size_t active     = 8;
        std::cout << "Network-only workload: one continuation worker; 64-byte UDP; 8 active sockets; send-to-joined-receive latency\n";
        Execution::ThreadPoolScheduler scheduler(1);
        for (std::size_t count: {std::size_t {8}, std::size_t {1024}})
        {
            const auto        cold = Clock::now();
            IO::Runtime       runtime;
            IO::RuntimeRunner runner(runtime);
            Receivers         receivers(runtime, scheduler, count);
            Net::UdpSocket    sender;
            Require(sender.Open(Net::AddressFamily::V4).has_value(), "sender open failed");
            receivers.StartAll();
            std::cout << "cold sockets=" << count << " setup_us=" << Microseconds(Clock::now() - cold)
                      << " file_backend=" << runtime.HasFileBackend() << '\n';
            Require(!runtime.HasFileBackend(), "network-only workload initialized files");
            Idle(count);
            std::array<Byte, 64> payload {};
            std::vector<double>  samples;
            samples.reserve(iterations);
            const auto workload = Clock::now();
            for (std::size_t index = 0; index != iterations; ++index)
            {
                const auto selected = index % active;
                // Exclude rearm/barrier time from the latency sample while
                // ensuring the receive was suspended before sending each packet.
                receivers.DrainSubmissions();
                const auto start = Clock::now();
                const auto sent  = sender.TrySendTo(receivers.Endpoint(selected), payload);
                Require(sent && *sent == payload.size(), "send failed or truncated");
                receivers.Complete(selected);
                samples.push_back(Microseconds(Clock::now() - start));
                receivers.Start(selected);
            }
            const double wall = Microseconds(Clock::now() - workload);
            std::sort(samples.begin(), samples.end());
            double sum {};
            for (double sample: samples)
                sum += sample;
            std::cout << "suspended_receive sockets=" << count << " active=" << active << " samples=" << samples.size()
                      << " p50_us=" << samples[(iterations - 1) * 50 / 100]
                      << " p95_us=" << samples[(iterations - 1) * 95 / 100]
                      << " p99_us=" << samples[(iterations - 1) * 99 / 100]
                      << " mean_us=" << sum / samples.size()
                      << " operations_per_second_including_rearm=" << iterations * 1e6 / wall << '\n';
            receivers.DrainSubmissions();
            const auto cancel = Clock::now();
            receivers.CancelAndJoin();
            std::cout << "cancel_all sockets=" << count << " join_us=" << Microseconds(Clock::now() - cancel) << '\n';
            const auto stopping = Clock::now();
            runner.Shutdown();
            std::cout << "shutdown sockets=" << count << " us=" << Microseconds(Clock::now() - stopping) << '\n';
        }
    } catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
