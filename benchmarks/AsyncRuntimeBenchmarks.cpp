#include "../src/NGIN/IO/FileOperationGate.hpp"
#include "../src/NGIN/IO/FileSystemDriver.hpp"
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskScope.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/AsyncFileHandle.hpp>
#include <NGIN/IO/Runtime.hpp>

#ifdef NGIN_ASYNC_MEASURE_ALLOCATIONS
#include "AllocationInstrumentation.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace NGIN;
    using Clock = std::chrono::steady_clock;

    Async::Task<int> Chain(int depth, Async::TaskContext& context, bool pending)
    {
        if (depth > 1)
            co_return 1 + co_await Chain(depth - 1, context, pending);
        if (pending)
            co_await context.YieldNow();
        co_return 1;
    }

    Async::Task<int> FairChain(int depth, int& entered)
    {
        ++entered;
        if (depth > 1)
            co_return 1 + co_await FairChain(depth - 1, entered);
        co_return 1;
    }

    Async::Task<int> PendingChain(int depth, Async::TaskContext& context)
    {
        if (depth > 1)
            co_return co_await PendingChain(depth - 1, context);
        co_await context.Delay(Units::Seconds(60));
        co_return 1;
    }

    Async::Task<int> ScopeJoin(Async::TaskContext&, Async::TaskScope<>& scope)
    {
        const auto& outcome = co_await scope.Join();
        co_return outcome.Succeeded() ? 1 : 0;
    }

    struct FileState
    {
        explicit FileState(IO::detail::FileSystemDriver& driver) : operations(driver) {}
        IO::detail::FileOperationGate operations;
        IO::Path                      path {};
        bool                          NativeIsOpen() const noexcept { return true; }
    };

    IO::AsyncTask<UIntSize> FileLeaf(const std::shared_ptr<void>&, Async::TaskContext&, std::span<Byte> bytes)
    {
        co_return bytes.size();
    }

    IO::AsyncTask<UIntSize> GatedFileLeaf(const std::shared_ptr<void>& state, Async::TaskContext& context, std::span<Byte> bytes)
    {
        auto file = std::static_pointer_cast<FileState>(state);
        IO::detail::FileOperationLease lease(*file, context, IO::detail::FileOperationGate::Kind::Independent);
        if (auto rejected = co_await lease)
        {
            co_await IO::AsyncTaskVoid::FromCompletion(std::move(*rejected));
            co_return 0;
        }
        co_return bytes.size();
    }

    // Counts submissions and reservation acquisition without changing production headers.
    // Reserved deliveries are reusable; reservation count is not delivery-hop count.
    struct CountedExecutor
    {
        Execution::ExecutorRef inner;
        std::size_t            submissions {};
        std::size_t            reservations {};
        std::size_t            deliveries {};
        Execution::WorkItem    CountDelivery(Execution::WorkItem work)
        {
            return Execution::WorkItem([this, work = std::move(work)]() mutable {
                ++deliveries;
                work.Invoke();
            });
        }
        Execution::ScheduleResult Execute(Execution::WorkItem work) noexcept
        {
            ++submissions;
            return inner.Execute(CountDelivery(std::move(work)));
        }
        Execution::ScheduleResult ExecuteAt(Execution::WorkItem work, Time::TimePoint time) noexcept
        {
            ++submissions;
            return inner.ExecuteAt(CountDelivery(std::move(work)), time);
        }
        std::expected<Execution::CompletionReservation, Execution::ScheduleError>
        ReserveCompletion(Execution::WorkItem work) noexcept
        {
            ++reservations;
            return inner.ReserveCompletion(CountDelivery(std::move(work)));
        }
        bool IsCurrent() const noexcept { return inner.IsCurrent(); }
    };

    template<typename Pump>
    void Drive(Pump& pump, Async::Operation<int>& operation)
    {
        while (!operation.IsCompleted())
            pump();
    }

    template<typename F>
    void Measure(const std::string& name, F&& work)
    {
        constexpr int samples = 1000;
        for (int i = 0; i < 100; ++i)
            work();
        std::vector<double> times;
        times.reserve(samples);
#ifdef NGIN_ASYNC_MEASURE_ALLOCATIONS
        const auto before = Benchmarks::Allocations::Snapshot();
#endif
        for (int i = 0; i < samples; ++i)
        {
            const auto start = Clock::now();
            work();
            times.push_back(std::chrono::duration<double, std::micro>(Clock::now() - start).count());
        }
#ifdef NGIN_ASYNC_MEASURE_ALLOCATIONS
        const auto after = Benchmarks::Allocations::Snapshot();
#endif
        double total = 0;
        for (double time: times)
            total += time;
        std::sort(times.begin(), times.end());
        std::cout << name << " samples=" << samples << " mean_us=" << total / samples
                  << " p50_us=" << times[samples / 2] << " p95_us=" << times[samples * 95 / 100]
                  << " p99_us=" << times[samples * 99 / 100];
#ifdef NGIN_ASYNC_MEASURE_ALLOCATIONS
        std::cout << " allocations_per_sample=" << double(after.totalCount - before.totalCount) / samples
                  << " bytes_per_sample=" << double(after.totalBytes - before.totalBytes) / samples;
#endif
        std::cout << '\n';
    }
    void MeasureFileLayers()
    {
        IO::Runtime                     runtime;
        auto                            driver = IO::detail::AcquireFileSystemDriver(runtime);
        std::shared_ptr<void>           state  = std::make_shared<FileState>(*driver);
        IO::AsyncFileHandle::Operations functions {};
        functions.read = GatedFileLeaf;
        IO::AsyncFileHandle handle(state, &functions);
        Async::TaskContext  context(runtime.GetExecutor());
        std::span<Byte>     bytes;
        for (int workers: {0, 1, 4})
        {
            std::unique_ptr<Execution::ThreadPoolScheduler> external;
            if (workers)
            {
                external = std::make_unique<Execution::ThreadPoolScheduler>(workers);
                context.BindExecutor(*external);
            }
            else
                context.BindExecutor(runtime.GetExecutor());
            for (int layer = 0; layer != 3; ++layer)
                Measure("file-synthetic workers=" + std::to_string(workers) + " layer=" + std::to_string(layer), [&] {
                    auto task   = layer == 0 ? FileLeaf(state, context, bytes) : layer == 1 ? GatedFileLeaf(state, context, bytes)
                                                                                            : handle.ReadAsync(context, bytes);
                    auto result = [&] {
                        if (external)
                            return Async::SyncWait(context, std::move(task));
                        auto operationTask = std::move(task);

                        NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

                        auto operation = operationRoot.Spawn(std::move(operationTask)).value();
                        while (!operation.IsCompleted())
                            runtime.PollOnce();
                        if (!operationRoot.TryJoin())
                            throw std::runtime_error("benchmark root did not retire");
                        return operation.TakeResult();
                    }();
                    if (!result.Succeeded())
                        throw std::runtime_error("synthetic file operation failed");
                });
        }
        runtime.Shutdown();
    }
}// namespace

int main(int argc, char** argv)
{
    using namespace NGIN;
    std::cout << "promise_bytes=" << sizeof(Async::Task<int>::promise_type)
              << " execution_state_bytes=" << sizeof(Async::detail::PromiseRuntimeCommon::ExecutionState)
              << " task_bytes=" << sizeof(Async::Task<int>)
              << " context_bytes=" << sizeof(Async::TaskContext)
              << " environment_bytes=" << sizeof(Async::AsyncEnvironment)
              << " frame_bytes=unavailable (promise size excludes compiler frame storage)\n";
    if (argc != 1)
    {
        if (argc == 2 && std::string_view(argv[1]) == "--file-only")
        {
            MeasureFileLayers();
            return 0;
        }
        std::cerr << "usage: " << argv[0] << " [--file-only]\n";
        return 2;
    }
    Async::TaskContext readyContext(Execution::ExecutorRef {});
    auto               environment = readyContext.GetEnvironment();
    Measure("environment-copy-empty", [&] {
        auto copy = environment;
        if (copy.metadata || copy.deadline || copy.priority || copy.cancellation.HasState())
            throw std::runtime_error("default environment mismatch");
    });
    Measure("ready-value-run", [&] {
        auto result = Async::SyncWait(readyContext, Async::Task<int>::FromValue(42));
        if (!result.Succeeded() || result.Value() != 42)
            throw std::runtime_error("ready value mismatch");
    });
    Measure("ready-void-run", [&] {
        if (!Async::SyncWait(readyContext, Async::Task<void>::FromValue()).Succeeded())
            throw std::runtime_error("ready void mismatch");
    });
    Measure("ready-error-run", [&] {
        auto result = Async::SyncWait(readyContext, Async::Task<int>::FromCompletion(
                                                            Async::Completion<int, Async::NoError>::Faulted(
                                                                    Async::MakeAsyncFault(Async::AsyncFaultCode::UnknownRuntimeFailure))));
        if (!result.Error().IsFault())
            throw std::runtime_error("ready fault mismatch");
    });
    Measure("ready-domain-run", [&] {
        auto result = Async::SyncWait(readyContext, Async::Task<int, int>::FromCompletion(
                                                            Async::Completion<int, int>::DomainFailure(7)));
        if (!result.Error().IsDomainError() || result.Error().DomainError() != 7)
            throw std::runtime_error("ready domain error mismatch");
    });
    Measure("ready-stopped-run", [&] {
        auto result = Async::SyncWait(readyContext, Async::Task<int>::FromCompletion(
                                                            Async::Completion<int, Async::NoError>::Stopped()));
        if (!result.IsStopped())
            throw std::runtime_error("ready stopped mismatch");
    });
    for (int workers: {1, 4})
    {
        Execution::ThreadPoolScheduler scheduler(workers);
        Async::CancellationSource      source;
        Async::TaskContext             context(scheduler);
        for (int mode = 0; mode != 3; ++mode)
        {
            if (mode != 0)
                context.BindCancellationToken(source.GetToken());
            if (mode == 2)
                source.Cancel();
            auto rootEnvironment         = context.GetEnvironment();
            rootEnvironment.cancellation = {};
            Async::TaskContext rootContext(rootEnvironment);
            for (int depth: {1, 8, 64})
            {
                const std::string label = "workers=" + std::to_string(workers) + " cancellation=" +
                                          std::to_string(mode) + " depth=" + std::to_string(depth);
                Measure("construction " + label, [&] { auto task = Chain(depth, context, false); });
                for (bool pending: {false, true})
                {
                    Measure(std::string(pending ? "yield " : "child ") + label, [&] {
                        auto result = Async::SyncWait(rootContext, Chain(depth, context, pending));
                        if ((!pending || mode != 2) && (!result.Succeeded() || result.Value() != depth))
                            throw std::runtime_error("chain produced an incorrect result");
                        if (mode == 2 && pending && !result.IsStopped())
                            throw std::runtime_error("canceled yield was not observed");
                    });
                }
            }
            Measure("context-copy workers=" + std::to_string(workers) + " cancellation=" + std::to_string(mode), [&] {
                Async::TaskContext copy = context;
                if (copy.GetExecutor() != context.GetExecutor())
                    throw std::runtime_error("context copy changed executor");
            });
        }
    }
    for (int mode = 0; mode != 3; ++mode)
    {
        Async::CancellationSource source;
        if (mode == 2)
            source.Cancel();
        const Async::CancellationToken token = mode == 0 ? Async::CancellationToken {} : source.GetToken();
        Measure("registration cancellation=" + std::to_string(mode), [&] {
            Async::CancellationRegistration registration;
            auto                            result = token.Register(registration, {}, {}, +[](void*) noexcept { return false; });
            if (!result)
                throw std::runtime_error("callback registration failed");
        });
        Measure("link cancellation=" + std::to_string(mode), [&] {
            Async::LinkedCancellationSource linked({token});
            if (linked.IsCancellationRequested() != (mode == 2))
                throw std::runtime_error("linked source lost cancellation");
        });
    }
    {
        IO::Runtime        runtime;
        Async::TaskContext context(runtime.GetExecutor());
        auto               pump = [&] { runtime.PollOnce(); };
        for (int mode = 0; mode != 3; ++mode)
        {
            Async::CancellationSource source;
            context.BindCancellationToken(mode == 0 ? Async::CancellationToken {} : source.GetToken());
            if (mode == 2)
                source.Cancel();
            auto rootEnvironment         = context.GetEnvironment();
            rootEnvironment.cancellation = {};
            Async::TaskContext rootContext(rootEnvironment);
            for (int depth: {1, 8, 64})
                for (bool pending: {false, true})
                    Measure(std::string("runtime ") + (pending ? "yield" : "child") + " depth=" + std::to_string(depth) +
                                    " cancellation=" + std::to_string(mode),
                            [&] {
                                auto operationTask = Chain(depth, context, pending);

                                NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(rootContext.GetEnvironment(), 1);

                                auto operation = operationRoot.Spawn(std::move(operationTask)).value();
                                Drive(pump, operation);
                                if (!operationRoot.TryJoin())
                                    throw std::runtime_error("benchmark root did not retire");
                                auto result = operation.TakeResult();
                                if ((!pending || mode != 2) && (!result.Succeeded() || result.Value() != depth))
                                    throw std::runtime_error("runtime chain produced an incorrect result");
                                if (pending && mode == 2 && !result.IsStopped())
                                    throw std::runtime_error("runtime yield missed cancellation");
                            });
        }
        runtime.Shutdown();
    }
    {
        Execution::CooperativeScheduler scheduler;
        CountedExecutor                 counted {Execution::ExecutorRef::From(scheduler)};
        Async::TaskContext              context(counted);
        auto                            pump = [&] { scheduler.RunUntilIdle(); };
        for (int depth: {1, 8, 64})
        {
            counted.submissions = counted.reservations = counted.deliveries = 0;
            auto operationTask                                              = Chain(depth, context, false);

            NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

            auto operation = operationRoot.Spawn(std::move(operationTask)).value();
            Drive(pump, operation);
            if (!operationRoot.TryJoin())
                throw std::runtime_error("benchmark root did not retire");
            if (!operation.TakeResult().Succeeded())
                throw std::runtime_error("counted chain failed");
            std::cout << "structural depth=" << depth << " submissions=" << counted.submissions
                      << " reservations=" << counted.reservations << " deliveries=" << counted.deliveries << '\n';
        }
    }
    MeasureFileLayers();
    for (std::size_t batch: {16U, 64U, 256U})
    {
        IO::Runtime        runtime(IO::Runtime::Options {.batchSize = batch});
        Async::TaskContext context(runtime.GetExecutor());
        int                timerAt = 0, unrelatedAt = 0, polls = 0, maxEntries = 0;
        Measure("fairness depth=4096 batch=" + std::to_string(batch), [&] {
            int entered = 0;
            timerAt = unrelatedAt = polls = maxEntries = 0;
            auto operationTask                         = FairChain(4096, entered);

            NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

            auto operation = operationRoot.Spawn(std::move(operationTask)).value();
            runtime.PollOnce();
            ++polls;
            maxEntries = entered;
            if (!context.GetExecutor().ExecuteAt(Execution::WorkItem([&] { timerAt = entered; }),
                                                 Time::MonotonicClock::Now()) ||
                !context.GetExecutor().Execute(Execution::WorkItem([&] { unrelatedAt = entered; })))
                throw std::runtime_error("fairness probe admission failed");
            while (!operation.IsCompleted() || timerAt == 0 || unrelatedAt == 0)
            {
                const int before = entered;
                runtime.PollOnce();
                ++polls;
                maxEntries = std::max(maxEntries, entered - before);
                if (polls > 20000)
                    throw std::runtime_error("fairness chain failed to drain");
            }
            if (!operationRoot.TryJoin())
                throw std::runtime_error("benchmark root did not retire");
            if (operation.TakeResult().Value() != 4096 || timerAt >= 4096 || unrelatedAt >= 4096 ||
                maxEntries > static_cast<int>(batch))
                throw std::runtime_error("fairness progress or poll work bound violated");
        });
        std::cout << "fairness-progress batch=" << batch << " timer_at_entry=" << timerAt
                  << " unrelated_at_entry=" << unrelatedAt << " polls=" << polls
                  << " max_entries_per_poll=" << maxEntries << '\n';
        runtime.Shutdown();
    }
    for (int depth: {1, 8, 64})
    {
        Execution::CooperativeScheduler scheduler;
        Measure("active-cancel depth=" + std::to_string(depth), [&] {
            Async::CancellationSource source;
            Async::TaskContext        context(scheduler, source.GetToken());
            auto                      operationTask = PendingChain(depth, context);

            NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

            auto operation = operationRoot.Spawn(std::move(operationTask)).value();
            scheduler.RunUntilIdle();
            if (operation.IsCompleted())
                throw std::runtime_error("pending fixture did not suspend");
            source.Cancel();
            scheduler.RunUntilIdle();
            if (!operationRoot.TryJoin())
                throw std::runtime_error("benchmark root did not retire");
            if (!operation.IsCompleted() || !operation.TakeResult().IsStopped())
                throw std::runtime_error("pending cancellation did not retire");
        });
        Measure("scope-spawn-join depth=" + std::to_string(depth), [&] {
            Async::TaskContext context(scheduler);
            Async::TaskScope   scope(context.GetExecutor(), {}, 1);
            auto               admitted = scope.Spawn([depth](Async::TaskContext& child) { return Chain(depth, child, false); });
            if (!admitted)
                throw std::runtime_error("scope admission failed");
            auto joiningTask = ScopeJoin(context, scope);

            NGIN::Async::TaskSupervisor<typename decltype(joiningTask)::ErrorType> joiningRoot(context.GetEnvironment(), 1);

            auto joining = joiningRoot.Spawn(std::move(joiningTask)).value();
            scheduler.RunUntilIdle();
            if (!joiningRoot.TryJoin())
                throw std::runtime_error("benchmark root did not retire");
            if (!joining.IsCompleted() || joining.TakeResult().Value() != 1 || scope.Pending() != 0)
                throw std::runtime_error("scope did not join");
        });
    }
}
