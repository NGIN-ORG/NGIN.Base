#include "AllocationInstrumentation.hpp"

#include <NGIN/Async/Task.hpp>
#include <NGIN/IO/Runtime.hpp>
#include <NGIN/Net/Sockets/UdpSocket.hpp>

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <thread>
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
    namespace Allocation = NGIN::Benchmarks::Allocations;
    using Execution::ScheduleError;
    using Execution::WorkItem;
    using Stats                    = Memory::AllocationStats;
    constexpr std::size_t capacity = 64;
    constexpr std::size_t rounds   = 128;
    constexpr std::size_t excess   = 1024;

    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    void Drain(IO::Runtime& runtime)
    {
        while (runtime.PollOnce()) {}
    }
    struct Shutdown final
    {
        IO::Runtime& runtime;
        ~Shutdown() { runtime.Shutdown(); }
    };
    Stats Begin()
    {
        Allocation::ResetPeak();
        return Allocation::Snapshot();
    }
    void Report(const char* name, const Stats& before, const Stats& after, std::size_t operations, std::size_t firstRejectionGrowth = 0)
    {
        std::cout << name << " operations=" << operations
                  << " allocations=" << after.totalCount - before.totalCount
                  << " requested_bytes=" << after.totalBytes - before.totalBytes
                  << " peak_live_bytes_above_floor=" << after.peakBytes - before.currentBytes
                  << " first_rejection_retained_bytes_max=" << firstRejectionGrowth
                  << " retained_bytes_above_floor=" << static_cast<std::int64_t>(after.currentBytes) - static_cast<std::int64_t>(before.currentBytes)
                  << '\n';
    }
    void RequireRetired(const Stats& floor)
    {
        const Stats now = Allocation::Snapshot();
        if (now.currentBytes != floor.currentBytes || now.currentCount != floor.currentCount)
        {
            std::cerr << "allocation mismatch floor_bytes=" << floor.currentBytes << " now_bytes=" << now.currentBytes
                      << " floor_count=" << floor.currentCount << " now_count=" << now.currentCount << '\n';
            throw std::runtime_error("joined workload retained C++ allocation storage");
        }
    }
    Time::TimePoint LongDeadline()
    {
        return Time::TimePoint::FromNanoseconds(Time::MonotonicClock::Now().ToNanoseconds() + 3'600'000'000'000ULL);
    }

    void InstrumentationCheck()
    {
        const Stats before = Begin();
        struct Blocks final
        {
            void* ordinary {};
            void* aligned {};
            void* zero {};
            void* pmr {};
            ~Blocks()
            {
                ::operator delete(ordinary, std::size_t {64});
                ::operator delete[](aligned, std::align_val_t {256});
                ::operator delete(zero, std::nothrow);
                if (pmr)
                    std::pmr::new_delete_resource()->deallocate(pmr, 96, 32);
            }
        };
        Stats live;
        bool  alignedCorrectly = false, pmrCovered = false;
        {
            Blocks blocks;
            blocks.ordinary = ::operator new(64);
            blocks.aligned  = ::operator new[](513, std::align_val_t {256});
            blocks.zero     = ::operator new(0, std::nothrow);
            Require(blocks.ordinary && blocks.aligned && blocks.zero, "instrumentation allocation failed");
            alignedCorrectly      = reinterpret_cast<std::uintptr_t>(blocks.aligned) % 256 == 0;
            const Stats beforePmr = Allocation::Snapshot();
            blocks.pmr            = std::pmr::new_delete_resource()->allocate(96, 32);
            live                  = Allocation::Snapshot();
            pmrCovered            = live.currentBytes == beforePmr.currentBytes + 96 && live.currentCount == beforePmr.currentCount + 1;
        }
        Require(alignedCorrectly, "instrumentation broke alignment");
        Require(pmrCovered, "replacement new does not cover the standard PMR resource on this platform");
        Require(live.currentBytes == before.currentBytes + 673 && live.currentCount == before.currentCount + 4,
                "instrumentation failed to account for requested allocations");
        const std::size_t impossible = std::numeric_limits<std::size_t>::max();
        void*             failed     = ::operator new(impossible, std::nothrow);
        Require(!failed, "instrumentation accepted an overflowing request");
        RequireRetired(before);
        {
            std::array<std::thread, 4> producers;
            for (auto& producer: producers)
                producer = std::thread([] {
                    for (std::size_t index = 0; index != 1000; ++index)
                    {
                        void* block = ::operator new(37, std::align_val_t {64});
                        ::operator delete(block, std::size_t {37}, std::align_val_t {64});
                    }
                });
            for (auto& producer: producers)
                producer.join();
        }
        RequireRetired(before);
        Require(Allocation::Snapshot().totalCount >= before.totalCount + 4004,
                "instrumentation missed concurrent allocation calls");
        std::cout << "instrumentation_check=passed ordinary/array/aligned/zero/pmr/overflow/concurrent/free\n";
    }

    void SubmissionOverload()
    {
        IO::Runtime runtime({.submissionCapacity = capacity});
        std::size_t called = 0;
        Shutdown    cleanup {runtime};
        Drain(runtime);
        const auto  executor = runtime.GetExecutor();
        const Stats before   = Begin();
        std::size_t retained = 0;
        for (std::size_t round = 0; round != rounds; ++round)
        {
            for (std::size_t index = 0; index != capacity; ++index)
                Require(executor.Execute(WorkItem([&] { ++called; })).has_value(), "submission admission failed early");
            const Stats full = Allocation::Snapshot();
            if (round == 0)
                retained = full.currentBytes;
            Require(full.currentBytes == retained, "submission queue grew between equal saturated rounds");
            for (std::size_t index = 0; index != excess; ++index)
            {
                const auto rejected = executor.Execute(WorkItem([] {}));
                Require(!rejected && rejected.error() == ScheduleError::ResourceExhausted, "submission overload was not rejected");
            }
            const Stats flooded = Allocation::Snapshot();
            Require(flooded.totalCount == full.totalCount && flooded.currentBytes == full.currentBytes,
                    "rejected small submissions allocated or retained queue storage");
            Drain(runtime);
            Require(called == (round + 1) * capacity, "accepted submission did not run exactly once");
            RequireRetired(before);
        }
        Report("submission_overload", before, Allocation::Snapshot(), rounds * (capacity + excess));
    }

    void CompletionOverload()
    {
        IO::Runtime                                   runtime({.completionCapacity = capacity});
        std::size_t                                   called = 0;
        Shutdown                                      cleanup {runtime};
        std::vector<Execution::CompletionReservation> tickets;
        tickets.reserve(capacity);
        const auto executor = runtime.GetExecutor();
        Drain(runtime);
        const Stats before   = Begin();
        std::size_t retained = 0;
        for (std::size_t round = 0; round != rounds; ++round)
        {
            for (std::size_t index = 0; index != capacity; ++index)
            {
                auto admitted = executor.ReserveCompletion(WorkItem([&] { ++called; }));
                Require(admitted.has_value(), "completion admission failed early");
                tickets.push_back(std::move(*admitted));
            }
            const Stats full = Allocation::Snapshot();
            if (round == 0)
                retained = full.currentBytes;
            Require(full.currentBytes == retained, "completion queue grew between equal saturated rounds");
            for (std::size_t index = 0; index != excess; ++index)
            {
                const auto rejected = executor.ReserveCompletion(WorkItem([] {}));
                Require(!rejected && rejected.error() == ScheduleError::ResourceExhausted, "completion overload was not rejected");
            }
            const Stats flooded = Allocation::Snapshot();
            Require(flooded.totalCount == full.totalCount && flooded.currentBytes == full.currentBytes,
                    "rejected completions allocated or retained queue storage");
            if (round % 2 != 0)
                for (auto& ticket: tickets)
                    ticket.Dispatch();
            tickets.clear();
            Drain(runtime);
            Require(called == ((round + 1) / 2) * capacity, "completion retirement/delivery was not exactly once");
            RequireRetired(before);
        }
        Report("completion_overload", before, Allocation::Snapshot(), rounds * (capacity + excess));
    }

    void TimerOverload()
    {
        struct Probe final
        {
            std::size_t&                destroyed;
            std::array<std::byte, 1024> payload {};
            ~Probe() { ++destroyed; }
        };
        IO::Runtime                               runtime({.timerCapacity = capacity});
        std::size_t                               called = 0, destroyed = 0;
        std::vector<Execution::TimerRegistration> timers;
        timers.reserve(capacity);
        Shutdown   cleanup {runtime};
        const auto executor = runtime.GetExecutor();
        const auto deadline = LongDeadline();
        Drain(runtime);
        const Stats before   = Begin();
        std::size_t retained = 0;
        for (std::size_t round = 0; round != rounds; ++round)
        {
            for (std::size_t index = 0; index != capacity; ++index)
            {
                auto probe    = std::make_unique<Probe>(destroyed);
                auto admitted = executor.ScheduleTimer(WorkItem([owner = std::move(probe), &called] { if (owner) ++called; }), deadline);
                Require(admitted.has_value(), "timer admission failed early");
                timers.push_back(std::move(*admitted));
            }
            const Stats full = Allocation::Snapshot();
            if (round == 0)
                retained = full.currentBytes;
            Require(full.currentBytes == retained, "timer queue grew between equal saturated rounds");
            for (std::size_t index = 0; index != excess; ++index)
            {
                const auto rejected = executor.ScheduleTimer(WorkItem([] {}), deadline);
                Require(!rejected && rejected.error() == ScheduleError::ResourceExhausted, "timer overload was not rejected");
            }
            const Stats flooded = Allocation::Snapshot();
            Require(flooded.totalCount == full.totalCount && flooded.currentBytes == full.currentBytes,
                    "rejected timers allocated or retained queue storage");
            for (auto& timer: timers)
                Require(timer.Cancel(), "long timer was lost before cancellation");
            timers.clear();
            Require(destroyed == (round + 1) * capacity && called == 0, "timer cancellation retained payload or invoked callback");
            Require(!runtime.NextDeadline(), "canceled long timer retained its deadline");
            // This check deliberately precedes PollOnce: cancellation must release
            // storage promptly, not defer it to the deadline or a drive call.
            RequireRetired(before);
            Drain(runtime);
        }
        Report("long_timer_overload", before, Allocation::Snapshot(), rounds * (capacity + excess));
    }

    template<typename T, typename Error = Async::NoError>
    struct Pending final
    {
        IO::Runtime&                            runtime;
        Async::CancellationSource               cancellation;
        Async::TaskContext                      context;
        std::vector<Async::Operation<T, Error>> operations;
        explicit Pending(IO::Runtime& io)
            : runtime(io), context(io.GetExecutor(), cancellation.GetToken())
        {
            operations.reserve(capacity + 1);
        }
        void Cancel() noexcept
        {
            cancellation.Cancel();
            while (!std::all_of(operations.begin(), operations.end(), [](const auto& op) { return op.IsCompleted(); }))
            {
                runtime.PollOnce();
                std::this_thread::yield();
            }
        }
        ~Pending()
        {
            Cancel();
            operations.clear();
            Drain(runtime);
        }
    };

    Async::Task<void> LongDelay(Async::TaskContext& context)
    {
        co_await context.Delay(Units::Seconds(3600));
    }

    void DelayedTaskOverload()
    {
        IO::Runtime runtime({.timerCapacity = capacity});
        Shutdown    cleanup {runtime};
        std::size_t firstRejectionGrowth = 0;
        const auto  run                  = [&] {
            Pending<void> pending(runtime);
            for (std::size_t index = 0; index != capacity; ++index)
                pending.operations.push_back(Async::Spawn(pending.context, LongDelay(pending.context)));
            Drain(runtime);
            for (const auto& operation: pending.operations)
                Require(!operation.IsCompleted(), "long delay admission failed early");
            const Stats full           = Allocation::Snapshot();
            Stats       rejectionFloor = full;
            for (std::size_t index = 0; index != 256; ++index)
            {
                pending.operations.push_back(Async::Spawn(pending.context, LongDelay(pending.context)));
                Drain(runtime);
                auto& rejected = pending.operations.back();
                Require(rejected.IsCompleted(), "delay overload admitted an extra timer");
                {
                    const auto result = rejected.TakeResult();
                    Require(result.IsFault() && result.Fault().code == Async::AsyncFaultCode::SchedulerDispatchFailed &&
                                                      result.Fault().native == static_cast<int>(ScheduleError::ResourceExhausted),
                                              "delay overload did not report timer resource exhaustion");
                }
                pending.operations.pop_back();
                Drain(runtime);
                if (index == 0)
                {
                    rejectionFloor = Allocation::Snapshot();
                    Require(rejectionFloor.currentCount == full.currentCount, "rejection retained an extra allocation object");
                    firstRejectionGrowth = std::max(firstRejectionGrowth,
                                                                      std::max(full.currentBytes, rejectionFloor.currentBytes) - full.currentBytes);
                }
                // Shared cancellation tables may grow for the first transient
                // registration. Further overload must plateau at that capacity.
                RequireRetired(rejectionFloor);
            }
            pending.Cancel();
            for (auto& operation: pending.operations)
                Require(operation.TakeResult().IsCanceled(), "long delay did not cancel");
            Require(!runtime.NextDeadline(), "long delay retained a canceled deadline");
        };
        run();
        const Stats before = Begin();
        for (std::size_t round = 0; round != 16; ++round)
        {
            run();
            RequireRetired(before);
        }
        Report("long_delay_task_overload", before, Allocation::Snapshot(), 16 * (capacity + 256), firstRejectionGrowth);
    }

    void SocketOverload(bool registrationLimit)
    {
        IO::Runtime                 runtime({.operationCapacity    = registrationLimit ? capacity + 1 : capacity,
                                             .registrationCapacity = capacity});
        std::vector<Net::UdpSocket> sockets;
        sockets.reserve(capacity + 1);
        std::array<std::array<Byte, 64>, capacity + 1> buffers {};
        for (std::size_t index = 0; index != capacity + 1; ++index)
        {
            sockets.emplace_back(runtime);
            Require(sockets.back().Open(Net::AddressFamily::V4, {.reuseAddress = false}).has_value(), "socket open failed");
            Require(sockets.back().Bind({Net::IpAddress::LoopbackV4(), 0}).has_value(), "socket bind failed");
        }
        Shutdown    cleanup {runtime};
        std::size_t firstRejectionGrowth = 0;
        const auto  run                  = [&] {
            Pending<Net::DatagramReceiveResult, Net::NetError> pending(runtime);
            for (std::size_t index = 0; index != capacity; ++index)
                pending.operations.push_back(Async::Spawn(pending.context,
                                                                            sockets[index].ReceiveFromAsync(pending.context, buffers[index])));
            Drain(runtime);
            for (const auto& operation: pending.operations)
                Require(!operation.IsCompleted(), "socket admission failed early");
            const Stats full           = Allocation::Snapshot();
            Stats       rejectionFloor = full;
            for (std::size_t index = 0; index != 256; ++index)
            {
                pending.operations.push_back(Async::Spawn(pending.context,
                                                                            sockets.back().ReceiveFromAsync(pending.context, buffers.back())));
                Drain(runtime);
                auto& rejected = pending.operations.back();
                Require(rejected.IsCompleted(), "socket overload admitted an extra receive");
                {
                    const auto result = rejected.TakeResult();
                    if (registrationLimit)
                        Require(result.IsDomainError() && result.DomainError().code == Net::NetErrorCode::ResourceExhausted,
                                                  "registration overload did not report resource exhaustion");
                    else
                        Require(result.IsFault() && result.Fault().code == Async::AsyncFaultCode::SchedulerDispatchFailed,
                                                  "operation overload did not report a scheduling fault");
                }
                pending.operations.pop_back();
                Drain(runtime);
                if (index == 0)
                {
                    rejectionFloor = Allocation::Snapshot();
                    Require(rejectionFloor.currentCount == full.currentCount, "rejection retained an extra allocation object");
                    firstRejectionGrowth = std::max(firstRejectionGrowth,
                                                                      std::max(full.currentBytes, rejectionFloor.currentBytes) - full.currentBytes);
                }
                // Shared cancellation tables may grow for the first transient
                // registration. Further overload must plateau at that capacity.
                RequireRetired(rejectionFloor);
            }
            pending.Cancel();
            for (auto& operation: pending.operations)
                Require(operation.TakeResult().IsCanceled(), "pending socket did not cancel");
        };
        run();
        const Stats before = Begin();
        for (std::size_t round = 0; round != 16; ++round)
        {
            run();
            RequireRetired(before);
        }
        Report(registrationLimit ? "socket_registration_overload" : "socket_operation_overload",
               before, Allocation::Snapshot(), 16 * (capacity + 256), firstRejectionGrowth);
        Require(!runtime.HasFileBackend(), "socket overload initialized file services");
    }

    // Used in a separate run from raw allocation measurements: wrapping callbacks
    // changes callable storage, but counts each actual executor callback invocation.
    struct CountingExecutor final
    {
        Execution::ExecutorRef    executor;
        std::size_t               submissions {}, submissionCallbacks {}, reservations {}, completionCallbacks {};
        bool                      IsCurrent() const noexcept { return executor.IsCurrent(); }
        Execution::ScheduleResult Execute(WorkItem work) noexcept
        {
            try
            {
                ++submissions;
                return executor.Execute(WorkItem([this, item = std::move(work)]() mutable {
                    ++submissionCallbacks;
                    item.Invoke();
                }));
            } catch (const std::bad_alloc&)
            {
                return std::unexpected(ScheduleError::ResourceExhausted);
            }
        }
        Execution::ScheduleResult ExecuteAt(WorkItem work, Time::TimePoint deadline) noexcept
        {
            try
            {
                ++submissions;
                return executor.ExecuteAt(WorkItem([this, item = std::move(work)]() mutable {
                                              ++submissionCallbacks;
                                              item.Invoke();
                                          }),
                                          deadline);
            } catch (const std::bad_alloc&)
            {
                return std::unexpected(ScheduleError::ResourceExhausted);
            }
        }
        std::expected<Execution::CompletionReservation, ScheduleError> ReserveCompletion(WorkItem work) noexcept
        {
            try
            {
                ++reservations;
                return executor.ReserveCompletion(WorkItem([this, item = std::move(work)]() mutable {
                    ++completionCallbacks;
                    item.Invoke();
                }));
            } catch (const std::bad_alloc&)
            {
                return std::unexpected(ScheduleError::ResourceExhausted);
            }
        }
    };

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

    void ReceiveAllocations(bool countCallbacks)
    {
        IO::Runtime          runtime;
        Net::UdpSocket       receiver(runtime), sender;
        CountingExecutor     counted {runtime.GetExecutor()};
        Async::TaskContext   context(countCallbacks ? Execution::ExecutorRef::From(counted) : runtime.GetExecutor());
        std::array<Byte, 64> buffer {};
        Require(receiver.Open(Net::AddressFamily::V4, {.reuseAddress = false}).has_value(), "receiver open failed");
        Require(receiver.Bind({Net::IpAddress::LoopbackV4(), 0}).has_value(), "receiver bind failed");
        Require(sender.Open(Net::AddressFamily::V4).has_value(), "sender open failed");
        const auto endpoint = BoundEndpoint(receiver);
        Shutdown   cleanup {runtime};
        const auto run = [&](bool ready) {
            if (ready)
                Require(sender.TrySendTo(endpoint, buffer).has_value(), "ready send failed");
            auto operation = Async::Spawn(context, receiver.ReceiveFromAsync(context, buffer));
            // A guard closes and drains before buffer/context destruction on error.
            struct Join final
            {
                IO::Runtime&         runtime;
                Net::UdpSocket&      socket;
                decltype(operation)& task;
                ~Join()
                {
                    if (!task.IsCompleted())
                    {
                        socket.Close();
                        while (!task.IsCompleted())
                        {
                            runtime.PollOnce();
                            std::this_thread::yield();
                        }
                    }
                }
            } join {runtime, receiver, operation};
            Drain(runtime);
            if (!ready)
            {
                Require(!operation.IsCompleted(), "receive did not suspend before data");
                Require(sender.TrySendTo(endpoint, buffer).has_value(), "suspended send failed");
            }
            while (!operation.IsCompleted())
            {
                runtime.PollOnce();
                std::this_thread::yield();
            }
            const auto result = operation.TakeResult();
            Require(result.Succeeded() && result.Value().bytesReceived == buffer.size(), "receive result failed");
        };
        run(true);
        run(false);
        Drain(runtime);
        for (bool ready: {true, false})
        {
            const CountingExecutor countsBefore = counted;
            const Stats            before       = Begin();
            for (std::size_t index = 0; index != 2000; ++index)
            {
                run(ready);
                Drain(runtime);
                RequireRetired(before);
            }
            if (countCallbacks)
            {
                Require(counted.submissions - countsBefore.submissions == counted.submissionCallbacks - countsBefore.submissionCallbacks,
                        "selected executor left submission callbacks undispatched");
#if defined(_WIN32)
                // Even primed receives retire through the shared IOCP packet.
                constexpr std::size_t expectedCompletions = 2000;
#else
                const std::size_t expectedCompletions = ready ? 0 : 2000;
#endif
                Require(counted.completionCallbacks - countsBefore.completionCallbacks == expectedCompletions,
                        "receive completion used an unexpected number of executor callbacks");
                std::cout << (ready ? "runtime_ready_receive_callbacks" : "runtime_suspended_receive_callbacks")
                          << " operations=2000 submissions=" << counted.submissions - countsBefore.submissions
                          << " submission_callbacks=" << counted.submissionCallbacks - countsBefore.submissionCallbacks
                          << " reservations=" << counted.reservations - countsBefore.reservations
                          << " completion_callbacks=" << counted.completionCallbacks - countsBefore.completionCallbacks << '\n';
            }
            else
                Report(ready ? "runtime_ready_receive" : "runtime_suspended_receive", before, Allocation::Snapshot(), 2000);
        }
        Require(!runtime.HasFileBackend(), "network allocation workload initialized files");
    }
}// namespace

int main()
{
    try
    {
        std::cout.setf(std::ios::unitbuf);
        std::cout << "Runtime allocation workload: requested C++ bytes; instrumented process; no latency acceptance data\n"
                  << "overload_capacity=" << capacity << " rounds=" << rounds << " rejected_per_round=" << excess
                  << " timer_deadline_seconds=3600 timer_payload_bytes=1024\n";
        InstrumentationCheck();
        const auto checked = [](auto function) {
            const Stats before = Allocation::Snapshot();
            function();
            RequireRetired(before);
        };
        checked(SubmissionOverload);
        checked(CompletionOverload);
        checked(TimerOverload);
        checked(DelayedTaskOverload);
        checked([] { SocketOverload(false); });
        checked([] { SocketOverload(true); });
        checked([] { ReceiveAllocations(false); });
        checked([] { ReceiveAllocations(true); });
        return 0;
    } catch (const std::exception& exception)
    {
        std::cerr << "allocation benchmark failed: " << exception.what() << '\n';
        return 1;
    }
}
