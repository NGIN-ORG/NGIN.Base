#include <NGIN/Benchmark.hpp>
#include <NGIN/Execution/FiberScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <iostream>
#include <coroutine>
#include <atomic>
#include <thread>

int main()
{
    using namespace NGIN;
    constexpr int numCoroutines = 10000;
    constexpr int numThreads    = 4;
    constexpr int numFibers     = 128;
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
            ctx.start();
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
    }

    // ThreadPoolScheduler benchmark
    if constexpr (runThreadPoolScheduler)
    {
        Benchmark::Register([](BenchmarkContext& ctx) {
            ctx.start();
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
    }

    // Run all benchmarks and print results
    Benchmark::defaultConfig.iterations       = 100;
    Benchmark::defaultConfig.warmupIterations = 5;
    auto results                              = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);
    return 0;
}
