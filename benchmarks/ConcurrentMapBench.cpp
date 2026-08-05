#include <NGIN/Benchmark.hpp>
#include <NGIN/Containers/ConcurrentHashMap.hpp>
#include <NGIN/Units.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef NGIN_HAVE_TBB
#include <tbb/concurrent_unordered_map.h>
#endif

namespace
{
    using NGIN::Containers::ConcurrentHashMap;
    using NGIN::Containers::ReclamationPolicy;

    struct WorkloadConfig
    {
        int threads;
        int keyCount;
        int operationsPerThread;
    };

    enum class Workload
    {
        ReadHeavy,
        WriteHeavy,
        Mixed,
        ReclamationHeavy,
    };

    constexpr WorkloadConfig CONFIGS[] {
            {1, 1'024, 500},
            {4, 4'096, 500},
    };

    [[nodiscard]] constexpr const char* PolicyName(ReclamationPolicy policy) noexcept
    {
        switch (policy)
        {
            case ReclamationPolicy::ManualQuiesce:
                return "ManualQuiesce";
            case ReclamationPolicy::HazardPointers:
                return "HazardPointers";
            case ReclamationPolicy::LocalEpoch:
                return "LocalEpoch";
        }
        return "Unknown";
    }

    [[nodiscard]] constexpr const char* WorkloadName(Workload workload) noexcept
    {
        switch (workload)
        {
            case Workload::ReadHeavy:
                return "ReadHeavy95";
            case Workload::WriteHeavy:
                return "WriteHeavy75";
            case Workload::Mixed:
                return "Mixed75Read";
            case Workload::ReclamationHeavy:
                return "ReclamationHeavy";
        }
        return "Unknown";
    }

    template<ReclamationPolicy Policy>
    void RegisterWorkload(const Workload workload, const WorkloadConfig config)
    {
        using Map = ConcurrentHashMap<int, int, std::hash<int>, std::equal_to<int>,
                                      NGIN::Memory::SystemAllocator, Policy>;

        const std::string name = std::string {"NGIN.ConcurrentHashMap."} + PolicyName(Policy) + "." +
                                 WorkloadName(workload) + ".t=" + std::to_string(config.threads);
        NGIN::Benchmark::Register(
                [workload, config](NGIN::BenchmarkContext& context) {
                    Map map {static_cast<std::size_t>(config.keyCount * 2)};
                    for (int key = 0; key < config.keyCount; ++key)
                        map.Insert(key, key);

                    std::atomic<std::uint64_t> checksum {0};
                    context.start();
                    std::vector<std::thread> threads;
                    threads.reserve(static_cast<std::size_t>(config.threads));
                    for (int threadIndex = 0; threadIndex < config.threads; ++threadIndex)
                    {
                        threads.emplace_back([&, threadIndex] {
                            std::mt19937                       random {0x4E47494EU + static_cast<unsigned>(threadIndex)};
                            std::uniform_int_distribution<int> keys {0, config.keyCount - 1};
                            std::uint64_t                      localChecksum = 0;
                            for (int operation = 0; operation < config.operationsPerThread; ++operation)
                            {
                                const int key      = keys(random);
                                const int selector = operation % 20;
                                int       value    = 0;
                                switch (workload)
                                {
                                    case Workload::ReadHeavy:
                                        if (selector == 0)
                                            map.InsertOrAssign(key, operation);
                                        else if (map.TryGet(key, value))
                                            localChecksum += static_cast<std::uint64_t>(value);
                                        break;
                                    case Workload::WriteHeavy:
                                        if (selector < 15)
                                            map.InsertOrAssign(key, operation);
                                        else if (map.TryGet(key, value))
                                            localChecksum += static_cast<std::uint64_t>(value);
                                        break;
                                    case Workload::Mixed:
                                        if (selector < 5)
                                            map.InsertOrAssign(key, operation);
                                        else if (map.TryGet(key, value))
                                            localChecksum += static_cast<std::uint64_t>(value);
                                        break;
                                    case Workload::ReclamationHeavy:
                                        if ((operation & 1) == 0)
                                            map.Remove(key);
                                        else
                                            map.InsertOrAssign(key, operation);
                                        break;
                                }
                            }
                            checksum.fetch_add(localChecksum, std::memory_order_relaxed);
                        });
                    }
                    for (auto& thread: threads)
                        thread.join();
                    if constexpr (Policy == ReclamationPolicy::ManualQuiesce)
                        map.Quiesce();
                    context.stop();
                    if (checksum.load(std::memory_order_relaxed) == UINT64_MAX)
                        std::cerr << "unreachable checksum\n";
                },
                name);
    }

#ifdef NGIN_HAVE_TBB
    void RegisterTbbMixed(const WorkloadConfig config)
    {
        const std::string name = "TBB.concurrent_unordered_map.Mixed75Read.t=" +
                                 std::to_string(config.threads);
        NGIN::Benchmark::Register(
                [config](NGIN::BenchmarkContext& context) {
                    tbb::concurrent_unordered_map<int, int> map;
                    for (int key = 0; key < config.keyCount; ++key)
                        map.emplace(key, key);
                    context.start();
                    std::vector<std::thread> threads;
                    threads.reserve(static_cast<std::size_t>(config.threads));
                    for (int threadIndex = 0; threadIndex < config.threads; ++threadIndex)
                    {
                        threads.emplace_back([&, threadIndex] {
                            std::mt19937                       random {0x54424221U + static_cast<unsigned>(threadIndex)};
                            std::uniform_int_distribution<int> keys {0, config.keyCount - 1};
                            for (int operation = 0; operation < config.operationsPerThread; ++operation)
                            {
                                const int key = keys(random);
                                if ((operation % 4) == 0)
                                    map.insert_or_assign(key, operation);
                                else
                                    (void) map.find(key);
                            }
                        });
                    }
                    for (auto& thread: threads)
                        thread.join();
                    context.stop();
                },
                name);
    }
#endif
}// namespace

int main()
{
    for (const auto config: CONFIGS)
    {
        RegisterWorkload<ReclamationPolicy::LocalEpoch>(Workload::ReadHeavy, config);
        RegisterWorkload<ReclamationPolicy::LocalEpoch>(Workload::WriteHeavy, config);
        RegisterWorkload<ReclamationPolicy::LocalEpoch>(Workload::Mixed, config);
        RegisterWorkload<ReclamationPolicy::LocalEpoch>(Workload::ReclamationHeavy, config);
        RegisterWorkload<ReclamationPolicy::HazardPointers>(Workload::ReclamationHeavy, config);
        RegisterWorkload<ReclamationPolicy::ManualQuiesce>(Workload::ReclamationHeavy, config);
#ifdef NGIN_HAVE_TBB
        RegisterTbbMixed(config);
#endif
    }

    NGIN::Benchmark::defaultConfig.iterations       = 2;
    NGIN::Benchmark::defaultConfig.warmupIterations = 1;
    const auto results                              = NGIN::Benchmark::RunAll<NGIN::Units::Milliseconds>();
    NGIN::Benchmark::PrintSummaryTable(std::cout, results);
    return 0;
}
