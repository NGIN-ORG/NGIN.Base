#include <NGIN/Benchmark.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <string_view>
#include <utility>

namespace
{
    int FreeFunction(int x)
    {
        return x + 1;
    }

    struct LargeFunctor
    {
        alignas(std::max_align_t) std::byte data[128] {};

        int operator()(int x) const
        {
            return x + static_cast<int>(data[0]);
        }
    };

    struct alignas(64) OverAlignedSmallFunctor
    {
        std::byte data[8] {};

        int operator()(int x) const
        {
            return x + static_cast<int>(data[0]);
        }
    };

    struct MoveOnlyCallable
    {
        std::unique_ptr<int> value;

        MoveOnlyCallable()
            : value(std::make_unique<int>(3)) {}

        MoveOnlyCallable(MoveOnlyCallable&&) noexcept = default;
        MoveOnlyCallable& operator=(MoveOnlyCallable&&) noexcept = default;

        MoveOnlyCallable(const MoveOnlyCallable&) = delete;
        MoveOnlyCallable& operator=(const MoveOnlyCallable&) = delete;

        int operator()(int x) const
        {
            return x + *value;
        }
    };

    template<typename F>
    void RegisterInvokeBenchmark(const NGIN::BenchmarkConfig& cfg, F&& makeCallable, std::string_view name)
    {
        NGIN::Benchmark::Register(cfg,
                                 [makeCallable = std::forward<F>(makeCallable)](NGIN::BenchmarkContext& ctx) mutable {
                                     constexpr int callsPerIteration = 1'000'000;
                                     auto callable                   = makeCallable();
                                     ctx.doNotOptimize(callable);
                                     int sum = 0;
                                     ctx.start();
                                     for (int i = 0; i < callsPerIteration; ++i)
                                     {
                                         sum += callable(i);
                                     }
                                     ctx.doNotOptimize(sum);
                                     ctx.stop();
                                 },
                                 name);
    }
}// namespace

int main()
{
    using NGIN::BenchmarkConfig;
    using NGIN::Units::Nanoseconds;

    BenchmarkConfig cfg;
    cfg.iterations        = 250;
    cfg.warmupIterations  = 50;
    cfg.accountForOverhead = true;
    cfg.keepRawTimings    = false;

    RegisterInvokeBenchmark(cfg,
                            [] {
                                return NGIN::Utilities::Callable<int(int)>(FreeFunction);
                            },
                            "Callable<int(int)> invoke (function ptr)");

    RegisterInvokeBenchmark(cfg,
                            [] {
                                return std::function<int(int)>(FreeFunction);
                            },
                            "std::function<int(int)> invoke (function ptr)");

    RegisterInvokeBenchmark(cfg,
                            [] {
                                int capture = 7;
                                return NGIN::Utilities::Callable<int(int)>([capture](int x) { return x + capture; });
                            },
                            "Callable<int(int)> invoke (small lambda)");

    RegisterInvokeBenchmark(cfg,
                            [] {
                                int capture = 7;
                                return std::function<int(int)>([capture](int x) { return x + capture; });
                            },
                            "std::function<int(int)> invoke (small lambda)");

    RegisterInvokeBenchmark(cfg,
                            [] {
                                LargeFunctor f {};
                                f.data[0] = std::byte {5};
                                return NGIN::Utilities::Callable<int(int)>(f);
                            },
                            "Callable<int(int)> invoke (heap LargeFunctor)");

    RegisterInvokeBenchmark(cfg,
                            [] {
                                LargeFunctor f {};
                                f.data[0] = std::byte {5};
                                return std::function<int(int)>(f);
                            },
                            "std::function<int(int)> invoke (LargeFunctor)");

    RegisterInvokeBenchmark(cfg,
                            [] {
                                OverAlignedSmallFunctor f {};
                                f.data[0] = std::byte {9};
                                return NGIN::Utilities::Callable<int(int)>(f);
                            },
                            "Callable<int(int)> invoke (heap over-aligned small)");

    RegisterInvokeBenchmark(cfg,
                            [] {
                                OverAlignedSmallFunctor f {};
                                f.data[0] = std::byte {9};
                                return std::function<int(int)>(f);
                            },
                            "std::function<int(int)> invoke (over-aligned small)");

#if defined(__cpp_lib_move_only_function) && (__cpp_lib_move_only_function >= 202110L)
    RegisterInvokeBenchmark(cfg,
                            [] {
                                return std::move_only_function<int(int)>(MoveOnlyCallable {});
                            },
                            "std::move_only_function<int(int)> invoke (move-only)");
#endif

    RegisterInvokeBenchmark(cfg,
                            [] {
                                return NGIN::Utilities::Callable<int(int)>(MoveOnlyCallable {});
                            },
                            "Callable<int(int)> invoke (move-only)");

    auto results = NGIN::Benchmark::RunAll<Nanoseconds>();
    NGIN::Benchmark::PrintSummaryTable(std::cout, results);
    return 0;
}
