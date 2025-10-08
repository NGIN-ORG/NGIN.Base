/// @file ConcurrentHashMapStress.cpp
/// @brief Phase 5 concurrency & stress tests for ConcurrentHashMap using Catch2.

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#define private public
#define protected public
#include <NGIN/Containers/ConcurrentHashMap.hpp>
#undef private
#undef protected

using NGIN::Containers::ConcurrentHashMap;

namespace
{
    template<class Tag>
    struct TrackingRegistry
    {
        static void Reset() noexcept
        {
            ConstructedCount().store(0, std::memory_order_relaxed);
            DestroyedCount().store(0, std::memory_order_relaxed);
            DoubleDestroyCount().store(0, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(MutexRef());
            DestroyedAddresses().clear();
        }

        static void RecordConstruction(const void* instance) noexcept
        {
            ConstructedCount().fetch_add(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(MutexRef());
            DestroyedAddresses().erase(instance);
        }

        static void RecordDestruction(const void* instance) noexcept
        {
            DestroyedCount().fetch_add(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(MutexRef());
            const bool                  inserted = DestroyedAddresses().insert(instance).second;
            if (!inserted)
            {
                DoubleDestroyCount().fetch_add(1, std::memory_order_relaxed);
            }
        }

        [[nodiscard]] static int Alive() noexcept
        {
            return ConstructedCount().load(std::memory_order_relaxed) -
                   DestroyedCount().load(std::memory_order_relaxed);
        }

        [[nodiscard]] static int Constructed() noexcept
        {
            return ConstructedCount().load(std::memory_order_relaxed);
        }

        [[nodiscard]] static int Destroyed() noexcept
        {
            return DestroyedCount().load(std::memory_order_relaxed);
        }

        [[nodiscard]] static int DoubleDestroyed() noexcept
        {
            return DoubleDestroyCount().load(std::memory_order_relaxed);
        }

    private:
        static std::atomic<int>& ConstructedCount() noexcept
        {
            static std::atomic<int> counter {0};
            return counter;
        }

        static std::atomic<int>& DestroyedCount() noexcept
        {
            static std::atomic<int> counter {0};
            return counter;
        }

        static std::atomic<int>& DoubleDestroyCount() noexcept
        {
            static std::atomic<int> counter {0};
            return counter;
        }

        static std::unordered_set<const void*>& DestroyedAddresses() noexcept
        {
            static std::unordered_set<const void*> addresses;
            return addresses;
        }

        static std::mutex& MutexRef() noexcept
        {
            static std::mutex mutex;
            return mutex;
        }
    };

    struct TrackingKeyTag;
    struct TrackingValueTag;

    struct TrackingKey
    {
        int value {0};

        TrackingKey() noexcept
        {
            TrackingRegistry<TrackingKeyTag>::RecordConstruction(this);
        }

        explicit TrackingKey(int v) noexcept : value(v)
        {
            TrackingRegistry<TrackingKeyTag>::RecordConstruction(this);
        }

        TrackingKey(const TrackingKey& other) noexcept : value(other.value)
        {
            TrackingRegistry<TrackingKeyTag>::RecordConstruction(this);
        }

        TrackingKey(TrackingKey&& other) noexcept : value(other.value)
        {
            TrackingRegistry<TrackingKeyTag>::RecordConstruction(this);
        }

        TrackingKey& operator=(const TrackingKey&) noexcept = default;
        TrackingKey& operator=(TrackingKey&&) noexcept      = default;

        ~TrackingKey() noexcept
        {
            TrackingRegistry<TrackingKeyTag>::RecordDestruction(this);
        }

        friend bool operator==(const TrackingKey& lhs, const TrackingKey& rhs) noexcept
        {
            return lhs.value == rhs.value;
        }
    };

    struct TrackingValue
    {
        int value {0};

        TrackingValue() noexcept
        {
            TrackingRegistry<TrackingValueTag>::RecordConstruction(this);
            BusyWork();
        }

        explicit TrackingValue(int v) noexcept : value(v)
        {
            TrackingRegistry<TrackingValueTag>::RecordConstruction(this);
            BusyWork();
        }

        TrackingValue(const TrackingValue& other) noexcept : value(other.value)
        {
            TrackingRegistry<TrackingValueTag>::RecordConstruction(this);
            BusyWork();
        }

        TrackingValue(TrackingValue&& other) noexcept : value(other.value)
        {
            TrackingRegistry<TrackingValueTag>::RecordConstruction(this);
            BusyWork();
        }

        TrackingValue& operator=(const TrackingValue&) noexcept = default;
        TrackingValue& operator=(TrackingValue&&) noexcept      = default;

        ~TrackingValue() noexcept
        {
            BusyWork();
            TrackingRegistry<TrackingValueTag>::RecordDestruction(this);
        }

    private:
        static void BusyWork() noexcept
        {
            for (int i = 0; i < 6; ++i)
            {
                std::this_thread::yield();
            }
        }
    };

    struct TransitionMatrix
    {
        std::array<std::array<std::uint64_t, 4>, 4> counts {};

        void Record(std::size_t from, std::size_t to) noexcept
        {
            if (from < counts.size() && to < counts[from].size())
            {
                ++counts[from][to];
            }
        }

        [[nodiscard]] std::uint64_t Count(std::size_t from, std::size_t to) const noexcept
        {
            return counts[from][to];
        }
    };
}// namespace

namespace std
{
    template<>
    struct hash<::TrackingKey>
    {
        std::size_t operator()(const ::TrackingKey& key) const noexcept
        {
            return std::hash<int> {}(key.value);
        }
    };
}// namespace std

TEST_CASE("ConcurrentHashMap builds collision chains", "[Containers][ConcurrentHashMap][Stress]")
{
    ConcurrentHashMap<int, int> map(2);
    for (int i = 0; i < 10; ++i)
    {
        map.Insert(i * 2, i);
    }

    for (int i = 0; i < 10; ++i)
    {
        CHECK(map.Contains(i * 2));
    }
    CHECK(map.Size() == 10U);
}

TEST_CASE("ConcurrentHashMap resizes under concurrent inserts", "[Containers][ConcurrentHashMap][Stress]")
{
    ConcurrentHashMap<int, int> map(8);
    constexpr int               threadCount      = 8;
    constexpr int               insertsPerThread = 4000;
    std::vector<std::thread>    workers;

    for (int t = 0; t < threadCount; ++t)
    {
        workers.emplace_back([t, &map] {
            const int base = t * insertsPerThread;
            for (int i = 0; i < insertsPerThread; ++i)
            {
                map.Insert(base + i, base + i);
            }
        });
    }

    for (auto& worker: workers)
    {
        worker.join();
    }

    CHECK(map.Size() == static_cast<std::size_t>(threadCount * insertsPerThread));
    CHECK(map.Contains(0));
    CHECK(map.Contains((threadCount - 1) * insertsPerThread));
}

TEST_CASE("ConcurrentHashMap mixed read/write stress", "[Containers][ConcurrentHashMap][Stress]")
{
    ConcurrentHashMap<int, int> map(32);
    constexpr int               writerThreads = 4;
    constexpr int               readerThreads = 8;
    constexpr int               opsPerWriter  = 5000;
    constexpr int               opsPerReader  = 10000;

    std::atomic<bool>        start {false};
    std::vector<std::thread> writers;
    std::vector<std::thread> readers;

    for (int w = 0; w < writerThreads; ++w)
    {
        writers.emplace_back([w, &map, &start] {
            std::mt19937                       rng(1234 + w);
            std::uniform_int_distribution<int> keys(0, 20000);
            while (!start.load())
            {
                std::this_thread::yield();
            }
            for (int i = 0; i < opsPerWriter; ++i)
            {
                int key = keys(rng);
                if ((i & 7) == 0)
                {
                    map.Remove(key);
                }
                map.Insert(key, key);
            }
        });
    }

    for (int r = 0; r < readerThreads; ++r)
    {
        readers.emplace_back([r, &map, &start] {
            std::mt19937                       rng(5678 + r);
            std::uniform_int_distribution<int> keys(0, 20000);
            while (!start.load())
            {
                std::this_thread::yield();
            }
            int dummy = 0;
            for (int i = 0; i < opsPerReader; ++i)
            {
                map.TryGet(keys(rng), dummy);
            }
        });
    }

    start.store(true);

    for (auto& writer: writers)
    {
        writer.join();
    }
    for (auto& reader: readers)
    {
        reader.join();
    }

    CHECK(map.Size() <= 20001U);
}

TEST_CASE("ConcurrentHashMap coordinates reserve during contention", "[Containers][ConcurrentHashMap][Stress]")
{
    ConcurrentHashMap<int, int> map(4);
    constexpr int               inserterThreads  = 6;
    constexpr int               insertsPerThread = 2000;
    std::atomic<bool>           start {false};
    std::vector<std::thread>    workers;

    for (int t = 0; t < inserterThreads; ++t)
    {
        workers.emplace_back([t, &map, &start] {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            const int base = t * insertsPerThread;
            for (int i = 0; i < insertsPerThread; ++i)
            {
                map.Insert(base + i, base + i);
            }
        });
    }

    std::thread resizer([&map, &start] {
        while (!start.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        for (int step = 1; step <= 6; ++step)
        {
            map.Reserve(static_cast<std::size_t>(step) * insertsPerThread * 2);
        }
    });

    start.store(true, std::memory_order_release);

    for (auto& worker: workers)
    {
        worker.join();
    }
    resizer.join();

    const auto expected = static_cast<std::size_t>(inserterThreads * insertsPerThread);
    // Ensure all migrations are finalized before validating edge keys.
    map.Quiesce();
    CHECK(map.Size() == expected);
    CHECK(map.Contains(0));
    CHECK(map.Contains(expected - 1));
}

TEST_CASE("ConcurrentHashMap handles insert/remove churn", "[Containers][ConcurrentHashMap][Stress]")
{
    ConcurrentHashMap<int, int> map(8);
    constexpr int               producerThreads = 4;
    constexpr int               consumerThreads = 4;
    constexpr int               opsPerProducer  = 4000;
    constexpr int               keySpace        = producerThreads * opsPerProducer;

    std::atomic<bool> start {false};
    std::atomic<int>  removedCount {0};

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    for (int p = 0; p < producerThreads; ++p)
    {
        producers.emplace_back([p, &map, &start] {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            const int base = p * opsPerProducer;
            for (int i = 0; i < opsPerProducer; ++i)
            {
                map.Insert(base + i, base + i);
            }
        });
    }

    for (int c = 0; c < consumerThreads; ++c)
    {
        consumers.emplace_back([c, &map, &start, &removedCount] {
            std::mt19937                       rng(9000 + c);
            std::uniform_int_distribution<int> keys(0, keySpace - 1);
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            for (int i = 0; i < opsPerProducer; ++i)
            {
                if (map.Remove(keys(rng)))
                {
                    removedCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& producer: producers)
    {
        producer.join();
    }
    for (auto& consumer: consumers)
    {
        consumer.join();
    }

    const auto totalInserted = static_cast<std::size_t>(producerThreads * opsPerProducer);
    const auto totalRemoved  = static_cast<std::size_t>(removedCount.load());
    CHECK(map.Size() + totalRemoved == totalInserted);
    CHECK(map.Size() <= totalInserted);
}

TEST_CASE("ConcurrentHashMap preserves slot transitions under remove/migrate stress",
          "[Containers][ConcurrentHashMap][Stress]")
{
    using StressMap = ConcurrentHashMap<TrackingKey, TrackingValue>;

    TrackingRegistry<TrackingKeyTag>::Reset();
    TrackingRegistry<TrackingValueTag>::Reset();

    TransitionMatrix transitions;

    constexpr std::uint8_t  unknownState     = 0xFFU;
    constexpr std::size_t   writerThreads    = 6;
    constexpr std::size_t   opsPerWriter     = 20000;
    constexpr int           keySpace         = 4096;
    constexpr std::uint32_t pendingSpinLimit = 2000;

    std::atomic<bool> start {false};
    std::atomic<bool> stopMonitor {false};
    std::atomic<bool> pendingStall {false};

    {
        StressMap map(64);

        std::thread monitor([&]() {
            std::vector<std::uint8_t>  primaryStates;
            std::vector<std::uint32_t> primaryPending;
            std::vector<std::uint8_t>  oldStates;
            std::vector<std::uint32_t> oldPending;

            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            auto sampleTable = [&](const StressMap::TableView& view,
                                   std::vector<std::uint8_t>&  states,
                                   std::vector<std::uint32_t>& streaks) {
                const std::size_t capacity = view.groups ? (view.mask + 1) : 0;
                if (states.size() != capacity)
                {
                    states.assign(capacity, unknownState);
                    streaks.assign(capacity, 0);
                }

                for (std::size_t index = 0; index < capacity; ++index)
                {
                    auto&      slot = StressMap::SlotAt(view, index);
                    const auto currentState =
                            static_cast<std::uint8_t>(slot.State(std::memory_order_acquire));
                    if (states[index] == unknownState)
                    {
                        states[index] = currentState;
                        continue;
                    }
                    if (states[index] != currentState)
                    {
                        transitions.Record(states[index], currentState);
                        states[index]  = currentState;
                        streaks[index] = 0;
                        continue;
                    }

                    if (currentState ==
                        static_cast<std::uint8_t>(NGIN::Containers::detail::SlotState::PendingInsert))
                    {
                        ++streaks[index];
                        if (streaks[index] > pendingSpinLimit)
                        {
                            pendingStall.store(true, std::memory_order_relaxed);
                        }
                    }
                    else
                    {
                        streaks[index] = 0;
                    }
                }
            };

            while (!stopMonitor.load(std::memory_order_acquire))
            {
                auto token = map.AcquireGuardedView();
                sampleTable(token.view, primaryStates, primaryPending);
                if (token.mig)
                {
                    StressMap::TableView oldView {token.mig->oldGroups, token.mig->oldMask};
                    sampleTable(oldView, oldStates, oldPending);
                }
                else
                {
                    oldStates.clear();
                    oldPending.clear();
                }
                map.ReleaseGuard(token, false);
                std::this_thread::yield();
            }
        });

        std::vector<std::thread> workers;
        workers.reserve(writerThreads);

        for (std::size_t idx = 0; idx < writerThreads; ++idx)
        {
            workers.emplace_back([idx, &map, &start]() {
                std::mt19937 rng(static_cast<std::mt19937::result_type>(idx * 7919 + 17));
                while (!start.load(std::memory_order_acquire))
                {
                    std::this_thread::yield();
                }
                for (std::size_t op = 0; op < opsPerWriter; ++op)
                {
                    const int keyValue = static_cast<int>(rng() % keySpace);
                    const int selector = static_cast<int>(rng() & 3);
                    if (selector == 0)
                    {
                        map.Insert(TrackingKey(keyValue), TrackingValue(keyValue));
                    }
                    else if (selector == 1)
                    {
                        map.Remove(TrackingKey(keyValue));
                    }
                    else if (selector == 2)
                    {
                        map.Upsert(
                                TrackingKey(keyValue),
                                TrackingValue(keyValue),
                                [](TrackingValue& existing, TrackingValue&& incoming) {
                                    existing = std::move(incoming);
                                });
                    }
                    else
                    {
                        TrackingValue sink;
                        map.TryGet(TrackingKey(keyValue), sink);
                    }
                }
            });
        }

        std::thread resizer([&map, &start]() {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            for (int pass = 0; pass < 32; ++pass)
            {
                map.Reserve(256 + static_cast<std::size_t>(pass % 8) * 128);
                std::this_thread::yield();
            }
        });

        start.store(true, std::memory_order_release);

        for (auto& worker: workers)
        {
            worker.join();
        }
        resizer.join();

        map.Quiesce();
        stopMonitor.store(true, std::memory_order_release);
        monitor.join();

        CHECK_FALSE(pendingStall.load(std::memory_order_relaxed));

        const std::size_t emptyIndex =
                static_cast<std::size_t>(NGIN::Containers::detail::SlotState::Empty);
        const std::size_t pendingIndex =
                static_cast<std::size_t>(NGIN::Containers::detail::SlotState::PendingInsert);
        const std::size_t occupiedIndex =
                static_cast<std::size_t>(NGIN::Containers::detail::SlotState::Occupied);
        const std::size_t tombstoneIndex =
                static_cast<std::size_t>(NGIN::Containers::detail::SlotState::Tombstone);

        CHECK(transitions.Count(occupiedIndex, pendingIndex) > 0);
        CHECK(transitions.Count(pendingIndex, tombstoneIndex) > 0);
        CHECK(transitions.Count(pendingIndex, occupiedIndex) > 0);
        CHECK(transitions.Count(tombstoneIndex, pendingIndex) + transitions.Count(tombstoneIndex, emptyIndex) > 0);

        std::size_t liveEntries = 0;
        map.ForEach([&](const TrackingKey&, const TrackingValue&) { ++liveEntries; });

        CHECK(static_cast<std::size_t>(TrackingRegistry<TrackingKeyTag>::Alive()) == liveEntries);
        CHECK(static_cast<std::size_t>(TrackingRegistry<TrackingValueTag>::Alive()) == liveEntries);
    }

    CHECK(TrackingRegistry<TrackingKeyTag>::DoubleDestroyed() == 0);
    CHECK(TrackingRegistry<TrackingValueTag>::DoubleDestroyed() == 0);
    CHECK(TrackingRegistry<TrackingKeyTag>::Constructed() == TrackingRegistry<TrackingKeyTag>::Destroyed());
    CHECK(TrackingRegistry<TrackingValueTag>::Constructed() == TrackingRegistry<TrackingValueTag>::Destroyed());

    TrackingRegistry<TrackingKeyTag>::Reset();
    TrackingRegistry<TrackingValueTag>::Reset();
}
