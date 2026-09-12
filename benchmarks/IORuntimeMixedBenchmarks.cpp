#include <NGIN/Async/TaskScope.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>
#include <NGIN/IO/RunTask.hpp>
#include <NGIN/Net/Sockets/UdpSocket.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

namespace
{
    using namespace NGIN;
    using Clock                          = std::chrono::steady_clock;
    constexpr std::size_t samples        = 1000;
    constexpr std::size_t fileBytes      = 64 * 1024;
    constexpr std::size_t networkWorkers = 4;

    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    double Micros(Clock::duration elapsed)
    {
        return std::chrono::duration<double, std::micro>(elapsed).count();
    }
    struct TemporaryFile final
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("ngin-mixed-runtime-" + std::to_string(Clock::now().time_since_epoch().count()));
        void Prepare()
        {
            std::ofstream     output(path, std::ios::binary);
            const std::string payload(2 * fileBytes, 'x');
            output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            Require(static_cast<bool>(output), "temporary file setup failed");
        }
        ~TemporaryFile()
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    };
    struct Channel final
    {
        Net::UdpSocket receiver, sender;
        Net::Endpoint  endpoint;
        explicit Channel(IO::Runtime& runtime) : receiver(runtime), sender(runtime)
        {
            Require(receiver.Open(Net::AddressFamily::V4, {.reuseAddress = false}).has_value(), "UDP receiver open failed");
            Require(receiver.Bind({Net::IpAddress::LoopbackV4(), 0}).has_value(), "UDP bind failed");
            Require(sender.Open(Net::AddressFamily::V4).has_value(), "UDP sender open failed");
            sockaddr_in address {};
#if defined(_WIN32)
            int          size   = sizeof(address);
            const SOCKET handle = static_cast<SOCKET>(receiver.Handle().Native());
#else
            socklen_t size   = sizeof(address);
            const int handle = static_cast<int>(receiver.Handle().Native());
#endif
            Require(::getsockname(handle, reinterpret_cast<sockaddr*>(&address), &size) == 0, "UDP endpoint lookup failed");
            endpoint = {Net::IpAddress::LoopbackV4(), ntohs(address.sin_port)};
        }
    };
    struct Results final
    {
        std::atomic<bool>                       stop {false};
        std::array<std::size_t, networkWorkers> exchanges {};
        std::array<std::size_t, 2>              transfers {}, yields {};
        std::vector<double>                     timerOvershoot;
        double                                  coldOpen {}, elapsed {}, joinDrain {};
        Results() { timerOvershoot.reserve(samples); }
    };

    Async::Task<void> Network(Async::TaskContext& context, Channel& channel, Results& results, std::size_t index)
    {
        std::array<Byte, 64> outgoing {}, incoming {};
        outgoing.fill(Byte {0x5a});
        while (!results.stop.load(std::memory_order_acquire))
        {
            // Allocate both cold frames before either operation borrows a buffer.
            // Spawn itself is noexcept and reports reservation failure as a result.
            auto       reading = channel.receiver.ReceiveFromAsync(context, incoming);
            auto       sending = channel.sender.SendToAsync(context, channel.endpoint, outgoing);
            auto       receive = Async::Spawn(context, std::move(reading));
            auto       send    = Async::Spawn(context, std::move(sending));
            const auto sent    = co_await send;
            if (!sent.Succeeded())
                channel.receiver.Close();
            // Always join the receive before propagating a send failure: its
            // buffer belongs to this frame even during scope cancellation.
            const auto received = co_await receive;
            Require(sent.Succeeded() && received.Succeeded(), "mixed UDP exchange failed");
            Require(sent.Value() == outgoing.size() && received.Value().bytesReceived == incoming.size() && incoming == outgoing,
                    "mixed UDP exchange lost or changed data");
            ++results.exchanges[index];
        }
    }
    Async::Task<void> File(Async::TaskContext& context, IO::AsyncFileHandle& file, Results& results, std::size_t writing)
    {
        std::array<Byte, fileBytes> buffer {};
        buffer.fill(writing ? Byte {0x7a} : Byte {});
        while (!results.stop.load(std::memory_order_acquire))
        {
            // Independent ranges let reads and writes overlap without a data race.
            auto       operation   = Async::Spawn(context, writing ? file.WriteAtAsync(context, fileBytes, buffer)
                                                                   : file.ReadAtAsync(context, 0, buffer));
            const auto transferred = co_await operation;
            Require(transferred.Succeeded() && transferred.Value() == buffer.size(), "mixed file transfer failed");
            if (!writing)
                Require(std::all_of(buffer.begin(), buffer.end(), [](Byte value) { return value == Byte {'x'}; }),
                        "mixed file read data changed");
            ++results.transfers[writing];
        }
    }
    Async::Task<void> Yield(Async::TaskContext& context, Results& results, std::size_t index)
    {
        while (!results.stop.load(std::memory_order_acquire))
        {
            co_await context.YieldNow();
            ++results.yields[index];
        }
    }

    void Measure(const TemporaryFile& temporary, IO::Runtime::FileBackendPreference preference, bool external, bool mixed)
    {
        const auto                                      coldStart = Clock::now();
        IO::Runtime                                     runtime({.files = {.backendPreference = preference}});
        std::unique_ptr<Execution::ThreadPoolScheduler> pool;
        if (external)
            pool = std::make_unique<Execution::ThreadPoolScheduler>(1);
        const auto                                           executor = external ? Execution::ExecutorRef::From(*pool) : runtime.GetExecutor();
        std::array<std::unique_ptr<Channel>, networkWorkers> channels;
        for (auto& channel: channels)
            channel = std::make_unique<Channel>(runtime);
        IO::LocalFileSystem files(runtime);
        Results             results;
        const auto          outcome = IO::RunTask(runtime, [&](Async::TaskContext& context, Async::TaskScope<>& scope) -> Async::Task<void> {
            IO::FileOpenOptions options;
            options.access      = IO::FileAccess::ReadWrite;
            options.disposition = IO::FileCreateDisposition::OpenExisting;
            auto opening        = Async::Spawn(context, files.OpenFileAsync(context, IO::Path(temporary.path.string().c_str()), options));
            auto opened         = co_await opening;
            Require(opened.Succeeded(), "mixed file open failed (requested backend must be available)");
            auto file          = std::move(opened.Value());
            results.coldOpen   = Micros(Clock::now() - coldStart);
            const auto started = Clock::now();
            if (mixed)
            {
                for (std::size_t index = 0; index != networkWorkers; ++index)
                    Require(scope.SpawnOn(executor, [&, index](Async::TaskContext& child) {
                                     return Network(child, *channels[index], results, index);
                                 }).has_value(),
                                     "mixed network task admission failed");
                for (std::size_t index = 0; index != 2; ++index)
                {
                    Require(scope.SpawnOn(executor, [&, index](Async::TaskContext& child) {
                                     return File(child, file, results, index);
                                 }).has_value(),
                                     "mixed file task admission failed");
                    Require(scope.SpawnOn(executor, [&, index](Async::TaskContext& child) {
                                     return Yield(child, results, index);
                                 }).has_value(),
                                     "mixed yielding task admission failed");
                }
            }
            for (std::size_t index = 0; index != samples; ++index)
            {
                const UInt64 before = Time::MonotonicClock::Now().ToNanoseconds();
                co_await context.Delay(Units::Milliseconds(1));
                const UInt64 after   = Time::MonotonicClock::Now().ToNanoseconds();
                const UInt64 elapsed = after - before;
                results.timerOvershoot.push_back(elapsed > 1'000'000 ? static_cast<double>(elapsed - 1'000'000) / 1000.0 : 0.0);
            }
            const auto stop = Clock::now();
            results.stop.store(true, std::memory_order_release);
            (void) co_await scope.Join();
            results.joinDrain = Micros(Clock::now() - stop);
            results.elapsed   = Micros(Clock::now() - started);
            auto closing      = Async::Spawn(context, file.CloseAsync(context));
            Require((co_await closing).Succeeded(), "mixed file close failed");
        });
        Require(outcome.Succeeded() && runtime.IsStopped(), "mixed root or structured child join failed");
        Require(results.timerOvershoot.size() == samples, "timer samples were lost");
        std::size_t exchanges = 0, transfers = 0, yields = 0;
        for (std::size_t value: results.exchanges)
        {
            Require(!mixed || value != 0, "a UDP worker made no progress");
            exchanges += value;
        }
        for (std::size_t value: results.transfers)
        {
            Require(!mixed || value != 0, "a file direction made no progress");
            transfers += value;
        }
        for (std::size_t value: results.yields)
        {
            Require(!mixed || value != 0, "a yielding worker made no progress");
            yields += value;
        }
        std::sort(results.timerOvershoot.begin(), results.timerOvershoot.end());
        double sum = 0;
        for (double value: results.timerOvershoot)
            sum += value;
        std::cout << "mixed=" << mixed << " continuations=" << (external ? "external_one_worker" : "runtime")
                  << " file_backend=" << static_cast<int>(runtime.GetFileBackend()) << " samples=" << samples
                  << " delay_us=1000 timer_overshoot_p50_us=" << results.timerOvershoot[(samples - 1) * 50 / 100]
                  << " timer_overshoot_p95_us=" << results.timerOvershoot[(samples - 1) * 95 / 100]
                  << " timer_overshoot_p99_us=" << results.timerOvershoot[(samples - 1) * 99 / 100]
                  << " timer_overshoot_max_us=" << results.timerOvershoot.back() << " timer_overshoot_mean_us=" << sum / samples
                  << " cold_setup_open_us=" << results.coldOpen << " elapsed_including_joins_us=" << results.elapsed
                  << " join_drain_us=" << results.joinDrain << " udp_exchanges=" << exchanges
                  << " file_reads=" << results.transfers[0] << " file_writes=" << results.transfers[1]
                  << " yield_callbacks=" << yields << " udp_exchanges_per_second=" << exchanges * 1e6 / results.elapsed
                  << " file_transfers_per_second=" << transfers * 1e6 / results.elapsed << '\n';
    }
}// namespace

int main()
{
    try
    {
        std::cout.setf(std::ios::unitbuf);
        std::cout << "Mixed runtime workload: four 64-byte UDP exchanges, independent 64-KiB file read/write, two yield loops; runtime timer root\n";
        TemporaryFile temporary;
        temporary.Prepare();
        for (auto preference: {IO::Runtime::FileBackendPreference::Native, IO::Runtime::FileBackendPreference::Fallback})
            for (bool external: {false, true})
                for (bool mixed: {false, true})
                    Measure(temporary, preference, external, mixed);
        return 0;
    } catch (const std::exception& error)
    {
        std::cerr << "mixed benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
