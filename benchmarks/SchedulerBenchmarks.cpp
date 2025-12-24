#include <NGIN/Benchmark.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Async/WhenAll.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/FiberScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <iostream>
#include <coroutine>
#include <atomic>
#include <thread>
#include <vector>

int main()
{
    using namespace NGIN;
    constexpr int numCoroutines = 10000;
    constexpr int numThreads    = 4;
    constexpr int numFibers     = 128;
    constexpr int numProducers  = 4;
    constexpr int numYieldManyTasks  = 2000;
    constexpr int yieldsPerYieldMany = 8;
    constexpr bool runCooperativeScheduler = true;
    constexpr bool runFiberScheduler      = true;
    constexpr bool runThreadPoolScheduler = true;

    // Minimal coroutine type for benchmarking
    struct BenchTask
    {
        struct promise_type
        {
            BenchTask get_return_object() noexcept
            {
                return {};
            }
            std::suspend_never initial_suspend() noexcept
            {
                return {};
            }
            std::suspend_never final_suspend() noexcept
            {
                return {};
            }
            void return_void() noexcept {}
            void unhandled_exception()
            {
                std::terminate();
            }
        };
    };

    // FiberScheduler benchmark
    if constexpr (runFiberScheduler)
    {
        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::FiberScheduler scheduler(numThreads, numFibers);
            std::atomic<int> completed {0};
            struct Awaitable
            {
                NGIN::Execution::FiberScheduler& sched;
                std::atomic<int>& completed;
                bool await_ready() const noexcept
                {
                    return false;
                }
                void await_suspend(std::coroutine_handle<> h) const
                {
                    sched.Schedule(h);
                }
                void await_resume() const noexcept
                {
                }
            };
            auto bench_coro = [](NGIN::Execution::FiberScheduler& sched, std::atomic<int>& completed) -> BenchTask {
                co_await Awaitable {sched, completed};
                ++completed;
                completed.notify_one();
            };

            benchCtx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                bench_coro(scheduler, completed);
            }
            auto value = completed.load();
            while (value < numCoroutines)
            {
                completed.wait(value);
                value = completed.load();
            }
            benchCtx.stop();
        },
                            "FiberScheduler schedule+complete 10k coroutines");

        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::FiberScheduler scheduler(numThreads, numFibers);
            std::atomic<int> completed {0};

            NGIN::Async::TaskContext taskCtx(scheduler);

            auto taskYieldMany = [](NGIN::Async::TaskContext& ctx, std::atomic<int>& completed) -> NGIN::Async::Task<void> {
                for (int i = 0; i < yieldsPerYieldMany; ++i)
                {
                    co_await ctx.Yield();
                }
                completed.fetch_add(1, std::memory_order_release);
                completed.notify_one();
                co_return;
            };

            std::vector<NGIN::Async::Task<void>> tasks;
            tasks.reserve(numYieldManyTasks);

            benchCtx.start();
            for (int i = 0; i < numYieldManyTasks; ++i)
            {
                tasks.emplace_back(taskYieldMany(taskCtx, completed));
                tasks.back().Start(taskCtx);
            }

            auto value = completed.load(std::memory_order_acquire);
            while (value < numYieldManyTasks)
            {
                completed.wait(value);
                value = completed.load(std::memory_order_acquire);
            }
            benchCtx.stop();
        },
                            "FiberScheduler Task<void> Yield x8 2k");

        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::FiberScheduler scheduler(numThreads, numFibers);
            std::atomic<int> completed {0};

            auto job = [&completed]() noexcept {
                completed.fetch_add(1, std::memory_order_release);
                completed.notify_one();
            };

            benchCtx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                scheduler.Execute(NGIN::Execution::WorkItem(job));
            }

            auto value = completed.load(std::memory_order_acquire);
            while (value < numCoroutines)
            {
                completed.wait(value);
                value = completed.load(std::memory_order_acquire);
            }
            benchCtx.stop();
        },
                            "FiberScheduler enqueue+run 10k jobs");

        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::FiberScheduler scheduler(numThreads, numFibers);
            std::atomic<int> completed {0};
            std::atomic<int> ready {0};
            std::atomic<bool> go {false};

            auto job = [&completed]() noexcept {
                completed.fetch_add(1, std::memory_order_release);
                completed.notify_one();
            };

            std::vector<std::thread> producers;
            producers.reserve(numProducers);
            for (int p = 0; p < numProducers; ++p)
            {
                producers.emplace_back([&, p] {
                    ready.fetch_add(1, std::memory_order_release);
                    ready.notify_one();

                    while (!go.load(std::memory_order_acquire))
                    {
                        go.wait(false);
                    }

                    const int startIndex = (numCoroutines * p) / numProducers;
                    const int endIndex   = (numCoroutines * (p + 1)) / numProducers;
                    for (int i = startIndex; i < endIndex; ++i)
                    {
                        scheduler.Execute(NGIN::Execution::WorkItem(job));
                    }
                });
            }

            auto readyValue = ready.load(std::memory_order_acquire);
            while (readyValue < numProducers)
            {
                ready.wait(readyValue);
                readyValue = ready.load(std::memory_order_acquire);
            }

            benchCtx.start();
            go.store(true, std::memory_order_release);
            go.notify_all();

            auto value = completed.load(std::memory_order_acquire);
            while (value < numCoroutines)
            {
                completed.wait(value);
                value = completed.load(std::memory_order_acquire);
            }
            benchCtx.stop();

            for (auto& t: producers)
            {
                t.join();
            }
        },
                            "FiberScheduler contended enqueue+run 10k jobs (4 producers)");

        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::FiberScheduler scheduler(numThreads, numFibers);
            const auto nowNanos  = NGIN::Time::MonotonicClock::Now().ToNanoseconds();
            const auto farFuture = NGIN::Time::TimePoint::FromNanoseconds(nowNanos + 60ull * 1'000'000'000ull);

            benchCtx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                const auto resumeAt = NGIN::Time::TimePoint::FromNanoseconds(farFuture.ToNanoseconds() + static_cast<UInt64>(i));
                scheduler.ExecuteAt(NGIN::Execution::WorkItem([]() noexcept {}), resumeAt);
            }
            benchCtx.stop();
        },
                            "FiberScheduler ExecuteAt enqueue 10k timers");
    }

    // ThreadPoolScheduler benchmark
    if constexpr (runThreadPoolScheduler)
    {
        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::ThreadPoolScheduler scheduler(numThreads);
            std::atomic<int> completed {0};
            struct Awaitable
            {
                NGIN::Execution::ThreadPoolScheduler& sched;
                std::atomic<int>& completed;
                bool await_ready() const noexcept
                {
                    return false;
                }
                void await_suspend(std::coroutine_handle<> h) const
                {
                    sched.Schedule(h);
                }
                void await_resume() const noexcept
                {
                }
            };
            auto bench_coro = [](NGIN::Execution::ThreadPoolScheduler& sched, std::atomic<int>& completed) -> BenchTask {
                co_await Awaitable {sched, completed};
                ++completed;
                completed.notify_one();
            };

            benchCtx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                bench_coro(scheduler, completed);
            }
            auto value = completed.load();
            while (value < numCoroutines)
            {
                completed.wait(value);
                value = completed.load();
            }
            benchCtx.stop();
        },
                            "ThreadPoolScheduler schedule+complete 10k coroutines");

        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::ThreadPoolScheduler scheduler(numThreads);
            std::atomic<int> completed {0};

            auto job = [&completed]() noexcept {
                completed.fetch_add(1, std::memory_order_release);
                completed.notify_one();
            };

            benchCtx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                scheduler.Execute(NGIN::Execution::WorkItem(job));
            }

            auto value = completed.load(std::memory_order_acquire);
            while (value < numCoroutines)
            {
                completed.wait(value);
                value = completed.load(std::memory_order_acquire);
            }
            benchCtx.stop();
        },
                            "ThreadPoolScheduler enqueue+run 10k jobs");

        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::ThreadPoolScheduler scheduler(numThreads);
            std::atomic<int> completed {0};
            std::atomic<int> ready {0};
            std::atomic<bool> go {false};

            auto job = [&completed]() noexcept {
                completed.fetch_add(1, std::memory_order_release);
                completed.notify_one();
            };

            std::vector<std::thread> producers;
            producers.reserve(numProducers);
            for (int p = 0; p < numProducers; ++p)
            {
                producers.emplace_back([&, p] {
                    ready.fetch_add(1, std::memory_order_release);
                    ready.notify_one();

                    while (!go.load(std::memory_order_acquire))
                    {
                        go.wait(false);
                    }

                    const int startIndex = (numCoroutines * p) / numProducers;
                    const int endIndex   = (numCoroutines * (p + 1)) / numProducers;
                    for (int i = startIndex; i < endIndex; ++i)
                    {
                        scheduler.Execute(NGIN::Execution::WorkItem(job));
                    }
                });
            }

            auto readyValue = ready.load(std::memory_order_acquire);
            while (readyValue < numProducers)
            {
                ready.wait(readyValue);
                readyValue = ready.load(std::memory_order_acquire);
            }

            benchCtx.start();
            go.store(true, std::memory_order_release);
            go.notify_all();

            auto value = completed.load(std::memory_order_acquire);
            while (value < numCoroutines)
            {
                completed.wait(value);
                value = completed.load(std::memory_order_acquire);
            }
            benchCtx.stop();

            for (auto& t: producers)
            {
                t.join();
            }
        },
                            "ThreadPoolScheduler contended enqueue+run 10k jobs (4 producers)");

        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::ThreadPoolScheduler scheduler(numThreads);
            const auto nowNanos  = NGIN::Time::MonotonicClock::Now().ToNanoseconds();
            const auto farFuture = NGIN::Time::TimePoint::FromNanoseconds(nowNanos + 60ull * 1'000'000'000ull);

            benchCtx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                const auto resumeAt = NGIN::Time::TimePoint::FromNanoseconds(farFuture.ToNanoseconds() + static_cast<UInt64>(i));
                scheduler.ExecuteAt(NGIN::Execution::WorkItem([]() noexcept {}), resumeAt);
            }
            benchCtx.stop();
        },
                            "ThreadPoolScheduler ExecuteAt enqueue 10k timers");

        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::ThreadPoolScheduler scheduler(numThreads);
            std::atomic<int> completed {0};

            NGIN::Async::TaskContext taskCtx(scheduler);

            auto taskYield = [](NGIN::Async::TaskContext& ctx, std::atomic<int>& completed) -> NGIN::Async::Task<void> {
                co_await ctx.Yield();
                completed.fetch_add(1, std::memory_order_release);
                completed.notify_one();
                co_return;
            };

            std::vector<NGIN::Async::Task<void>> tasks;
            tasks.reserve(numCoroutines);

            benchCtx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                tasks.emplace_back(taskYield(taskCtx, completed));
                tasks.back().Start(taskCtx);
            }

            auto value = completed.load(std::memory_order_acquire);
            while (value < numCoroutines)
            {
                completed.wait(value);
                value = completed.load(std::memory_order_acquire);
            }
            benchCtx.stop();
        },
                            "ThreadPoolScheduler Task<void> Yield 10k");

        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::ThreadPoolScheduler scheduler(numThreads);
            std::atomic<int> completed {0};

            NGIN::Async::TaskContext taskCtx(scheduler);

            auto taskYieldMany = [](NGIN::Async::TaskContext& ctx, std::atomic<int>& completed) -> NGIN::Async::Task<void> {
                for (int i = 0; i < yieldsPerYieldMany; ++i)
                {
                    co_await ctx.Yield();
                }
                completed.fetch_add(1, std::memory_order_release);
                completed.notify_one();
                co_return;
            };

            std::vector<NGIN::Async::Task<void>> tasks;
            tasks.reserve(numYieldManyTasks);

            benchCtx.start();
            for (int i = 0; i < numYieldManyTasks; ++i)
            {
                tasks.emplace_back(taskYieldMany(taskCtx, completed));
                tasks.back().Start(taskCtx);
            }

            auto value = completed.load(std::memory_order_acquire);
            while (value < numYieldManyTasks)
            {
                completed.wait(value);
                value = completed.load(std::memory_order_acquire);
            }
            benchCtx.stop();
        },
                            "ThreadPoolScheduler Task<void> Yield x8 2k");

        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::ThreadPoolScheduler scheduler(numThreads);
            NGIN::Async::CancellationSource      cancelSource;
            NGIN::Async::TaskContext             taskCtx(scheduler, cancelSource.GetToken());
            std::atomic<int>                     completed {0};

            auto delayCanceled = [](NGIN::Async::TaskContext& ctx, std::atomic<int>& completed) -> NGIN::Async::Task<void> {
                try
                {
                    co_await ctx.Delay(NGIN::Units::Milliseconds(100.0));
                } catch (const NGIN::Async::TaskCanceled&)
                {
                }
                completed.fetch_add(1, std::memory_order_release);
                completed.notify_one();
                co_return;
            };

            std::vector<NGIN::Async::Task<void>> tasks;
            tasks.reserve(numCoroutines);

            benchCtx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                tasks.emplace_back(delayCanceled(taskCtx, completed));
                tasks.back().Start(taskCtx);
            }

            cancelSource.Cancel();

            auto value = completed.load(std::memory_order_acquire);
            while (value < numCoroutines)
            {
                completed.wait(value);
                value = completed.load(std::memory_order_acquire);
            }
            benchCtx.stop();
        },
                            "ThreadPoolScheduler Task<void> Delay(100ms) + cancel 10k");

        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::ThreadPoolScheduler scheduler(numThreads);
            NGIN::Async::TaskContext             taskCtx(scheduler);
            std::atomic<int>                     completed {0};

            auto leaf = [](NGIN::Async::TaskContext& ctx) -> NGIN::Async::Task<void> {
                co_await ctx.Yield();
                co_return;
            };

            auto coordinator = [](NGIN::Async::TaskContext& ctx, std::atomic<int>& completed, auto leaf) -> NGIN::Async::Task<void> {
                auto a = leaf(ctx);
                auto b = leaf(ctx);
                a.Start(ctx);
                b.Start(ctx);
                co_await NGIN::Async::WhenAll(ctx, a, b);
                completed.fetch_add(1, std::memory_order_release);
                completed.notify_one();
                co_return;
            };

            std::vector<NGIN::Async::Task<void>> tasks;
            tasks.reserve(numCoroutines);

            benchCtx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                tasks.emplace_back(coordinator(taskCtx, completed, leaf));
                tasks.back().Start(taskCtx);
            }

            auto value = completed.load(std::memory_order_acquire);
            while (value < numCoroutines)
            {
                completed.wait(value);
                value = completed.load(std::memory_order_acquire);
            }
            benchCtx.stop();
        },
                            "ThreadPoolScheduler Task WhenAll(2x Yield) 10k");
    }

    if constexpr (runCooperativeScheduler)
    {
        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::CooperativeScheduler scheduler;
            std::atomic<int> completed {0};

            auto job = [&completed]() noexcept {
                completed.fetch_add(1, std::memory_order_relaxed);
            };

            benchCtx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                scheduler.Execute(NGIN::Execution::WorkItem(job));
            }
            while (completed.load(std::memory_order_relaxed) < numCoroutines)
            {
                static_cast<void>(scheduler.RunOne());
            }
            benchCtx.stop();
        },
                            "CooperativeScheduler enqueue+run 10k jobs");

        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::CooperativeScheduler scheduler;
            std::atomic<int> completed {0};
            NGIN::Async::TaskContext taskCtx(scheduler);

            auto taskYield = [](NGIN::Async::TaskContext& ctx, std::atomic<int>& completed) -> NGIN::Async::Task<void> {
                co_await ctx.Yield();
                completed.fetch_add(1, std::memory_order_relaxed);
                co_return;
            };

            std::vector<NGIN::Async::Task<void>> tasks;
            tasks.reserve(numCoroutines);

            benchCtx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                tasks.emplace_back(taskYield(taskCtx, completed));
                tasks.back().Start(taskCtx);
            }
            while (completed.load(std::memory_order_relaxed) < numCoroutines)
            {
                static_cast<void>(scheduler.RunOne());
            }
            benchCtx.stop();
        },
                            "CooperativeScheduler Task<void> Yield 10k");

        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::CooperativeScheduler scheduler;
            std::atomic<int> completed {0};
            NGIN::Async::TaskContext taskCtx(scheduler);

            auto taskYieldMany = [](NGIN::Async::TaskContext& ctx, std::atomic<int>& completed) -> NGIN::Async::Task<void> {
                for (int i = 0; i < yieldsPerYieldMany; ++i)
                {
                    co_await ctx.Yield();
                }
                completed.fetch_add(1, std::memory_order_relaxed);
                co_return;
            };

            std::vector<NGIN::Async::Task<void>> tasks;
            tasks.reserve(numYieldManyTasks);

            benchCtx.start();
            for (int i = 0; i < numYieldManyTasks; ++i)
            {
                tasks.emplace_back(taskYieldMany(taskCtx, completed));
                tasks.back().Start(taskCtx);
            }
            while (completed.load(std::memory_order_relaxed) < numYieldManyTasks)
            {
                static_cast<void>(scheduler.RunOne());
            }
            benchCtx.stop();
        },
                            "CooperativeScheduler Task<void> Yield x8 2k");

        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::CooperativeScheduler scheduler;
            NGIN::Async::CancellationSource      cancelSource;
            NGIN::Async::TaskContext             taskCtx(scheduler, cancelSource.GetToken());
            std::atomic<int>                     completed {0};

            auto delayCanceled = [](NGIN::Async::TaskContext& ctx, std::atomic<int>& completed) -> NGIN::Async::Task<void> {
                try
                {
                    co_await ctx.Delay(NGIN::Units::Milliseconds(100.0));
                } catch (const NGIN::Async::TaskCanceled&)
                {
                }
                completed.fetch_add(1, std::memory_order_relaxed);
                co_return;
            };

            std::vector<NGIN::Async::Task<void>> tasks;
            tasks.reserve(numCoroutines);

            benchCtx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                tasks.emplace_back(delayCanceled(taskCtx, completed));
                tasks.back().Start(taskCtx);
            }

            cancelSource.Cancel();
            while (completed.load(std::memory_order_relaxed) < numCoroutines)
            {
                static_cast<void>(scheduler.RunOne());
            }
            benchCtx.stop();
        },
                            "CooperativeScheduler Task<void> Delay(100ms) + cancel 10k");

        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::CooperativeScheduler scheduler;
            NGIN::Async::TaskContext             taskCtx(scheduler);
            std::atomic<int>                     completed {0};

            auto leaf = [](NGIN::Async::TaskContext& ctx) -> NGIN::Async::Task<void> {
                co_await ctx.Yield();
                co_return;
            };

            auto coordinator = [](NGIN::Async::TaskContext& ctx, std::atomic<int>& completed, auto leaf) -> NGIN::Async::Task<void> {
                auto a = leaf(ctx);
                auto b = leaf(ctx);
                a.Start(ctx);
                b.Start(ctx);
                co_await NGIN::Async::WhenAll(ctx, a, b);
                completed.fetch_add(1, std::memory_order_relaxed);
                co_return;
            };

            std::vector<NGIN::Async::Task<void>> tasks;
            tasks.reserve(numCoroutines);

            benchCtx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                tasks.emplace_back(coordinator(taskCtx, completed, leaf));
                tasks.back().Start(taskCtx);
            }

            while (completed.load(std::memory_order_relaxed) < numCoroutines)
            {
                static_cast<void>(scheduler.RunOne());
            }
            benchCtx.stop();
        },
                            "CooperativeScheduler Task WhenAll(2x Yield) 10k");

        Benchmark::Register([](BenchmarkContext& benchCtx) {
            NGIN::Execution::CooperativeScheduler scheduler;
            const auto nowNanos  = NGIN::Time::MonotonicClock::Now().ToNanoseconds();
            const auto farFuture = NGIN::Time::TimePoint::FromNanoseconds(nowNanos + 60ull * 1'000'000'000ull);

            benchCtx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                const auto resumeAt = NGIN::Time::TimePoint::FromNanoseconds(farFuture.ToNanoseconds() + static_cast<UInt64>(i));
                scheduler.ExecuteAt(NGIN::Execution::WorkItem([]() noexcept {}), resumeAt);
            }
            benchCtx.stop();
        },
                            "CooperativeScheduler ExecuteAt enqueue 10k timers");
    }

    // Run all benchmarks and print results
    Benchmark::defaultConfig.iterations       = 100;
    Benchmark::defaultConfig.warmupIterations = 5;
    auto results                              = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);
    return 0;
}
