/// @file ConcurrentHashMap.hpp
/// @brief Lock-free style concurrent hash map (experimental).
///
/// Invariants / Design Notes:
/// 1. Fast path: Each slot stores (hash, BucketData*) in an atomic BucketPtr (two‑word CAS)
///    when the platform provides a lock‑free atomic for the pair. Fallback path: each slot
///    stores only BucketData* (single‑word CAS) and the hash lives inside BucketData.
///    Once a non‑null slot is published with a release CAS, the pointed BucketData and its
///    associated hash are immutable for the lifetime of the map (no relocation, no reuse).
/// 2. Logical deletion is performed by calling BucketData::v.drop() which converts the
///    guarded value into a tombstone (guard no longer valid). We never reclaim / free a
///    BucketData during map lifetime; full reclamation only occurs at map destruction.
/// 3. Readers perform an acquire load of the slot before dereferencing BucketData ensuring
///    visibility of the fully constructed object published under release semantics.
/// 4. Chaining: Additional VirtualBucket nodes are appended with a release CAS on the
///    'next' pointer. Once linked they remain reachable (no removal) until destruction.
/// 5. Size accounting increments only on first successful publication of a non-null slot
///    and decrements only on a transition from live -> tombstone (Phase 1 keeps existing
///    semantics; further tightening may occur in later phases).
/// 6. Destruction requires external quiescence: user must ensure no concurrent operations
///    are in-flight when destroying the map (epoch based reclamation may be added later).
/// 7. Fast path uses lock-free atomic<BucketPtr>; if the platform does not provide a
///    lock-free two-word CAS the implementation transparently falls back to a single-word
///    pointer CAS storing the hash inside BucketData.

#pragma once

#include <atomic>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include <optional>

namespace NGIN::Containers
{

    // ============================================================
    // SharedDoubleReferenceGuard
    // ============================================================

    template<typename DataType>
    class SharedDoubleReferenceGuard
    {
    private:
        struct DataStruct
        {
            std::atomic<int> internal_counter;
            DataType         object;

            template<typename... Ts>
            explicit DataStruct(Ts&&... args)
                : internal_counter(0), object(std::forward<Ts>(args)...)
            {}

            void release_ref() noexcept
            {
                if (internal_counter.fetch_add(1, std::memory_order_acq_rel) == -1)
                    delete this;
            }
        };

        struct DataPtrStruct
        {
            int         external_counter;
            DataStruct* ptr;
        };

        static_assert(std::is_trivially_copyable_v<DataPtrStruct>,
                      "DataPtrStruct must be trivially copyable for atomic CAS");

        std::atomic<DataPtrStruct> data_ptr;

        void release(DataPtrStruct& old_data_ptr) noexcept
        {
            if (!old_data_ptr.ptr)
                return;
            int external = old_data_ptr.external_counter;
            if (old_data_ptr.ptr->internal_counter.fetch_sub(external, std::memory_order_acq_rel) == external - 1)
            {
                delete old_data_ptr.ptr;
            }
            else
            {
                old_data_ptr.ptr->release_ref();
            }
        }

    public:
        class DataGuard
        {
            friend class SharedDoubleReferenceGuard<DataType>;
            DataStruct* ptr;
            explicit DataGuard(DataStruct* p) : ptr(p) {}

        public:
            DataGuard(const DataGuard&)            = delete;
            DataGuard& operator=(const DataGuard&) = delete;
            DataGuard(DataGuard&& other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }
            DataGuard& operator=(DataGuard&& other) noexcept
            {
                if (this != &other)
                {
                    if (ptr)
                        ptr->release_ref();
                    ptr       = other.ptr;
                    other.ptr = nullptr;
                }
                return *this;
            }
            ~DataGuard()
            {
                if (ptr)
                    ptr->release_ref();
            }

            bool            is_valid() const noexcept { return ptr != nullptr; }
            DataType*       operator->() noexcept { return &ptr->object; }
            DataType&       operator*() noexcept { return ptr->object; }
            const DataType* operator->() const noexcept { return &ptr->object; }
            const DataType& operator*() const noexcept { return ptr->object; }
        };

        SharedDoubleReferenceGuard() noexcept
        {
            DataPtrStruct init {0, nullptr};
            data_ptr.store(init, std::memory_order_relaxed);
        }

        ~SharedDoubleReferenceGuard()
        {
            auto old = data_ptr.load();
            release(old);
        }

        DataGuard acquire() noexcept
        {
            DataPtrStruct new_data_ptr {};
            DataPtrStruct old_data_ptr = data_ptr.load(std::memory_order_relaxed);
            do
            {
                new_data_ptr = old_data_ptr;
                ++new_data_ptr.external_counter;
            } while (!data_ptr.compare_exchange_weak(
                    old_data_ptr, new_data_ptr,
                    std::memory_order_acq_rel, std::memory_order_relaxed));
            return DataGuard(new_data_ptr.ptr);
        }

        template<typename... Ts>
        void emplace(Ts&&... args)
        {
            auto*         new_data = new DataStruct(std::forward<Ts>(args)...);
            DataPtrStruct new_dp {1, new_data};
            DataPtrStruct old_dp = data_ptr.load(std::memory_order_relaxed);
            while (!data_ptr.compare_exchange_weak(
                    old_dp, new_dp,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {}
            release(old_dp);
        }

        void drop()
        {
            DataPtrStruct new_dp {0, nullptr};
            DataPtrStruct old_dp = data_ptr.load(std::memory_order_relaxed);
            while (!data_ptr.compare_exchange_weak(
                    old_dp, new_dp,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {}
            release(old_dp);
        }
    };

    // ============================================================
    // ConcurrentHashMap
    // ============================================================

    template<typename Key, typename Value, std::size_t BucketsPerVirtual = 8>
    class ConcurrentHashMap
    {
        static_assert(std::is_copy_constructible_v<Value>, "Value must be copy constructible (Phase 2 requirement)");
        static inline std::size_t next_pow2(std::size_t n)
        {
            if (n < 2)
                return 2;
            --n;
            n |= n >> 1;
            n |= n >> 2;
            n |= n >> 4;
            n |= n >> 8;
            n |= n >> 16;
            if constexpr (sizeof(std::size_t) >= 8)
                n |= n >> 32;
            return n + 1;
        }

        struct BucketData
        {
            std::size_t                       hash;// always stored (even in pair-CAS path) for portability & debug
            Key                               key;
            SharedDoubleReferenceGuard<Value> v;
            template<typename K, typename... Ts>
            BucketData(std::size_t h, K&& k, Ts&&... args) : hash(h), key(std::forward<K>(k)), v()
            {
                v.emplace(std::forward<Ts>(args)...);
            }
        };

        struct BucketPtr
        {
            std::size_t hash;
            BucketData* data;
            friend bool operator==(const BucketPtr& a, const BucketPtr& b) noexcept
            {
                return a.hash == b.hash && a.data == b.data;
            }
        };
        static_assert(std::is_trivially_copyable_v<BucketPtr>, "BucketPtr must be trivially copyable");

#ifndef NGIN_CONCURRENT_HASHMAP_FORCE_FALLBACK
        static constexpr bool kUsePairCAS = std::atomic<BucketPtr>::is_always_lock_free;
#else
        static constexpr bool kUsePairCAS = false;
#endif

        struct VirtualBucket
        {
            using PairSlotArray     = std::array<std::atomic<BucketPtr>, BucketsPerVirtual>;
            using FallbackSlotArray = std::array<std::atomic<BucketData*>, BucketsPerVirtual>;
            std::conditional_t<kUsePairCAS, PairSlotArray, FallbackSlotArray> slots;
            std::atomic<VirtualBucket*>                                       next {nullptr};
            VirtualBucket()
            {
                if constexpr (kUsePairCAS)
                {
                    for (auto& s: slots)
                        s.store(BucketPtr {0, nullptr}, std::memory_order_relaxed);
                }
                else
                {
                    for (auto& s: slots)
                        s.store(nullptr, std::memory_order_relaxed);
                }
            }
            ~VirtualBucket()
            {
                if (auto* p = next.load())
                    delete p;
                if constexpr (kUsePairCAS)
                {
                    for (auto& s: slots)
                    {
                        auto bp = s.load();
                        if (bp.data)
                            delete bp.data;
                    }
                }
                else
                {
                    for (auto& s: slots)
                    {
                        auto* ptr = s.load();
                        if (ptr)
                            delete ptr;
                    }
                }
            }
        };

        std::atomic<std::atomic<VirtualBucket*>*> tableBuckets {nullptr};// atomic pointer to current bucket array
        std::atomic<std::size_t>                  tableLen {0};          // published with release, read with acquire for mask
        std::atomic<std::size_t>                  size_ {0};
        // Track allocated tables via a lock-free stack (avoid unsynchronized std::vector access during concurrent resizes)
        struct TableAllocationNode
        {
            std::atomic<VirtualBucket*>* buckets;
            TableAllocationNode*         next;
        };
        std::atomic<TableAllocationNode*> allocationsHead {nullptr};

        void TrackAllocation(std::atomic<VirtualBucket*>* buckets) noexcept
        {
            auto* node = new TableAllocationNode {buckets, allocationsHead.load(std::memory_order_relaxed)};
            while (!allocationsHead.compare_exchange_weak(node->next, node,
                                                          std::memory_order_release, std::memory_order_relaxed)) {}
        }
        std::hash<Key> hasher;

        // ------------------------------------------------------------
        // Phase 4: Cooperative (incremental) resize state
        // ------------------------------------------------------------
        struct ResizeState
        {
            // Old (source) table
            std::atomic<VirtualBucket*>* old_table;
            std::size_t                  old_len;
            // New (destination) table
            std::atomic<VirtualBucket*>* new_table;
            std::size_t                  new_len;
            // Per-bucket migration markers: 0 = not started, 1 = in progress, 2 = done
            std::unique_ptr<std::atomic<int>[]> markers;
            std::atomic<std::size_t>            nextIndex {0};
            std::atomic<std::size_t>            migratedCount {0};

            ResizeState(std::atomic<VirtualBucket*>* o, std::size_t ol,
                        std::atomic<VirtualBucket*>* nt, std::size_t nl)
                : old_table(o), old_len(ol), new_table(nt), new_len(nl)
            {
                markers = std::unique_ptr<std::atomic<int>[]>(new std::atomic<int>[old_len]);
                for (std::size_t i = 0; i < old_len; ++i)
                    markers[i].store(0, std::memory_order_relaxed);
            }
        };
        std::atomic<ResizeState*> resizeState {nullptr};

        std::size_t mask() const noexcept { return tableLen.load(std::memory_order_acquire) - 1; }

        struct TableView
        {
            std::atomic<VirtualBucket*>* buckets;
            std::size_t                  len;
        };

        // Acquire a self-consistent snapshot of (buckets pointer, length).
        // We double-load the buckets pointer and only accept if unchanged, guaranteeing
        // that len corresponds to the same published table (since publish installs
        // buckets then len sequentially and readers may otherwise see a mixed pair).
        TableView LoadTable() const noexcept
        {
            while (true)
            {
                auto* b1 = tableBuckets.load(std::memory_order_acquire);
                auto  l  = tableLen.load(std::memory_order_acquire);
                auto* b2 = tableBuckets.load(std::memory_order_acquire);
                if (b1 == b2) [[likely]]
                    return {b1, l};
                // else race with publish; retry
            }
        }

        // Attempt to start a cooperative resize (non-blocking).
        void MaybeStartResize()
        {
            // Heuristic: load factor > 0.75 triggers resize attempt.
            auto curLen = tableLen.load(std::memory_order_acquire);
            if (Size() * 4 < curLen * BucketsPerVirtual * 3)
                return;

            if (resizeState.load(std::memory_order_acquire) != nullptr)
                return;// already resizing

            std::size_t new_len = curLen * 2;
            auto*       new_tab = new std::atomic<VirtualBucket*>[new_len];
            for (std::size_t i = 0; i < new_len; ++i)
            {
                new_tab[i].store(new VirtualBucket(), std::memory_order_release);
            }
            TrackAllocation(new_tab);// track ownership (lock-free)

            // Create state object
            auto*        state    = new ResizeState(tableBuckets.load(std::memory_order_acquire), curLen, new_tab, new_len);
            ResizeState* expected = nullptr;
            if (!resizeState.compare_exchange_strong(expected, state, std::memory_order_release, std::memory_order_relaxed))
            {
                // Lost race; discard state (leak avoidance)
                delete state;// new_tab retained (intentional leak until destruction)
            }
        }

        // Place (hash,key,value) into destination table (used during migration). Value passed by const ref.
        void PlaceInto(VirtualBucket* root, std::size_t h, const Key& key, const Value& value)
        {
            VirtualBucket* vb = root;
            while (vb)
            {
                if constexpr (kUsePairCAS)
                {
                    for (auto& slot: vb->slots)
                    {
                        BucketPtr expected {0, nullptr};
                        BucketPtr desired {h, new BucketData(h, key, value)};
                        if (slot.compare_exchange_strong(expected, desired))
                            return;
                        delete desired.data;// someone beat us
                    }
                }
                else
                {
                    for (auto& slot: vb->slots)
                    {
                        BucketData* expected = nullptr;
                        auto*       desired  = new BucketData(h, key, value);
                        if (slot.compare_exchange_strong(expected, desired))
                            return;
                        delete desired;// lost race
                    }
                }
                VirtualBucket* next = vb->next.load(std::memory_order_acquire);
                if (!next)
                {
                    auto* new_vb = new VirtualBucket();
                    if (vb->next.compare_exchange_strong(next, new_vb, std::memory_order_release, std::memory_order_relaxed))
                        vb = new_vb;
                    else
                    {
                        delete new_vb;
                        vb = next;
                    }
                }
                else
                    vb = next;
            }
        }

        // Migrate a single bucket index; returns true if any work was performed.
        bool MigrateOne(ResizeState* state)
        {
            std::size_t i = state->nextIndex.fetch_add(1, std::memory_order_acq_rel);
            if (i >= state->old_len)
                return false;
            // Claim marker
            int expected = 0;
            if (!state->markers[i].compare_exchange_strong(expected, 1, std::memory_order_acq_rel, std::memory_order_relaxed))
                return true;// another thread is/was migrating; count as helping

            VirtualBucket* vb = state->old_table[i].load(std::memory_order_acquire);
            while (vb)
            {
                if constexpr (kUsePairCAS)
                {
                    for (auto& s: vb->slots)
                    {
                        BucketPtr bp = s.load(std::memory_order_acquire);
                        if (bp.data)
                        {
                            auto g = bp.data->v.acquire();
                            if (g.is_valid())
                                PlaceInto(state->new_table[bp.hash & (state->new_len - 1)].load(std::memory_order_relaxed), bp.hash, bp.data->key, *g);
                        }
                    }
                }
                else
                {
                    for (auto& s: vb->slots)
                    {
                        BucketData* ptr = s.load(std::memory_order_acquire);
                        if (ptr)
                        {
                            auto g = ptr->v.acquire();
                            if (g.is_valid())
                                PlaceInto(state->new_table[ptr->hash & (state->new_len - 1)].load(std::memory_order_relaxed), ptr->hash, ptr->key, *g);
                        }
                    }
                }
                vb = vb->next.load(std::memory_order_acquire);
            }

            state->markers[i].store(2, std::memory_order_release);
            state->migratedCount.fetch_add(1, std::memory_order_acq_rel);
            return true;
        }

        void HelpResize()
        {
            ResizeState* state = resizeState.load(std::memory_order_acquire);
            if (!state)
                return;
            // Perform a bounded amount of work (1 bucket) to keep latency predictable.
            MigrateOne(state);
            // Check for completion
            if (state->migratedCount.load(std::memory_order_acquire) == state->old_len)
            {
                // Install new table (publish)
                tableBuckets.store(state->new_table, std::memory_order_release);
                tableLen.store(state->new_len, std::memory_order_release);// publish len
                // Clear resize state (single thread wins)
                ResizeState* expected = state;
                if (resizeState.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed))
                {
                    // IMPORTANT: After swap, state->new_table now OWNS the *old* table (pre-swap). We release ownership
                    // so old buckets persist. We also intentionally DO NOT delete the ResizeState object here because
                    // other threads may still have a raw pointer loaded before we cleared resizeState. Full safe
                    // reclamation will be added with epoch/hazard management (Phase 6). This leaks one small object per
                    // resize which is acceptable short-term.
                    // leak resize state (will reclaim later when epochs added)
                }
            }
        }

    public:
        explicit ConcurrentHashMap(std::size_t initial_capacity = 16)
        {
            const std::size_t cap = next_pow2(initial_capacity);
            tableLen.store(cap, std::memory_order_relaxed);
            auto* buckets = new std::atomic<VirtualBucket*>[cap];
            for (std::size_t i = 0; i < cap; ++i)
            {
                buckets[i].store(new VirtualBucket(), std::memory_order_release);
            }
            tableBuckets.store(buckets, std::memory_order_release);
            TrackAllocation(buckets);
            auto tl = tableLen.load(std::memory_order_relaxed);
            assert(tl && (tl & (tl - 1)) == 0 && "table_len must be power of two");
#ifndef NDEBUG
            {
                std::atomic<BucketPtr> test;
                (void) test;// silence unused warning
                if constexpr (kUsePairCAS)
                {
                    // Pair-CAS path selected; sanity check it is indeed lock-free.
                    assert(test.is_lock_free() && "Expected lock-free atomic<BucketPtr> for pair-CAS fast path");
                }
                else
                {
                    // Fallback path active (non lock-free pair CAS). No assertion; documented behavior.
                }
            }
#endif
        }

        ~ConcurrentHashMap()
        {
            auto curBuckets = tableBuckets.load(std::memory_order_acquire);
            auto len        = tableLen.load(std::memory_order_acquire);
            for (std::size_t i = 0; i < len; ++i)
            {
                if (auto* vb = curBuckets[i].load(std::memory_order_relaxed))
                    delete vb;
            }
            // Delete all arrays (walk lock-free stack)
            TableAllocationNode* node = allocationsHead.load(std::memory_order_acquire);
            while (node)
            {
                auto* next = node->next;
                delete[] node->buckets;
                delete node;
                node = next;
            }
        }

        void Insert(const Key& key, const Value& value)
        {
            const size_t   h       = hasher(key);// hash computed once
            auto           tv      = LoadTable();
            const size_t   idx     = h & (tv.len - 1);
            auto*          buckets = tv.buckets;
            VirtualBucket* vb      = buckets[idx].load(std::memory_order_acquire);

            while (vb)
            {
                if constexpr (kUsePairCAS)
                {
                    for (auto& slot: vb->slots)
                    {
                        BucketPtr bp = slot.load(std::memory_order_acquire);
                        if (bp.data == nullptr)
                        {
                            auto*     bd = new BucketData(h, key, value);
                            BucketPtr expected {0, nullptr};
                            BucketPtr desired {h, bd};
                            if (slot.compare_exchange_strong(expected, desired,
                                                             std::memory_order_release,
                                                             std::memory_order_relaxed))
                            {
                                size_.fetch_add(1);
                                MaybeStartResize();
                                HelpResize();
                                return;
                            }
                            delete bd;
                        }
                        else if (bp.hash == h && bp.data->key == key)
                        {
                            // Distinguish update vs tombstone reinsert
                            auto g       = bp.data->v.acquire();
                            bool wasDead = !g.is_valid();
                            bp.data->v.emplace(value);
                            if (wasDead)
                                size_.fetch_add(1);
                            return;
                        }
                    }
                }
                else
                {
                    for (auto& slot: vb->slots)
                    {
                        BucketData* ptr = slot.load(std::memory_order_acquire);
                        if (ptr == nullptr)
                        {
                            auto* expected = static_cast<BucketData*>(nullptr);
                            auto* desired  = new BucketData(h, key, value);
                            if (slot.compare_exchange_strong(expected, desired,
                                                             std::memory_order_release,
                                                             std::memory_order_relaxed))
                            {
                                size_.fetch_add(1);
                                MaybeStartResize();
                                HelpResize();
                                return;
                            }
                            delete desired;
                        }
                        else if (ptr->hash == h && ptr->key == key)
                        {
                            auto g       = ptr->v.acquire();
                            bool wasDead = !g.is_valid();
                            ptr->v.emplace(value);
                            if (wasDead)
                                size_.fetch_add(1);
                            return;
                        }
                    }
                }
                VirtualBucket* next = vb->next.load();
                if (!next)
                {
                    auto* new_vb = new VirtualBucket();
                    if (vb->next.compare_exchange_strong(next, new_vb,
                                                         std::memory_order_release,
                                                         std::memory_order_relaxed))
                        vb = new_vb;
                    else
                    {
                        delete new_vb;
                        vb = next;
                    }
                }
                else
                    vb = next;
            }
        }

        // Rvalue overload to avoid unnecessary copy in hot paths.
        void Insert(const Key& key, Value&& value)
        {
            const size_t   h       = hasher(key);
            auto           tv      = LoadTable();
            const size_t   idx     = h & (tv.len - 1);
            auto*          buckets = tv.buckets;
            VirtualBucket* vb      = buckets[idx].load(std::memory_order_acquire);

            while (vb)
            {
                if constexpr (kUsePairCAS)
                {
                    for (auto& slot: vb->slots)
                    {
                        BucketPtr bp = slot.load(std::memory_order_acquire);
                        if (bp.data == nullptr)
                        {
                            auto*     bd = new BucketData(h, key, std::move(value));
                            BucketPtr expected {0, nullptr};
                            BucketPtr desired {h, bd};
                            if (slot.compare_exchange_strong(expected, desired,
                                                             std::memory_order_release,
                                                             std::memory_order_relaxed))
                            {
                                size_.fetch_add(1);
                                MaybeStartResize();
                                HelpResize();
                                return;
                            }
                            delete bd;
                        }
                        else if (bp.hash == h && bp.data->key == key)
                        {
                            auto g       = bp.data->v.acquire();
                            bool wasDead = !g.is_valid();
                            bp.data->v.emplace(std::move(value));
                            if (wasDead)
                                size_.fetch_add(1);
                            return;
                        }
                    }
                }
                else
                {
                    for (auto& slot: vb->slots)
                    {
                        BucketData* ptr = slot.load(std::memory_order_acquire);
                        if (ptr == nullptr)
                        {
                            auto* expected = static_cast<BucketData*>(nullptr);
                            auto* desired  = new BucketData(h, key, std::move(value));
                            if (slot.compare_exchange_strong(expected, desired,
                                                             std::memory_order_release,
                                                             std::memory_order_relaxed))
                            {
                                size_.fetch_add(1);
                                MaybeStartResize();
                                HelpResize();
                                return;
                            }
                            delete desired;
                        }
                        else if (ptr->hash == h && ptr->key == key)
                        {
                            auto g       = ptr->v.acquire();
                            bool wasDead = !g.is_valid();
                            ptr->v.emplace(std::move(value));
                            if (wasDead)
                                size_.fetch_add(1);
                            return;
                        }
                    }
                }
                VirtualBucket* next = vb->next.load();
                if (!next)
                {
                    auto* new_vb = new VirtualBucket();
                    if (vb->next.compare_exchange_strong(next, new_vb,
                                                         std::memory_order_release,
                                                         std::memory_order_relaxed))
                        vb = new_vb;
                    else
                    {
                        delete new_vb;
                        vb = next;
                    }
                }
                else
                    vb = next;
            }
        }

        void Remove(const Key& key)
        {
            HelpResize();
            const size_t   h       = hasher(key);
            auto           tv      = LoadTable();
            const size_t   idx     = h & (tv.len - 1);
            auto*          buckets = tv.buckets;
            VirtualBucket* vb      = buckets[idx].load(std::memory_order_acquire);

            while (vb)
            {
                for (auto& slot: vb->slots)
                {
                    if constexpr (kUsePairCAS)
                    {
                        BucketPtr bp = slot.load(std::memory_order_acquire);
                        if (bp.hash == h && bp.data && bp.data->key == key)
                        {
                            auto g = bp.data->v.acquire();
                            if (g.is_valid())
                            {
                                bp.data->v.drop();
                                size_.fetch_sub(1);
                            }
                            return;
                        }
                    }
                    else
                    {
                        BucketData* ptr = slot.load(std::memory_order_acquire);
                        if (ptr && ptr->hash == h && ptr->key == key)
                        {
                            auto g = ptr->v.acquire();
                            if (g.is_valid())
                            {
                                ptr->v.drop();
                                size_.fetch_sub(1);
                            }
                            return;
                        }
                    }
                }
                vb = vb->next.load(std::memory_order_acquire);
            }
        }

        bool Contains(const Key& key) const
        {
            // const version cannot help migrate; best effort only (benign race)
            const size_t   h       = hasher(key);
            auto           tv      = LoadTable();
            const size_t   idx     = h & (tv.len - 1);
            auto*          buckets = tv.buckets;
            VirtualBucket* vb      = buckets[idx].load(std::memory_order_acquire);

            while (vb)
            {
                for (auto& slot: vb->slots)
                {
                    if constexpr (kUsePairCAS)
                    {
                        BucketPtr bp = slot.load(std::memory_order_acquire);
                        if (bp.hash == h && bp.data && bp.data->key == key)
                        {
                            auto g = bp.data->v.acquire();
                            return g.is_valid();
                        }
                    }
                    else
                    {
                        BucketData* ptr = slot.load(std::memory_order_acquire);
                        if (ptr && ptr->hash == h && ptr->key == key)
                        {
                            auto g = ptr->v.acquire();
                            return g.is_valid();
                        }
                    }
                }
                vb = vb->next.load(std::memory_order_acquire);
            }
            return false;
        }

        Value Get(const Key& key) const
        {
            // const version cannot help migrate; best effort only
            const size_t   h       = hasher(key);
            auto           tv      = LoadTable();
            const size_t   idx     = h & (tv.len - 1);
            auto*          buckets = tv.buckets;
            VirtualBucket* vb      = buckets[idx].load(std::memory_order_acquire);

            while (vb)
            {
                for (auto& slot: vb->slots)
                {
                    if constexpr (kUsePairCAS)
                    {
                        BucketPtr bp = slot.load(std::memory_order_acquire);
                        if (bp.hash == h && bp.data && bp.data->key == key)
                        {
                            auto g = bp.data->v.acquire();
                            if (g.is_valid())
                                return *g;
                            break;
                        }
                    }
                    else
                    {
                        BucketData* ptr = slot.load(std::memory_order_acquire);
                        if (ptr && ptr->hash == h && ptr->key == key)
                        {
                            auto g = ptr->v.acquire();
                            if (g.is_valid())
                                return *g;
                            break;
                        }
                    }
                }
                vb = vb->next.load(std::memory_order_acquire);
            }
            throw std::out_of_range("Key not found");
        }

        // Non-throwing retrieval; returns true on success, false if absent or tombstoned.
        bool TryGet(const Key& key, Value& out) const
        {
            // const version cannot help migrate; best effort only
            const size_t   h       = hasher(key);
            auto           tv      = LoadTable();
            const size_t   idx     = h & (tv.len - 1);
            auto*          buckets = tv.buckets;
            VirtualBucket* vb      = buckets[idx].load(std::memory_order_acquire);

            while (vb)
            {
                for (auto& slot: vb->slots)
                {
                    if constexpr (kUsePairCAS)
                    {
                        BucketPtr bp = slot.load(std::memory_order_acquire);
                        if (bp.hash == h && bp.data && bp.data->key == key)
                        {
                            auto g = bp.data->v.acquire();
                            if (g.is_valid())
                            {
                                out = *g;
                                return true;
                            }
                            return false;
                        }
                    }
                    else
                    {
                        BucketData* ptr = slot.load(std::memory_order_acquire);
                        if (ptr && ptr->hash == h && ptr->key == key)
                        {
                            auto g = ptr->v.acquire();
                            if (g.is_valid())
                            {
                                out = *g;
                                return true;
                            }
                            return false;
                        }
                    }
                }
                vb = vb->next.load(std::memory_order_acquire);
            }
            return false;
        }

        // Optional-return convenience.
        std::optional<Value> GetOptional(const Key& key) const
        {
            Value v;
            if (TryGet(key, v))
                return v;
            return std::nullopt;
        }

        void Clear()
        {
            HelpResize();
            auto* buckets = tableBuckets.load(std::memory_order_acquire);
            for (std::size_t i = 0; i < tableLen.load(std::memory_order_acquire); ++i)
            {
                VirtualBucket* vb = buckets[i].load(std::memory_order_acquire);
                while (vb)
                {
                    for (auto& s: vb->slots)
                    {
                        if constexpr (kUsePairCAS)
                        {
                            BucketPtr bp = s.load(std::memory_order_acquire);
                            if (bp.data)
                            {
                                auto g = bp.data->v.acquire();
                                if (g.is_valid())
                                    bp.data->v.drop();
                            }
                        }
                        else
                        {
                            BucketData* ptr = s.load(std::memory_order_acquire);
                            if (ptr)
                            {
                                auto g = ptr->v.acquire();
                                if (g.is_valid())
                                    ptr->v.drop();
                            }
                        }
                    }
                    vb = vb->next.load(std::memory_order_acquire);
                }
            }
            size_.store(0, std::memory_order_relaxed);
        }

        size_t Size() const
        {
            return size_.load();
        }
    };

}// namespace NGIN::Containers
