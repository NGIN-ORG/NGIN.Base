#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Race.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Benchmark.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/WorkItem.hpp>
#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Units.hpp>

#include <array>
#include <cstddef>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

namespace
{
    constexpr std::size_t OperationCount = 10'000;

    bool CountCancellation(void* state) noexcept
    {
        std::size_t* count = static_cast<std::size_t*>(state);
        ++(*count);
        return false;
    }

    struct HeapJob final
    {
        std::array<std::byte, 128> payload {};
        std::size_t*               count {nullptr};

        void operator()() const noexcept
        {
            ++(*count);
        }
    };

    NGIN::Async::Task<int> ImmediateWinner(NGIN::Async::TaskContext&)
    {
        co_return 1;
    }

    NGIN::Async::Task<int> CooperativeLoser(NGIN::Async::TaskContext& context)
    {
        for (;;)
        {
            if (context.CheckCancellation())
            {
                co_await NGIN::Async::Stopped();
                co_return 0;
            }
            co_await context.YieldNow();
        }
    }

    NGIN::Async::Task<int> SlowLoser(NGIN::Async::TaskContext& uncancelledContext)
    {
        for (std::size_t iteration = 0; iteration < 16; ++iteration)
            co_await uncancelledContext.YieldNow();
        co_return 2;
    }
}// namespace

int main()
{
    // Registration snapshots the default configuration, so set it before
    // constructing any benchmark entries.
    NGIN::Benchmark::defaultConfig.iterations       = 100;
    NGIN::Benchmark::defaultConfig.warmupIterations = 20;
    NGIN::Benchmark::defaultConfig.keepRawTimings   = true;

    NGIN::Benchmark::Register([](NGIN::BenchmarkContext& context) {
        std::size_t invoked = 0;
        context.start();
        for (std::size_t index = 0; index < OperationCount; ++index)
        {
            NGIN::Execution::WorkItem item([&invoked]() noexcept { ++invoked; });
            item.Invoke();
        }
        context.stop();
        context.doNotOptimize(invoked);
    },
                              "WorkItem inline construct/invoke x10k");

    NGIN::Benchmark::Register([](NGIN::BenchmarkContext& context) {
        std::size_t invoked = 0;
        context.start();
        for (std::size_t index = 0; index < OperationCount; ++index)
        {
            NGIN::Execution::WorkItem item(HeapJob {.count = &invoked});
            item.Invoke();
        }
        context.stop();
        context.doNotOptimize(invoked);
    },
                              "WorkItem heap construct/invoke x10k");

    NGIN::Benchmark::Register([](NGIN::BenchmarkContext& context) {
        NGIN::Async::CancellationSource       source;
        NGIN::Async::CancellationRegistration registration;
        std::size_t                           callbacks = 0;
        context.start();
        for (std::size_t index = 0; index < OperationCount; ++index)
        {
            const NGIN::Async::CancellationRegistrationResult result =
                    source.GetToken().Register(registration, {}, {}, &CountCancellation, &callbacks);
            if (!result)
                throw std::bad_alloc {};
            registration.Reset();
        }
        context.stop();
        context.doNotOptimize(callbacks);
    },
                              "Cancellation register/reset x10k");

    NGIN::Benchmark::Register([](NGIN::BenchmarkContext& context) {
        constexpr std::size_t                              CallbackCount = 256;
        NGIN::Async::CancellationSource                    source;
        std::vector<NGIN::Async::CancellationRegistration> registrations(CallbackCount);
        std::size_t                                        callbacks = 0;
        for (NGIN::Async::CancellationRegistration& registration: registrations)
        {
            const NGIN::Async::CancellationRegistrationResult result =
                    source.GetToken().Register(registration, {}, {}, &CountCancellation, &callbacks);
            if (!result)
                throw std::bad_alloc {};
        }
        context.start();
        source.Cancel();
        context.stop();
        context.doNotOptimize(callbacks);
    },
                              "Cancellation fire 256 callbacks");

    NGIN::Benchmark::Register([](NGIN::BenchmarkContext& context) {
        context.start();
        for (std::size_t index = 0; index < OperationCount; ++index)
        {
            NGIN::Memory::Shared<int>                                owner  = NGIN::Memory::MakeShared<int>(42);
            NGIN::Memory::Ticket<int, NGIN::Memory::SystemAllocator> weak   = NGIN::Memory::MakeTicket(owner);
            NGIN::Memory::Shared<int>                                copy   = owner;
            NGIN::Memory::Shared<int>                                locked = weak.Lock();
            context.doNotOptimize(locked);
        }
        context.stop();
    },
                              "Shared create/copy/weak-lock/release x10k");

    NGIN::Benchmark::Register([](NGIN::BenchmarkContext& context) {
        NGIN::Execution::CooperativeScheduler scheduler;
        NGIN::Async::TaskContext              taskContext(scheduler);
        context.start();
        // The cooperative loser yields while the admitted winner completes.
        auto operationTask = NGIN::Async::Race(
                taskContext,
                [](NGIN::Async::TaskContext& child) { return CooperativeLoser(child); },
                [](NGIN::Async::TaskContext& child) { return ImmediateWinner(child); });

        NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(taskContext.GetEnvironment(), 1);

        auto operation = operationRoot.Spawn(std::move(operationTask)).value();
        while (!operation.IsCompleted())
        {
            if (!scheduler.RunOne())
                throw std::runtime_error("Race became idle before completing");
        }
        if (!operationRoot.TryJoin())
            throw std::runtime_error("benchmark root did not retire");
        auto result = operation.TakeResult();
        context.stop();
        context.doNotOptimize(result);
    },
                              "Race responsive loser cancel+drain");

    NGIN::Benchmark::Register([](NGIN::BenchmarkContext& context) {
        NGIN::Execution::CooperativeScheduler scheduler;
        NGIN::Async::TaskContext              taskContext(scheduler);
        context.start();
        // The loser continues for a fixed number of yields after winner selection.
        auto operationTask = NGIN::Async::Race(
                taskContext,
                [&taskContext](NGIN::Async::TaskContext&) { return SlowLoser(taskContext); },
                [](NGIN::Async::TaskContext& child) { return ImmediateWinner(child); });

        NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(taskContext.GetEnvironment(), 1);

        auto operation = operationRoot.Spawn(std::move(operationTask)).value();
        while (!operation.IsCompleted())
        {
            if (!scheduler.RunOne())
                throw std::runtime_error("Race became idle before completing");
        }
        if (!operationRoot.TryJoin())
            throw std::runtime_error("benchmark root did not retire");
        auto result = operation.TakeResult();
        context.stop();
        context.doNotOptimize(result);
    },
                              "Race slow loser drain (16 yields)");

    const std::vector<NGIN::BenchmarkResult<NGIN::Units::Nanoseconds>> results =
            NGIN::Benchmark::RunAll<NGIN::Units::Nanoseconds>();
    NGIN::Benchmark::PrintSummaryTable(std::cout, results);
    for (const NGIN::BenchmarkResult<NGIN::Units::Nanoseconds>& result: results)
        std::cout << result << '\n';
    return 0;
}
