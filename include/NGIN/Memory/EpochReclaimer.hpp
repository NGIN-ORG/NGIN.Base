/// @file EpochReclaimer.hpp
/// @brief Lightweight epoch-based reclamation (experimental) for deferred deletes.
///
/// Strategy:
///  - Global epoch increments when attempting reclamation.
///  - Threads announce active epoch via Guard RAII; 0 means quiescent.
///  - Retired nodes reclaimed only when all active epochs advanced beyond retireEpoch.
///
/// NOTE: Experimental – API and internals may change.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>
#include <array>
#include <thread>
#include <cassert>

namespace NGIN::Memory
{

    class EpochReclaimer
    {
    public:
        struct RetiredNode
        {
            void*                      ptr {nullptr};
            std::function<void(void*)> deleter {};
            std::uint64_t              retireEpoch {0};
        };

        struct ThreadRecord
        {
            std::atomic<std::uint64_t> activeEpoch {0};// 0 == inactive
            std::vector<RetiredNode>   pending;
            bool                       reclaiming = false;// local-thread only
        };

        static EpochReclaimer& Instance()
        {
            static EpochReclaimer inst;
            return inst;
        }

        class Guard
        {
        public:
            Guard() noexcept { EpochReclaimer::Instance().Enter(); }
            ~Guard() { EpochReclaimer::Instance().Leave(); }
            Guard(const Guard&)            = delete;
            Guard& operator=(const Guard&) = delete;
        };

        void Retire(void* p, std::function<void(void*)> d)
        {
            if (!p)
                return;
            auto& rec = threadRecord();
            rec.pending.push_back({p, std::move(d), globalEpoch.load(std::memory_order_relaxed)});
            if (rec.pending.size() >= retireBatchThreshold)
            {
                TryAdvanceAndReclaim(rec);
            }
        }

        void ForceDrain() { TryAdvanceAndReclaim(threadRecord(), true); }

    private:
        EpochReclaimer()
        {
            for (auto& slot: records)
            {
                slot.store(nullptr, std::memory_order_relaxed);
            }
        }

        // Increased to tolerate high thread churn scenarios (e.g. benchmarks repeatedly
        // spawning fresh thread pools) without tripping an assert. We also implement
        // slot reuse of quiescent (inactive + no pending retire list) records below.
        static constexpr std::size_t kMaxThreads          = 4096;
        static constexpr std::size_t retireBatchThreshold = 32;

        std::atomic<std::uint64_t> globalEpoch {1};
        // Use atomic pointers for safe multi-thread publication (distinct indices only).
        std::array<std::atomic<ThreadRecord*>, kMaxThreads> records {};
        std::atomic<std::size_t>                            recordCount {0};

        ThreadRecord& threadRecord()
        {
            thread_local ThreadRecord* tr = nullptr;
            if (tr)
                return *tr;

            // Allocate a fresh slot each time a brand new OS thread first touches the reclaimer.
            // (Previous reuse scheme had a benign data race allowing two threads to pick the same inactive record.)
            auto slot = recordCount.fetch_add(1, std::memory_order_acq_rel);
            assert(slot < kMaxThreads && "Exceeded EpochReclaimer thread slot capacity (increase kMaxThreads)");
            tr = new ThreadRecord();

            // reserve some space to avoid realloc churn
            if (tr->pending.capacity() == 0)
                tr->pending.reserve(256);

            records[slot].store(tr, std::memory_order_release);
            return *tr;
        }

        void Enter()
        {
            auto& rec = threadRecord();
            auto  ge  = globalEpoch.load(std::memory_order_acquire);
            rec.activeEpoch.store(ge, std::memory_order_release);
        }
        void Leave()
        {
            auto& rec = threadRecord();
            rec.activeEpoch.store(0, std::memory_order_release);
        }

        std::uint64_t MinActiveEpoch() const
        {
            auto          count    = recordCount.load(std::memory_order_acquire);
            std::uint64_t minEpoch = globalEpoch.load(std::memory_order_relaxed);
            for (std::size_t i = 0; i < count; ++i)
            {
                if (auto* r = records[i].load(std::memory_order_acquire))
                {
                    auto e = r->activeEpoch.load(std::memory_order_acquire);
                    if (e != 0 && e < minEpoch)
                        minEpoch = e;
                }
            }
            return minEpoch;
        }

        void TryAdvanceAndReclaim(ThreadRecord& rec, bool force = false)
        {
            if (!force && rec.pending.size() < retireBatchThreshold)
                return;

            // Prevent nested TryAdvance on the same thread record
            if (rec.reclaiming)
                return;
            rec.reclaiming = true;

            (void) globalEpoch.fetch_add(1, std::memory_order_acq_rel);
            const auto safeEpoch = MinActiveEpoch();

            // Partition to local vectors so deleters can't mutate the container we're iterating.
            std::vector<RetiredNode> toReclaim;
            std::vector<RetiredNode> survivors;
            toReclaim.reserve(rec.pending.size());
            survivors.reserve(rec.pending.size());

            for (auto& n: rec.pending)
            {
                if (n.retireEpoch < safeEpoch)
                {
                    toReclaim.push_back(std::move(n));
                }
                else
                {
                    survivors.push_back(std::move(n));
                }
            }
            rec.pending.swap(survivors);// make pending stable before running deleters
                                        // rec.reclaiming = false;     // allow Retire() to trigger another pass if needed


            // Run deleters AFTER making pending stable.
            for (auto& n: toReclaim)
            {
                // Move out the deleter before invoking to avoid any surprise aliasing
                auto  d = std::move(n.deleter);
                void* p = n.ptr;
                if (d)
                {
                    d(p);
                }
            }

            rec.reclaiming = false;
        }
    };

}// namespace NGIN::Memory
