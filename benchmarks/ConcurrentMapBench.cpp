#include <NGIN/Benchmark.hpp>
#include <NGIN/Containers/ConcurrentHashMap.hpp>
#include <random>
#include <vector>
#include <thread>
#include <string>
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <cstdlib>// (left intentionally; remove if no other file uses getenv)
#include <cstddef>
#include <atomic>
#include <NGIN/Units.hpp>

// Compile-time diagnostics toggle. Set to 1 to enable instrumentation output without runtime getenv.
#ifndef NGIN_MAP_DIAGNOSTICS
#define NGIN_MAP_DIAGNOSTICS 1
#endif

#ifdef NGIN_HAVE_TBB
#include <tbb/concurrent_unordered_map.h>
#endif

using namespace NGIN;

namespace
{
    struct WorkloadConfig
    {
        int threads;
        int keyCount;
        int opsPerThread;
    };

    std::vector<int> MakeKeys(int n)
    {
        std::vector<int> v(n);
        for (int i = 0; i < n; ++i)
            v[i] = i;
        return v;
    }
}// namespace

int main() noexcept
{
    using Map = NGIN::Containers::ConcurrentHashMap<int, int>;

    const std::vector<WorkloadConfig> configs = {
            {1, 1000, 5000},
            {4, 5000, 5000},
            {8, 10000, 5000},
            {16, 10000, 5000},
            {64, 10000, 5000},
    };

    // Aggregated diagnostics across all NGIN map benchmark executions (Phase 0 baseline capture helper)
#if NGIN_MAP_DIAGNOSTICS
    struct AggDiag
    {
        std::atomic<unsigned long long> calls {0};
        std::atomic<unsigned long long> newInserts {0};
        std::atomic<unsigned long long> updates {0};
        std::atomic<unsigned long long> probeSteps {0};
        std::atomic<unsigned long long> maxProbe {0};
        std::atomic<unsigned long long> abandon {0};
        std::atomic<unsigned long long> locateYields {0};
        std::atomic<unsigned long long> locateBudgetAbandon {0};
        std::atomic<unsigned long long> locatePressureAbort {0};
    };
    static AggDiag gAgg;
#endif

    // Variant A: Mixed workload WITHOUT erase (portable to maps lacking safe concurrent erase)
    for (auto cfg: configs)
    {
        Benchmark::Register([cfg](BenchmarkContext& ctx) {
            Map          map(1024);
            const size_t reserveSize = static_cast<size_t>(cfg.keyCount) * 2ull;
            map.Reserve(reserveSize);
            ctx.start();
            std::vector<std::thread> ts;
            ts.reserve(static_cast<size_t>(cfg.threads));
            for (int t = 0; t < cfg.threads; ++t)
            {
                ts.emplace_back([&] {
                    std::mt19937                       rng(1111u + t);
                    std::uniform_int_distribution<int> dist(0, cfg.keyCount - 1);
                    for (int i = 0; i < cfg.opsPerThread; ++i)
                    {
                        const int k = dist(rng);
                        if ((i & 3) == 0)
                        {// 1/4 inserts (updates)
                            map.Insert(k, k);
                        }
                        else
                        {// 3/4 lookups
                            int out;
                            (void) map.TryGet(k, out);
                        }
                    }
                });
            }
            for (auto& th: ts)
                th.join();
            ctx.stop();
#if NGIN_MAP_DIAGNOSTICS
            {
                auto d = map.GetDiagnostics();
                std::cout << "Diagnostics t=" << cfg.threads
                          << " calls=" << d.insertCalls
                          << " new=" << d.insertSuccessNew
                          << " upd=" << d.insertSuccessUpdate
                          << " probeSteps=" << d.insertProbeSteps
                          << " maxProbe=" << d.insertMaxProbe
                          << " abandon=" << d.insertAbandon
                          << " locateYields=" << d.locateInsertYields
                          << " locateBudgetAbandon=" << d.locateInsertBudgetAbandon
                          << " locatePressureAbort=" << d.locateInsertPressureAbort
                          << '\n';
                gAgg.calls.fetch_add(d.insertCalls, std::memory_order_relaxed);
                gAgg.newInserts.fetch_add(d.insertSuccessNew, std::memory_order_relaxed);
                gAgg.updates.fetch_add(d.insertSuccessUpdate, std::memory_order_relaxed);
                gAgg.probeSteps.fetch_add(d.insertProbeSteps, std::memory_order_relaxed);
                unsigned long long observed = gAgg.maxProbe.load(std::memory_order_relaxed);
                while (observed < d.insertMaxProbe &&
                       !gAgg.maxProbe.compare_exchange_weak(observed, d.insertMaxProbe, std::memory_order_relaxed)) {}
                gAgg.abandon.fetch_add(d.insertAbandon, std::memory_order_relaxed);
                gAgg.locateYields.fetch_add(d.locateInsertYields, std::memory_order_relaxed);
                gAgg.locateBudgetAbandon.fetch_add(d.locateInsertBudgetAbandon, std::memory_order_relaxed);
                gAgg.locatePressureAbort.fetch_add(d.locateInsertPressureAbort, std::memory_order_relaxed);
            }
#endif
        },
                            "NGIN.ConcurrentHashMap MixedNoErase t=" + std::to_string(cfg.threads));
    }

    // Baseline: std::unordered_map protected by a single mutex (coarse grained lock)
    for (auto cfg: configs)
    {
        Benchmark::Register([cfg](BenchmarkContext& ctx) {
            std::unordered_map<int, int> map;
            map.reserve(static_cast<size_t>(cfg.keyCount) * 2ull);
            std::mutex mtx;
            ctx.start();
            std::vector<std::thread> ts;
            ts.reserve(static_cast<size_t>(cfg.threads));
            for (int t = 0; t < cfg.threads; ++t)
            {
                ts.emplace_back([&] {
                    std::mt19937                       rng(2222u + t);
                    std::uniform_int_distribution<int> dist(0, cfg.keyCount - 1);
                    for (int i = 0; i < cfg.opsPerThread; ++i)
                    {
                        const int k = dist(rng);
                        if ((i & 3) == 0)
                        {
                            std::lock_guard<std::mutex> lg(mtx);
                            map[k] = k;// insert or assign
                        }
                        else
                        {
                            std::lock_guard<std::mutex> lg(mtx);
                            auto                        it = map.find(k);
                            (void) it;
                        }
                    }
                });
            }
            for (auto& th: ts)
                th.join();
            ctx.stop();
        },
                            "Std.UnorderedMapMutex MixedNoErase t=" + std::to_string(cfg.threads));
    }


#ifdef NGIN_HAVE_TBB
    using TbbMap = tbb::concurrent_unordered_map<int, int>;
    for (auto cfg: configs)
    {
        Benchmark::Register([cfg](BenchmarkContext& ctx) {
            TbbMap map;
            map.reserve(static_cast<size_t>(cfg.keyCount) * 2ull);
            ctx.start();
            std::vector<std::thread> ts;
            ts.reserve(static_cast<size_t>(cfg.threads));
            for (int t = 0; t < cfg.threads; ++t)
            {
                ts.emplace_back([&] {
                    std::mt19937                       rng(5678u + t);
                    std::uniform_int_distribution<int> dist(0, cfg.keyCount - 1);
                    for (int i = 0; i < cfg.opsPerThread; ++i)
                    {
                        const int k = dist(rng);
                        if ((i & 3) == 0)
                        {
                            map.emplace(k, k);
                        }
                        else
                        {
                            auto it = map.find(k);
                            (void) it;
                        }
                    }
                });
            }
            for (auto& th: ts)
                th.join();
            ctx.stop();
        },
                            "TBB.concurrent_unordered_map MixedNoErase t=" + std::to_string(cfg.threads));
    }
#endif

#ifdef NGIN_HAVE_FOLLY
#include <folly/container/F14Map.h>
    using F14Map = folly::F14FastMap<int, int>;// NOTE: F14FastMap is NOT thread-safe; this is illustrative only.
    for (auto cfg: configs)
    {
        Benchmark::Register([cfg](BenchmarkContext& ctx) {
            F14Map map;
            map.reserve(cfg.keyCount * 2);
            ctx.start();
            std::vector<std::thread> ts;
            for (int t = 0; t < cfg.threads; ++t)
            {
                ts.emplace_back([&] {
                    std::mt19937                       rng(9142u + t);
                    std::uniform_int_distribution<int> dist(0, cfg.keyCount - 1);
                    for (int i = 0; i < cfg.opsPerThread; ++i)
                    {
                        int k = dist(rng);
                        if ((i & 3) == 0)
                        {
                            map.emplace(k, k);
                        }
                        else
                        {
                            auto it = map.find(k);
                            (void) it;
                        }
                    }
                });
            }
            for (auto& th: ts)
                th.join();
            ctx.stop();
        },
                            "Folly.F14FastMap MixedNoErase (NOT thread-safe) t=" + std::to_string(cfg.threads));
    }
#endif
    Benchmark::defaultConfig.iterations       = 5;
    Benchmark::defaultConfig.warmupIterations = 2;
    auto results                              = Benchmark::RunAll<Units::Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);
#if NGIN_MAP_DIAGNOSTICS
    std::cout << "AggregatedDiagnostics calls=" << gAgg.calls.load() << " new=" << gAgg.newInserts.load()
              << " upd=" << gAgg.updates.load() << " probeSteps=" << gAgg.probeSteps.load()
              << " maxProbe=" << gAgg.maxProbe.load() << " abandon=" << gAgg.abandon.load()
              << " locateYields=" << gAgg.locateYields.load()
              << " locateBudgetAbandon=" << gAgg.locateBudgetAbandon.load()
              << " locatePressureAbort=" << gAgg.locatePressureAbort.load() << '\n';
#endif
    return 0;
}
