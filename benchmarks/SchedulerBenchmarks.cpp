#include <NGIN/Benchmark.hpp>
#include <NGIN/Async/FiberScheduler.hpp>
#include <NGIN/Async/ThreadPoolScheduler.hpp>
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

    // Minimal coroutine type for benchmarking
    struct BenchTask
    {
        struct promise_type
        {
            BenchTask get_return_object()
            {
                return BenchTask {std::coroutine_handle<promise_type>::from_promise(*this)};
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
        std::coroutine_handle<promise_type> handle;
        explicit BenchTask(std::coroutine_handle<promise_type> h) : handle(h) {}
        BenchTask(BenchTask&& other) noexcept : handle(other.handle)
        {
            other.handle = nullptr;
        }
        ~BenchTask()
        {
            if (handle)
                handle.destroy();
        }
        BenchTask(const BenchTask&)            = delete;
        BenchTask& operator=(const BenchTask&) = delete;
    };

    // FiberScheduler benchmark
    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        NGIN::Async::FiberScheduler scheduler(numThreads, numFibers);
        std::atomic<int> completed {0};
        struct Awaitable
        {
            NGIN::Async::FiberScheduler& sched;
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
                ++completed;
            }
        };
        auto bench_coro = [](NGIN::Async::FiberScheduler& sched, std::atomic<int>& completed) -> BenchTask {
            co_await Awaitable {sched, completed};
        };
        std::vector<BenchTask> tasks;
        tasks.reserve(numCoroutines);
        for (int i = 0; i < numCoroutines; ++i)
        {
            tasks.emplace_back(bench_coro(scheduler, completed));
        }
        while (completed.load() < numCoroutines)
        {
        }
        ctx.stop();

        // Prevent double-destroy: clear all handles before vector destruction
        for (auto& t: tasks)
            t.handle = nullptr;
    },
                        "FiberScheduler schedule+complete 10k coroutines");

    // ThreadPoolScheduler benchmark
    Benchmark::Register([](BenchmarkContext& ctx) {
        ctx.start();
        NGIN::Async::ThreadPoolScheduler scheduler(numThreads);
        std::atomic<int> completed {0};
        struct Awaitable
        {
            NGIN::Async::ThreadPoolScheduler& sched;
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
                ++completed;
            }
        };
        auto bench_coro = [](NGIN::Async::ThreadPoolScheduler& sched, std::atomic<int>& completed) -> BenchTask {
            co_await Awaitable {sched, completed};
        };
        std::vector<BenchTask> tasks;
        tasks.reserve(numCoroutines);
        for (int i = 0; i < numCoroutines; ++i)
        {
            tasks.emplace_back(bench_coro(scheduler, completed));
        }
        while (completed.load() < numCoroutines)
        {
        }
        ctx.stop();

        // Prevent double-destroy: clear all handles before vector destruction
        for (auto& t: tasks)
            t.handle = nullptr;
    },
                        "ThreadPoolScheduler schedule+complete 10k coroutines");

    // Run all benchmarks and print results
    Benchmark::defaultConfig.iterations       = 100;
    Benchmark::defaultConfig.warmupIterations = 5;
    auto results                              = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);
    return 0;
}
