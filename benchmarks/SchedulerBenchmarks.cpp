#include <NGIN/Benchmark.hpp>
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
        Benchmark::Register([](BenchmarkContext& ctx) {
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

            ctx.start();
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
            ctx.stop();
        },
                            "FiberScheduler schedule+complete 10k coroutines");

        Benchmark::Register([](BenchmarkContext& ctx) {
            NGIN::Execution::FiberScheduler scheduler(numThreads, numFibers);
            std::atomic<int> completed {0};

            auto job = [&completed]() noexcept {
                completed.fetch_add(1, std::memory_order_release);
                completed.notify_one();
            };

            ctx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                scheduler.Execute(NGIN::Execution::WorkItem(NGIN::Utilities::Callable<void()>(job)));
            }

            auto value = completed.load(std::memory_order_acquire);
            while (value < numCoroutines)
            {
                completed.wait(value);
                value = completed.load(std::memory_order_acquire);
            }
            ctx.stop();
        },
                            "FiberScheduler enqueue+run 10k jobs");

        Benchmark::Register([](BenchmarkContext& ctx) {
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
                        scheduler.Execute(NGIN::Execution::WorkItem(NGIN::Utilities::Callable<void()>(job)));
                    }
                });
            }

            auto readyValue = ready.load(std::memory_order_acquire);
            while (readyValue < numProducers)
            {
                ready.wait(readyValue);
                readyValue = ready.load(std::memory_order_acquire);
            }

            ctx.start();
            go.store(true, std::memory_order_release);
            go.notify_all();

            auto value = completed.load(std::memory_order_acquire);
            while (value < numCoroutines)
            {
                completed.wait(value);
                value = completed.load(std::memory_order_acquire);
            }
            ctx.stop();

            for (auto& t: producers)
            {
                t.join();
            }
        },
                            "FiberScheduler contended enqueue+run 10k jobs (4 producers)");

        Benchmark::Register([](BenchmarkContext& ctx) {
            NGIN::Execution::FiberScheduler scheduler(numThreads, numFibers);
            const auto nowNanos  = NGIN::Time::MonotonicClock::Now().ToNanoseconds();
            const auto farFuture = NGIN::Time::TimePoint::FromNanoseconds(nowNanos + 60ull * 1'000'000'000ull);

            ctx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                const auto resumeAt = NGIN::Time::TimePoint::FromNanoseconds(farFuture.ToNanoseconds() + static_cast<UInt64>(i));
                scheduler.ExecuteAt(NGIN::Execution::WorkItem(NGIN::Utilities::Callable<void()>([]() noexcept {})), resumeAt);
            }
            ctx.stop();
        },
                            "FiberScheduler ExecuteAt enqueue 10k timers");
    }

    // ThreadPoolScheduler benchmark
    if constexpr (runThreadPoolScheduler)
    {
        Benchmark::Register([](BenchmarkContext& ctx) {
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

            ctx.start();
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
            ctx.stop();
        },
                            "ThreadPoolScheduler schedule+complete 10k coroutines");

        Benchmark::Register([](BenchmarkContext& ctx) {
            NGIN::Execution::ThreadPoolScheduler scheduler(numThreads);
            std::atomic<int> completed {0};

            auto job = [&completed]() noexcept {
                completed.fetch_add(1, std::memory_order_release);
                completed.notify_one();
            };

            ctx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                scheduler.Execute(NGIN::Execution::WorkItem(NGIN::Utilities::Callable<void()>(job)));
            }

            auto value = completed.load(std::memory_order_acquire);
            while (value < numCoroutines)
            {
                completed.wait(value);
                value = completed.load(std::memory_order_acquire);
            }
            ctx.stop();
        },
                            "ThreadPoolScheduler enqueue+run 10k jobs");

        Benchmark::Register([](BenchmarkContext& ctx) {
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
                        scheduler.Execute(NGIN::Execution::WorkItem(NGIN::Utilities::Callable<void()>(job)));
                    }
                });
            }

            auto readyValue = ready.load(std::memory_order_acquire);
            while (readyValue < numProducers)
            {
                ready.wait(readyValue);
                readyValue = ready.load(std::memory_order_acquire);
            }

            ctx.start();
            go.store(true, std::memory_order_release);
            go.notify_all();

            auto value = completed.load(std::memory_order_acquire);
            while (value < numCoroutines)
            {
                completed.wait(value);
                value = completed.load(std::memory_order_acquire);
            }
            ctx.stop();

            for (auto& t: producers)
            {
                t.join();
            }
        },
                            "ThreadPoolScheduler contended enqueue+run 10k jobs (4 producers)");

        Benchmark::Register([](BenchmarkContext& ctx) {
            NGIN::Execution::ThreadPoolScheduler scheduler(numThreads);
            const auto nowNanos  = NGIN::Time::MonotonicClock::Now().ToNanoseconds();
            const auto farFuture = NGIN::Time::TimePoint::FromNanoseconds(nowNanos + 60ull * 1'000'000'000ull);

            ctx.start();
            for (int i = 0; i < numCoroutines; ++i)
            {
                const auto resumeAt = NGIN::Time::TimePoint::FromNanoseconds(farFuture.ToNanoseconds() + static_cast<UInt64>(i));
                scheduler.ExecuteAt(NGIN::Execution::WorkItem(NGIN::Utilities::Callable<void()>([]() noexcept {})), resumeAt);
            }
            ctx.stop();
        },
                            "ThreadPoolScheduler ExecuteAt enqueue 10k timers");
    }

    // Run all benchmarks and print results
    Benchmark::defaultConfig.iterations       = 100;
    Benchmark::defaultConfig.warmupIterations = 5;
    auto results                              = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);
    return 0;
}
