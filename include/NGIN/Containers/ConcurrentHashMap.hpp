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
#include <thread>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include <optional>
// Phase 6: epoch based reclamation (experimental)
#include <NGIN/Memory/EpochReclaimer.hpp>

namespace NGIN::Containers
{

    // ============================================================
    // SharedDoubleReferenceGuard (RCU / epoch variant)
    // ============================================================
    // Replaces shared_ptr-based guard with raw pointer + epoch retirement.
    // Semantics preserved for existing call sites:
    //  - acquire(): returns DataGuard with raw pointer (valid while epoch guard held)
    //  - emplace(args...): allocates new Value; returns true iff previous pointer was null (tombstone->live)
    //  - drop(): atomically sets pointer to null; returns true iff previous was non-null (live->tombstone)
    // Lifetime: old values retired via EpochReclaimer ensuring safe reclamation after quiescence.
    template<typename DataType>
    class SharedDoubleReferenceGuard
    {
        std::atomic<DataType*> m_ptr {nullptr};

    public:
        class DataGuard
        {
            DataType* p {nullptr};
            explicit DataGuard(DataType* raw) : p(raw) {}
            friend class SharedDoubleReferenceGuard<DataType>;

        public:
            DataGuard() = default;
            bool            is_valid() const noexcept { return p != nullptr; }
            const DataType* operator->() const noexcept { return p; }
            const DataType& operator*() const noexcept { return *p; }
        };

        DataGuard acquire() const noexcept { return DataGuard(m_ptr.load(std::memory_order_acquire)); }

        template<typename... Ts>
        bool emplace(Ts&&... args)
        {
            NGIN::Memory::EpochReclaimer::Guard g;
            auto*                               fresh = new DataType(std::forward<Ts>(args)...);
            DataType*                           old   = m_ptr.exchange(fresh, std::memory_order_acq_rel);
            if (old)
            {
                NGIN::Memory::EpochReclaimer::Instance().Retire(old, [](void* p) { delete static_cast<DataType*>(p); });
                return false;
            }
            return true;
        }

        bool drop() noexcept
        {
            DataType* old = m_ptr.exchange(nullptr, std::memory_order_acq_rel);
            if (old)
            {
                NGIN::Memory::EpochReclaimer::Instance().Retire(old, [](void* p) { delete static_cast<DataType*>(p); });
                return true;
            }
            return false;
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
                // Iteratively delete chain to avoid recursion; detach first to be idempotent.
                VirtualBucket* p = next.exchange(nullptr, std::memory_order_acq_rel);
                while (p)
                {
                    VirtualBucket* nxt = p->next.exchange(nullptr, std::memory_order_acq_rel);
                    delete p;
                    p = nxt;
                }

                if constexpr (kUsePairCAS)
                {
                    for (auto& s: slots)
                    {
                        auto bp = s.load(std::memory_order_relaxed);
                        if (bp.data)
                            delete bp.data;
                    }
                }
                else
                {
                    for (auto& s: slots)
                    {
                        if (auto* ptr = s.load(std::memory_order_relaxed))
                            delete ptr;
                    }
                }
            }
        };

        struct TableHeader
        {
            std::size_t                  len;
            std::atomic<VirtualBucket*>* buckets;
        };
        // Single atomic pointer publish of header (len + buckets) avoids torn snapshot issues.
        std::atomic<TableHeader*> tableHeader {nullptr};
        std::atomic<std::size_t>  size_ {0};
        // Track allocated tables via a lock-free stack (avoid unsynchronized std::vector access during concurrent resizes)
        struct TableAllocationNode
        {
            std::atomic<VirtualBucket*>* buckets;
            std::size_t                  len;
            std::atomic<bool>            retired {false};// <<< add this
            TableAllocationNode*         next;
        };

        std::atomic<TableAllocationNode*> allocationsHead {nullptr};

        void TrackAllocation(std::atomic<VirtualBucket*>* buckets, std::size_t len) noexcept
        {
            auto* node    = new TableAllocationNode {};
            node->buckets = buckets;
            node->len     = len;
            node->retired.store(false, std::memory_order_relaxed);
            node->next = allocationsHead.load(std::memory_order_relaxed);
            while (!allocationsHead.compare_exchange_weak(
                    node->next, node, std::memory_order_release, std::memory_order_relaxed)) {}
        }


        void MarkRetired(std::atomic<VirtualBucket*>* which) noexcept
        {
            for (auto* n = allocationsHead.load(std::memory_order_acquire); n; n = n->next)
            {
                if (n->buckets == which)
                {
                    n->retired.store(true, std::memory_order_release);
                    break;
                }
            }
        }

        static void FreeTable(std::atomic<VirtualBucket*>* buckets, std::size_t len) noexcept
        {
            if (!buckets)
                return;
            for (std::size_t i = 0; i < len; ++i)
            {
                if (auto* vb = buckets[i].load(std::memory_order_relaxed))
                    delete vb;// VirtualBucket dtor iteratively frees chain
            }
            delete[] buckets;
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

        std::size_t mask() const noexcept
        {
            auto* hdr = tableHeader.load(std::memory_order_acquire);
            return hdr->len - 1;
        }

        struct TableView
        {
            std::atomic<VirtualBucket*>* buckets;
            std::size_t                  len;
        };

        // Acquire a self-consistent snapshot of (buckets pointer, length).
        // Publisher order: BUCKETS then LEN. We double-load BUCKETS and accept when
        // stable so that LEN corresponds to the same buckets array; this avoids the
        // torn state (new len with old buckets) that can cause out-of-bounds access.
        TableView LoadTable() const noexcept
        {
            auto* hdr = tableHeader.load(std::memory_order_acquire);
            return {hdr->buckets, hdr->len};
        }

        // Attempt to start a cooperative resize (non-blocking).
        void MaybeStartResize()
        {
            // Heuristic: load factor > 0.75 triggers resize attempt.
            auto* hdr    = tableHeader.load(std::memory_order_acquire);
            auto  curLen = hdr->len;
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
            // Create state object (not yet published)
            auto*        state    = new ResizeState(hdr->buckets, curLen, new_tab, new_len);
            ResizeState* expected = nullptr;
            if (!resizeState.compare_exchange_strong(expected, state, std::memory_order_release, std::memory_order_relaxed))
            {
                // Lost race; free newly allocated table & state (was never published)
                FreeTable(new_tab, new_len);
                delete state;
            }
            else
            {
                // Track only after successful publication attempt wins.
                TrackAllocation(new_tab, new_len);
            }
        }

        // Place (hash,key,value) into destination table (used during migration). Two-pass duplicate-safe.
        void PlaceInto(VirtualBucket* root, std::size_t h, const Key& key, const Value& value)
        {
            // Pass A: scan entire chain for existing key (live or tombstoned) BEFORE attempting any insert.
            for (VirtualBucket* scan = root; scan; scan = scan->next.load(std::memory_order_acquire))
            {
                if constexpr (kUsePairCAS)
                {
                    for (auto& slot: scan->slots)
                    {
                        BucketPtr bp = slot.load(std::memory_order_acquire);
                        if (bp.data && bp.hash == h && bp.data->key == key)
                        {
                            auto g = bp.data->v.acquire();
                            if (!g.is_valid())
                                bp.data->v.emplace(value);// resurrection handled (size accounted by caller)
                            return;
                        }
                    }
                }
                else
                {
                    for (auto& slot: scan->slots)
                    {
                        BucketData* ptr = slot.load(std::memory_order_acquire);
                        if (ptr && ptr->hash == h && ptr->key == key)
                        {
                            auto g = ptr->v.acquire();
                            if (!g.is_valid())
                                ptr->v.emplace(value);
                            return;
                        }
                    }
                }
            }
            // Pass B: attempt insertion; re-check for duplicate in each node before claiming empties (in case race inserted it).
            VirtualBucket* vb = root;
            while (vb)
            {
                if constexpr (kUsePairCAS)
                {
                    for (auto& slot: vb->slots)
                    {
                        BucketPtr bp = slot.load(std::memory_order_acquire);
                        if (bp.data && bp.hash == h && bp.data->key == key)
                            return;// another thread inserted between passes
                        if (bp.data == nullptr)
                        {
                            auto*     bd = new BucketData(h, key, value);
                            BucketPtr expected {0, nullptr};
                            BucketPtr desired {h, bd};
                            if (slot.compare_exchange_strong(expected, desired, std::memory_order_release, std::memory_order_relaxed))
                                return;
                            delete bd;
                        }
                    }
                }
                else
                {
                    for (auto& slot: vb->slots)
                    {
                        BucketData* cur = slot.load(std::memory_order_acquire);
                        if (cur && cur->hash == h && cur->key == key)
                            return;
                        if (cur == nullptr)
                        {
                            auto* expected = static_cast<BucketData*>(nullptr);
                            auto* desired  = new BucketData(h, key, value);
                            if (slot.compare_exchange_strong(expected, desired, std::memory_order_release, std::memory_order_relaxed))
                                return;
                            delete desired;
                        }
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
                                // Acquire to observe fully constructed VirtualBucket published at allocation.
                                PlaceInto(state->new_table[bp.hash & (state->new_len - 1)].load(std::memory_order_acquire), bp.hash, bp.data->key, *g);
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
                                // Acquire to observe fully constructed VirtualBucket published at allocation.
                                PlaceInto(state->new_table[ptr->hash & (state->new_len - 1)].load(std::memory_order_acquire), ptr->hash, ptr->key, *g);
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
            NGIN::Memory::EpochReclaimer::Guard epochGuard;// <— pin lifetime of resize structures

            ResizeState* state = resizeState.load(std::memory_order_acquire);
            if (!state)
                return;

            MigrateOne(state);

            if (state->migratedCount.load(std::memory_order_acquire) == state->old_len)
            {
                auto* newHdr = new TableHeader {state->new_len, state->new_table};
                auto* oldHdr = tableHeader.load(std::memory_order_acquire);
                tableHeader.store(newHdr, std::memory_order_release);

                // Retire the tiny header with EBR
                NGIN::Memory::EpochReclaimer::Instance().Retire(oldHdr, [](void* p) {
                    delete static_cast<TableHeader*>(p);
                });

                // Only the winning thread clears the state
                ResizeState* expected = state;
                if (resizeState.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed))
                {
                    // ALSO retire the OLD TABLE ARRAY under EBR to prevent UAF in MigrateOne
                    auto* old_tab = state->old_table;
                    auto  old_len = state->old_len;

                    MarkRetired(old_tab);

                    NGIN::Memory::EpochReclaimer::Instance().Retire(old_tab, [old_len](void* p) {
                        auto* buckets = static_cast<std::atomic<VirtualBucket*>*>(p);
                        if (!buckets)
                            return;
                        for (std::size_t i = 0; i < old_len; ++i)
                        {
                            if (auto* vb = buckets[i].load(std::memory_order_relaxed))
                                delete vb;// VirtualBucket dtor deletes its chain & payloads
                        }
                        delete[] buckets;
                    });

                    // Finally retire the ResizeState itself
                    NGIN::Memory::EpochReclaimer::Instance().Retire(state, [](void* p) {
                        delete static_cast<ResizeState*>(p);
                    });
                }
            }
        }


        // Dual write with finalize-aware fallback: if resizeState vanished after old-table insert, mirror into current header table.
        template<typename V>
        void DualWriteIfNeeded(std::size_t idx, std::size_t h, const Key& key, const V& value,
                               std::atomic<VirtualBucket*>* srcTable)
        {
            if (ResizeState* rs = resizeState.load(std::memory_order_acquire);
                rs && rs->old_table == srcTable && idx < rs->old_len)
            {
                if (rs->markers[idx].load(std::memory_order_acquire) >= 1)
                {
                    auto* root = rs->new_table[h & (rs->new_len - 1)].load(std::memory_order_acquire);
                    PlaceInto(root, h, key, value);
                }
                return;
            }
            // Fallback: resize finalized; if we inserted into a non-current table, replicate into current table.
            TableHeader* hdr = tableHeader.load(std::memory_order_acquire);
            if (hdr && hdr->buckets != srcTable)
            {
                auto* root = hdr->buckets[h & (hdr->len - 1)].load(std::memory_order_acquire);
                PlaceInto(root, h, key, value);
            }
        }

        // Determine whether a dual lookup is needed and whether to prefer new table first.
        bool NeedDualLookup(std::size_t idx, const TableView& tv, ResizeState* rs, bool& preferNew) const noexcept
        {
            preferNew = false;
            if (!rs)
                return false;
            if (rs->old_table != tv.buckets)
                return false;
            if (idx >= rs->old_len)
                return false;
            int marker = rs->markers[idx].load(std::memory_order_acquire);
            if (marker >= 1)
            {
                preferNew = (marker == 2);
                return true;
            }
            return false;
        }

        // Dual drop with finalize-aware fallback mirroring.
        void DualDropIfNeeded(std::size_t idx, std::size_t h, const Key& key, std::atomic<VirtualBucket*>* srcTable)
        {
            if (ResizeState* rs = resizeState.load(std::memory_order_acquire);
                rs && rs->old_table == srcTable && idx < rs->old_len &&
                rs->markers[idx].load(std::memory_order_acquire) >= 1)
            {
                auto* newRoot = rs->new_table[h & (rs->new_len - 1)].load(std::memory_order_acquire);
                for (VirtualBucket* vb = newRoot; vb; vb = vb->next.load(std::memory_order_acquire))
                {
                    for (auto& slot: vb->slots)
                    {
                        if constexpr (kUsePairCAS)
                        {
                            BucketPtr bp = slot.load(std::memory_order_acquire);
                            if (bp.data && bp.hash == h && bp.data->key == key)
                            {
                                bp.data->v.drop();
                                return;
                            }
                        }
                        else
                        {
                            BucketData* ptr = slot.load(std::memory_order_acquire);
                            if (ptr && ptr->hash == h && ptr->key == key)
                            {
                                ptr->v.drop();
                                return;
                            }
                        }
                    }
                }
                return;
            }
            // Finalized fallback
            TableHeader* hdr = tableHeader.load(std::memory_order_acquire);
            if (hdr && hdr->buckets != srcTable)
            {
                auto* curRoot = hdr->buckets[h & (hdr->len - 1)].load(std::memory_order_acquire);
                for (VirtualBucket* vb = curRoot; vb; vb = vb->next.load(std::memory_order_acquire))
                {
                    for (auto& slot: vb->slots)
                    {
                        if constexpr (kUsePairCAS)
                        {
                            BucketPtr bp = slot.load(std::memory_order_acquire);
                            if (bp.data && bp.hash == h && bp.data->key == key)
                            {
                                bp.data->v.drop();
                                return;
                            }
                        }
                        else
                        {
                            BucketData* ptr = slot.load(std::memory_order_acquire);
                            if (ptr && ptr->hash == h && ptr->key == key)
                            {
                                ptr->v.drop();
                                return;
                            }
                        }
                    }
                }
            }
        }

    public:
        explicit ConcurrentHashMap(std::size_t initial_capacity = 16)
        {
            const std::size_t cap     = next_pow2(initial_capacity);
            auto*             buckets = new std::atomic<VirtualBucket*>[cap];
            for (std::size_t i = 0; i < cap; ++i)
            {
                buckets[i].store(new VirtualBucket(), std::memory_order_release);
            }
            auto* hdr = new TableHeader {cap, buckets};
            tableHeader.store(hdr, std::memory_order_release);
            TrackAllocation(buckets, cap);
            assert(cap && (cap & (cap - 1)) == 0 && "table_len must be power of two");
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
            // Ensure any scheduled frees (from resizes) actually run now.
            NGIN::Memory::EpochReclaimer::Instance().ForceDrain();

            auto* node = allocationsHead.load(std::memory_order_acquire);
            while (node)
            {
                if (!node->retired.load(std::memory_order_acquire))
                {
                    // Not retired via EBR → we own freeing it here.
                    for (std::size_t i = 0; i < node->len; ++i)
                    {
                        if (auto* vb = node->buckets[i].load(std::memory_order_relaxed))
                            delete vb;// VirtualBucket dtor handles chain and values
                    }
                    delete[] node->buckets;
                }
                auto* next = node->next;
                delete node;
                node = next;
            }
            if (auto* hdr = tableHeader.load(std::memory_order_acquire))
                delete hdr;// tiny
        }

        // Debug / test helper: fully drain any in-progress resize so subsequent Contains/Get
        // observe the final table. Bounded helping loop.
        void DebugDrainResize()
        {
            NGIN::Memory::EpochReclaimer::Guard epochGuard;// protect resizeState during draining
            for (int i = 0; i < 100000; ++i)               // large upper bound; exits early when done
            {
                if (!resizeState.load(std::memory_order_acquire))
                    return;
                HelpResize();
            }
        }

        void Insert(const Key& key, const Value& value)
        {
            NGIN::Memory::EpochReclaimer::Guard epochGuard;
            const size_t                        h       = hasher(key);
            auto                                tv      = LoadTable();
            const size_t                        idx     = h & (tv.len - 1);
            auto*                               buckets = tv.buckets;
            VirtualBucket*                      vb      = buckets[idx].load(std::memory_order_acquire);

            // If a resize is active and this bucket was already migrated, insert/update in new table (two-pass)
            if (auto* rs = resizeState.load(std::memory_order_acquire); rs && rs->old_table == tv.buckets && idx < rs->old_len)
            {
                int migratedState = rs->markers[idx].load(std::memory_order_acquire);
                if (migratedState == 2)
                {
                    auto*          newRoot = rs->new_table[h & (rs->new_len - 1)].load(std::memory_order_acquire);
                    VirtualBucket* cur     = newRoot;
                    // Pass A: search
                    for (VirtualBucket* scan = cur; scan; scan = scan->next.load(std::memory_order_acquire))
                    {
                        if constexpr (kUsePairCAS)
                        {
                            for (auto& slot: scan->slots)
                            {
                                BucketPtr bp = slot.load(std::memory_order_acquire);
                                if (bp.data && bp.hash == h && bp.data->key == key)
                                {
                                    bool inserted = bp.data->v.emplace(value);
                                    if (inserted)
                                        size_.fetch_add(1);
                                    DualWriteIfNeeded(idx, h, key, value, tv.buckets);
                                    MaybeStartResize();
                                    HelpResize();
                                    return;
                                }
                            }
                        }
                        else
                        {
                            for (auto& slot: scan->slots)
                            {
                                BucketData* ptr = slot.load(std::memory_order_acquire);
                                if (ptr && ptr->hash == h && ptr->key == key)
                                {
                                    bool inserted = ptr->v.emplace(value);
                                    if (inserted)
                                        size_.fetch_add(1);
                                    DualWriteIfNeeded(idx, h, key, value, tv.buckets);
                                    MaybeStartResize();
                                    HelpResize();
                                    return;
                                }
                            }
                        }
                    }
                    // Pass B: insert
                    while (cur)
                    {
                        if constexpr (kUsePairCAS)
                        {
                            for (auto& slot: cur->slots)
                            {
                                BucketPtr bp = slot.load(std::memory_order_acquire);
                                if (bp.data && bp.hash == h && bp.data->key == key)
                                {
                                    bool inserted = bp.data->v.emplace(value);
                                    if (inserted)
                                        size_.fetch_add(1);
                                    DualWriteIfNeeded(idx, h, key, value, tv.buckets);
                                    MaybeStartResize();
                                    HelpResize();
                                    return;
                                }
                                if (bp.data == nullptr)
                                {
                                    auto*     bd = new BucketData(h, key, value);
                                    BucketPtr expected {0, nullptr};
                                    BucketPtr desired {h, bd};
                                    if (slot.compare_exchange_strong(expected, desired, std::memory_order_release, std::memory_order_relaxed))
                                    {
                                        size_.fetch_add(1);
                                        DualWriteIfNeeded(idx, h, key, value, tv.buckets);
                                        MaybeStartResize();
                                        HelpResize();
                                        return;
                                    }
                                    delete bd;
                                }
                            }
                        }
                        else
                        {
                            for (auto& slot: cur->slots)
                            {
                                BucketData* ptr = slot.load(std::memory_order_acquire);
                                if (ptr && ptr->hash == h && ptr->key == key)
                                {
                                    bool inserted = ptr->v.emplace(value);
                                    if (inserted)
                                        size_.fetch_add(1);
                                    DualWriteIfNeeded(idx, h, key, value, tv.buckets);
                                    MaybeStartResize();
                                    HelpResize();
                                    return;
                                }
                                if (ptr == nullptr)
                                {
                                    auto* expected = static_cast<BucketData*>(nullptr);
                                    auto* desired  = new BucketData(h, key, value);
                                    if (slot.compare_exchange_strong(expected, desired, std::memory_order_release, std::memory_order_relaxed))
                                    {
                                        size_.fetch_add(1);
                                        DualWriteIfNeeded(idx, h, key, value, tv.buckets);
                                        MaybeStartResize();
                                        HelpResize();
                                        return;
                                    }
                                    delete desired;
                                }
                            }
                        }
                        VirtualBucket* next = cur->next.load(std::memory_order_acquire);
                        if (!next)
                        {
                            auto* new_vb = new VirtualBucket();
                            if (cur->next.compare_exchange_strong(next, new_vb, std::memory_order_release, std::memory_order_relaxed))
                                cur = new_vb;
                            else
                            {
                                delete new_vb;
                                cur = next;
                            }
                        }
                        else
                            cur = next;
                    }
                }
            }

            // Pass A: scan entire chain for existing key first
            for (VirtualBucket* scan = vb; scan; scan = scan->next.load(std::memory_order_acquire))
            {
                if constexpr (kUsePairCAS)
                {
                    for (auto& slot: scan->slots)
                    {
                        BucketPtr bp = slot.load(std::memory_order_acquire);
                        if (bp.data && bp.hash == h && bp.data->key == key)
                        {
                            bool inserted = bp.data->v.emplace(value);
                            if (inserted)
                                size_.fetch_add(1);
                            DualWriteIfNeeded(idx, h, key, value, tv.buckets);
                            return;
                        }
                    }
                }
                else
                {
                    for (auto& slot: scan->slots)
                    {
                        BucketData* ptr = slot.load(std::memory_order_acquire);
                        if (ptr && ptr->hash == h && ptr->key == key)
                        {
                            bool inserted = ptr->v.emplace(value);
                            if (inserted)
                                size_.fetch_add(1);
                            DualWriteIfNeeded(idx, h, key, value, tv.buckets);
                            return;
                        }
                    }
                }
            }
            // Pass B: insert
            while (vb)
            {
                if constexpr (kUsePairCAS)
                {
                    for (auto& slot: vb->slots)
                    {
                        BucketPtr bp = slot.load(std::memory_order_acquire);
                        if (bp.data && bp.hash == h && bp.data->key == key)
                        {
                            bool inserted = bp.data->v.emplace(value);
                            if (inserted)
                                size_.fetch_add(1);
                            DualWriteIfNeeded(idx, h, key, value, tv.buckets);
                            return;
                        }
                        if (bp.data == nullptr)
                        {
                            auto*     bd = new BucketData(h, key, value);
                            BucketPtr expected {0, nullptr};
                            BucketPtr desired {h, bd};
                            if (slot.compare_exchange_strong(expected, desired, std::memory_order_release, std::memory_order_relaxed))
                            {
                                size_.fetch_add(1);
                                DualWriteIfNeeded(idx, h, key, value, tv.buckets);
                                MaybeStartResize();
                                HelpResize();
                                return;
                            }
                            delete bd;
                        }
                    }
                }
                else
                {
                    for (auto& slot: vb->slots)
                    {
                        BucketData* ptr = slot.load(std::memory_order_acquire);
                        if (ptr && ptr->hash == h && ptr->key == key)
                        {
                            bool inserted = ptr->v.emplace(value);
                            if (inserted)
                                size_.fetch_add(1);
                            DualWriteIfNeeded(idx, h, key, value, tv.buckets);
                            return;
                        }
                        if (ptr == nullptr)
                        {
                            auto* expected = static_cast<BucketData*>(nullptr);
                            auto* desired  = new BucketData(h, key, value);
                            if (slot.compare_exchange_strong(expected, desired, std::memory_order_release, std::memory_order_relaxed))
                            {
                                size_.fetch_add(1);
                                DualWriteIfNeeded(idx, h, key, value, tv.buckets);
                                MaybeStartResize();
                                HelpResize();
                                return;
                            }
                            delete desired;
                        }
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

        // Rvalue overload to avoid unnecessary copy in hot paths.
        void Insert(const Key& key, Value&& value)
        {
            NGIN::Memory::EpochReclaimer::Guard epochGuard;
            const size_t                        h       = hasher(key);
            auto                                tv      = LoadTable();
            const size_t                        idx     = h & (tv.len - 1);
            auto*                               buckets = tv.buckets;
            VirtualBucket*                      vb      = buckets[idx].load(std::memory_order_acquire);
            // rvalue path: no pre-snapshot gating

            if (auto* rs = resizeState.load(std::memory_order_acquire); rs && rs->old_table == tv.buckets && idx < rs->old_len)
            {
                int migratedState = rs->markers[idx].load(std::memory_order_acquire);
                if (migratedState == 2)
                {
                    auto*          newRoot = rs->new_table[h & (rs->new_len - 1)].load(std::memory_order_acquire);
                    VirtualBucket* cur     = newRoot;
                    // Pass A search
                    for (VirtualBucket* scan = cur; scan; scan = scan->next.load(std::memory_order_acquire))
                    {
                        if constexpr (kUsePairCAS)
                        {
                            for (auto& slot: scan->slots)
                            {
                                BucketPtr bp = slot.load(std::memory_order_acquire);
                                if (bp.data && bp.hash == h && bp.data->key == key)
                                {
                                    bool inserted = bp.data->v.emplace(value);// cannot move now (value possibly moved later)
                                    if (inserted)
                                        size_.fetch_add(1);
                                    // new table path only
                                    MaybeStartResize();
                                    HelpResize();
                                    return;
                                }
                            }
                        }
                        else
                        {
                            for (auto& slot: scan->slots)
                            {
                                BucketData* ptr = slot.load(std::memory_order_acquire);
                                if (ptr && ptr->hash == h && ptr->key == key)
                                {
                                    bool inserted = ptr->v.emplace(value);
                                    if (inserted)
                                        size_.fetch_add(1);
                                    DualWriteIfNeeded(idx, h, key, value, tv.buckets);
                                    MaybeStartResize();
                                    HelpResize();
                                    return;
                                }
                            }
                        }
                    }
                    // Pass B insert (moving value when actually inserting/resurrecting)
                    while (cur)
                    {
                        if constexpr (kUsePairCAS)
                        {
                            for (auto& slot: cur->slots)
                            {
                                BucketPtr bp = slot.load(std::memory_order_acquire);
                                if (bp.data && bp.hash == h && bp.data->key == key)
                                {
                                    bool inserted = bp.data->v.emplace(value);
                                    if (inserted)
                                        size_.fetch_add(1);
                                    // new table path only
                                    MaybeStartResize();
                                    HelpResize();
                                    return;
                                }
                                if (bp.data == nullptr)
                                {
                                    auto*     bd = new BucketData(h, key, std::move(value));
                                    BucketPtr expected {0, nullptr};
                                    BucketPtr desired {h, bd};
                                    if (slot.compare_exchange_strong(expected, desired, std::memory_order_release, std::memory_order_relaxed))
                                    {
                                        size_.fetch_add(1);
                                        auto guard = desired.data->v.acquire();
                                        if (guard.is_valid()) { /* new table only */ }
                                        MaybeStartResize();
                                        HelpResize();
                                        return;
                                    }
                                    delete bd;
                                }
                            }
                        }
                        else
                        {
                            for (auto& slot: cur->slots)
                            {
                                BucketData* ptr = slot.load(std::memory_order_acquire);
                                if (ptr && ptr->hash == h && ptr->key == key)
                                {
                                    bool inserted = ptr->v.emplace(value);
                                    if (inserted)
                                        size_.fetch_add(1);
                                    // new table path only
                                    MaybeStartResize();
                                    HelpResize();
                                    return;
                                }
                                if (ptr == nullptr)
                                {
                                    auto* expected = static_cast<BucketData*>(nullptr);
                                    auto* desired  = new BucketData(h, key, std::move(value));
                                    if (slot.compare_exchange_strong(expected, desired, std::memory_order_release, std::memory_order_relaxed))
                                    {
                                        size_.fetch_add(1);
                                        auto guard = desired->v.acquire();
                                        if (guard.is_valid()) { /* new table only */ }
                                        MaybeStartResize();
                                        HelpResize();
                                        return;
                                    }
                                    delete desired;
                                }
                            }
                        }
                        VirtualBucket* next = cur->next.load(std::memory_order_acquire);
                        if (!next)
                        {
                            auto* new_vb = new VirtualBucket();
                            if (cur->next.compare_exchange_strong(next, new_vb, std::memory_order_release, std::memory_order_relaxed))
                                cur = new_vb;
                            else
                            {
                                delete new_vb;
                                cur = next;
                            }
                        }
                        else
                            cur = next;
                    }
                }
            }

            // Pass A: search chain
            for (VirtualBucket* scan = vb; scan; scan = scan->next.load(std::memory_order_acquire))
            {
                if constexpr (kUsePairCAS)
                {
                    for (auto& slot: scan->slots)
                    {
                        BucketPtr bp = slot.load(std::memory_order_acquire);
                        if (bp.data && bp.hash == h && bp.data->key == key)
                        {
                            bool inserted = bp.data->v.emplace(value);// use copy (can't move yet)
                            if (inserted)
                                size_.fetch_add(1);
                            auto g = bp.data->v.acquire();
                            if (g.is_valid())
                                DualWriteIfNeeded(idx, h, key, *g, tv.buckets);
                            return;
                        }
                    }
                }
                else
                {
                    for (auto& slot: scan->slots)
                    {
                        BucketData* ptr = slot.load(std::memory_order_acquire);
                        if (ptr && ptr->hash == h && ptr->key == key)
                        {
                            bool inserted = ptr->v.emplace(value);
                            if (inserted)
                                size_.fetch_add(1);
                            auto g = ptr->v.acquire();
                            if (g.is_valid())
                                DualWriteIfNeeded(idx, h, key, *g, tv.buckets);
                            return;
                        }
                    }
                }
            }
            // Pass B: insert (moving value only when we actually claim a slot)
            while (vb)
            {
                if constexpr (kUsePairCAS)
                {
                    for (auto& slot: vb->slots)
                    {
                        BucketPtr bp = slot.load(std::memory_order_acquire);
                        if (bp.data && bp.hash == h && bp.data->key == key)
                        {
                            bool inserted = bp.data->v.emplace(value);
                            if (inserted)
                                size_.fetch_add(1);
                            auto g = bp.data->v.acquire();
                            if (g.is_valid())
                                DualWriteIfNeeded(idx, h, key, *g, tv.buckets);
                            return;
                        }
                        if (bp.data == nullptr)
                        {
                            auto*     bd = new BucketData(h, key, std::move(value));
                            BucketPtr expected {0, nullptr};
                            BucketPtr desired {h, bd};
                            if (slot.compare_exchange_strong(expected, desired, std::memory_order_release, std::memory_order_relaxed))
                            {
                                size_.fetch_add(1);
                                auto guard = desired.data->v.acquire();
                                if (guard.is_valid())
                                    DualWriteIfNeeded(idx, h, key, *guard, tv.buckets);
                                MaybeStartResize();
                                HelpResize();
                                return;
                            }
                            delete bd;
                        }
                    }
                }
                else
                {
                    for (auto& slot: vb->slots)
                    {
                        BucketData* ptr = slot.load(std::memory_order_acquire);
                        if (ptr && ptr->hash == h && ptr->key == key)
                        {
                            bool inserted = ptr->v.emplace(value);
                            if (inserted)
                                size_.fetch_add(1);
                            auto g = ptr->v.acquire();
                            if (g.is_valid())
                                DualWriteIfNeeded(idx, h, key, *g, tv.buckets);
                            return;
                        }
                        if (ptr == nullptr)
                        {
                            auto* expected = static_cast<BucketData*>(nullptr);
                            auto* desired  = new BucketData(h, key, std::move(value));
                            if (slot.compare_exchange_strong(expected, desired, std::memory_order_release, std::memory_order_relaxed))
                            {
                                size_.fetch_add(1);
                                auto guard = desired->v.acquire();
                                if (guard.is_valid())
                                    DualWriteIfNeeded(idx, h, key, *guard, tv.buckets);
                                MaybeStartResize();
                                HelpResize();
                                return;
                            }
                            delete desired;
                        }
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

        void Remove(const Key& key)
        {
            NGIN::Memory::EpochReclaimer::Guard epochGuard;
            HelpResize();
            const size_t   h         = hasher(key);
            auto           tv        = LoadTable();
            const size_t   idx       = h & (tv.len - 1);
            auto*          buckets   = tv.buckets;
            VirtualBucket* oldRoot   = buckets[idx].load(std::memory_order_acquire);
            ResizeState*   rsLive    = resizeState.load(std::memory_order_acquire);
            bool           canSeeNew = false;
            VirtualBucket* newRoot   = nullptr;
            if (rsLive && rsLive->old_table == tv.buckets && idx < rsLive->old_len)
            {
                int marker = rsLive->markers[idx].load(std::memory_order_acquire);
                canSeeNew  = marker >= 1;
                if (canSeeNew)
                    newRoot = rsLive->new_table[h & (rsLive->new_len - 1)].load(std::memory_order_acquire);
            }

            // Search old table first. If found, drop and (if needed) propagate to new table.
            for (VirtualBucket* vb = oldRoot; vb; vb = vb->next.load(std::memory_order_acquire))
            {
                for (auto& slot: vb->slots)
                {
                    if constexpr (kUsePairCAS)
                    {
                        BucketPtr bp = slot.load(std::memory_order_acquire);
                        if (bp.hash == h && bp.data && bp.data->key == key)
                        {
                            if (bp.data->v.drop())
                            {
                                size_.fetch_sub(1);
                                DualDropIfNeeded(idx, h, key, tv.buckets);
                            }
                            return;
                        }
                    }
                    else
                    {
                        BucketData* ptr = slot.load(std::memory_order_acquire);
                        if (ptr && ptr->hash == h && ptr->key == key)
                        {
                            if (ptr->v.drop())
                            {
                                size_.fetch_sub(1);
                                DualDropIfNeeded(idx, h, key, tv.buckets);
                            }
                            return;
                        }
                    }
                }
            }
            // Not found in old; if resize in progress and bucket at least claimed, search new table directly.
            if (canSeeNew && newRoot)
            {
                for (VirtualBucket* vb = newRoot; vb; vb = vb->next.load(std::memory_order_acquire))
                {
                    for (auto& slot: vb->slots)
                    {
                        if constexpr (kUsePairCAS)
                        {
                            BucketPtr bp = slot.load(std::memory_order_acquire);
                            if (bp.hash == h && bp.data && bp.data->key == key)
                            {
                                if (bp.data->v.drop())
                                    size_.fetch_sub(1);
                                return;
                            }
                        }
                        else
                        {
                            BucketData* ptr = slot.load(std::memory_order_acquire);
                            if (ptr && ptr->hash == h && ptr->key == key)
                            {
                                if (ptr->v.drop())
                                    size_.fetch_sub(1);
                                return;
                            }
                        }
                    }
                }
            }
        }

        bool Contains(const Key& key) const
        {
            NGIN::Memory::EpochReclaimer::Guard epochGuard;
            const size_t                        h         = hasher(key);
            auto                                tv        = LoadTable();
            const size_t                        idx       = h & (tv.len - 1);
            auto*                               buckets   = tv.buckets;
            ResizeState*                        rs        = resizeState.load(std::memory_order_acquire);
            bool                                preferNew = false;
            bool                                needDual  = NeedDualLookup(idx, tv, rs, preferNew);

            auto scanChain = [&](VirtualBucket* root) -> int {
                for (VirtualBucket* vb = root; vb; vb = vb->next.load(std::memory_order_acquire))
                {
                    for (auto& slot: vb->slots)
                    {
                        if constexpr (kUsePairCAS)
                        {
                            BucketPtr bp = slot.load(std::memory_order_acquire);
                            if (bp.hash == h && bp.data && bp.data->key == key)
                            {
                                auto g = bp.data->v.acquire();
                                return g.is_valid() ? 1 : 0;// 1=live,0=tombstoned
                            }
                        }
                        else
                        {
                            BucketData* ptr = slot.load(std::memory_order_acquire);
                            if (ptr && ptr->hash == h && ptr->key == key)
                            {
                                auto g = ptr->v.acquire();
                                return g.is_valid() ? 1 : 0;
                            }
                        }
                    }
                }
                return -1;// not found
            };

            VirtualBucket* oldRoot = buckets[idx].load(std::memory_order_acquire);
            VirtualBucket* newRoot = nullptr;
            if (needDual)
                newRoot = rs->new_table[h & (rs->new_len - 1)].load(std::memory_order_acquire);

            if (!needDual)
            {
                return scanChain(oldRoot) == 1;
            }
            // Dual path
            if (preferNew)
            {
                int r = scanChain(newRoot);
                if (r == 1)
                    return true;
                if (r == 0)// tombstone in new table; old won't have live copy
                    return false;
                // not found new, try old
                r = scanChain(oldRoot);
                if (r == 1)
                    return true;
                if (r == 0)
                    return false;
                return false;
            }
            else
            {
                int r = scanChain(oldRoot);
                if (r == 1)
                    return true;
                if (r == 0)
                {
                    // tombstoned old copy; new might have resurrected value
                    int rn = scanChain(newRoot);
                    return rn == 1;
                }
                // not found old; check new
                r = scanChain(newRoot);
                return r == 1;
            }
        }

        Value Get(const Key& key) const
        {
            NGIN::Memory::EpochReclaimer::Guard epochGuard;
            const size_t                        h         = hasher(key);
            auto                                tv        = LoadTable();
            const size_t                        idx       = h & (tv.len - 1);
            auto*                               buckets   = tv.buckets;
            ResizeState*                        rs        = resizeState.load(std::memory_order_acquire);
            bool                                preferNew = false;
            bool                                needDual  = NeedDualLookup(idx, tv, rs, preferNew);
            VirtualBucket*                      oldRoot   = buckets[idx].load(std::memory_order_acquire);
            VirtualBucket*                      newRoot   = needDual ? rs->new_table[h & (rs->new_len - 1)].load(std::memory_order_acquire) : nullptr;

            auto tryGetChain = [&](VirtualBucket* root, Value* out) -> int {
                for (VirtualBucket* vb = root; vb; vb = vb->next.load(std::memory_order_acquire))
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
                                    *out = *g;
                                    return 1;
                                }
                                return 0;// tombstone
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
                                    *out = *g;
                                    return 1;
                                }
                                return 0;
                            }
                        }
                    }
                }
                return -1;
            };

            Value out {};
            if (!needDual)
            {
                if (tryGetChain(oldRoot, &out) == 1)
                    return out;
                throw std::out_of_range("Key not found");
            }
            if (preferNew)
            {
                int r = tryGetChain(newRoot, &out);
                if (r == 1)
                    return out;
                if (r == 0)
                    throw std::out_of_range("Key not found");
                r = tryGetChain(oldRoot, &out);
                if (r == 1)
                    return out;
                throw std::out_of_range("Key not found");
            }
            else
            {
                int r = tryGetChain(oldRoot, &out);
                if (r == 1)
                    return out;
                if (r == 0)
                {
                    // tombstoned old; maybe resurrected new
                    r = tryGetChain(newRoot, &out);
                    if (r == 1)
                        return out;
                    throw std::out_of_range("Key not found");
                }
                r = tryGetChain(newRoot, &out);
                if (r == 1)
                    return out;
                throw std::out_of_range("Key not found");
            }
        }

        // Non-throwing retrieval; returns true on success, false if absent or tombstoned.
        bool TryGet(const Key& key, Value& out) const
        {
            NGIN::Memory::EpochReclaimer::Guard epochGuard;
            const size_t                        h         = hasher(key);
            auto                                tv        = LoadTable();
            const size_t                        idx       = h & (tv.len - 1);
            auto*                               buckets   = tv.buckets;
            ResizeState*                        rs        = resizeState.load(std::memory_order_acquire);
            bool                                preferNew = false;
            bool                                needDual  = NeedDualLookup(idx, tv, rs, preferNew);
            VirtualBucket*                      oldRoot   = buckets[idx].load(std::memory_order_acquire);
            VirtualBucket*                      newRoot   = needDual ? rs->new_table[h & (rs->new_len - 1)].load(std::memory_order_acquire) : nullptr;

            auto tryGetChain = [&](VirtualBucket* root) -> int {
                for (VirtualBucket* vb = root; vb; vb = vb->next.load(std::memory_order_acquire))
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
                                    return 1;// success
                                }
                                return 0;// tombstone
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
                                    return 1;
                                }
                                return 0;
                            }
                        }
                    }
                }
                return -1;// not found
            };

            if (!needDual)
            {
                return tryGetChain(oldRoot) == 1;
            }
            if (preferNew)
            {
                int r = tryGetChain(newRoot);
                if (r == 1)
                    return true;
                if (r == 0)
                    return false;
                r = tryGetChain(oldRoot);
                return r == 1;
            }
            else
            {
                int r = tryGetChain(oldRoot);
                if (r == 1)
                    return true;
                if (r == 0)
                {
                    r = tryGetChain(newRoot);
                    return r == 1;
                }
                r = tryGetChain(newRoot);
                return r == 1;
            }
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
            NGIN::Memory::EpochReclaimer::Guard epochGuard;
            HelpResize();
            auto* hdr     = tableHeader.load(std::memory_order_acquire);
            auto* buckets = hdr->buckets;
            for (std::size_t i = 0; i < hdr->len; ++i)
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
                                bp.data->v.drop();
                            }
                        }
                        else
                        {
                            BucketData* ptr = s.load(std::memory_order_acquire);
                            if (ptr)
                            {
                                ptr->v.drop();
                            }
                        }
                    }
                    vb = vb->next.load(std::memory_order_acquire);
                }
            }
            // NOTE: Clear currently assumes external quiescence (no concurrent inserts/removes)
            // for accurate size_ reset; concurrent mutators may skew the count.
            size_.store(0, std::memory_order_relaxed);
        }

        size_t Size() const
        {
            return size_.load();
        }
    };

}// namespace NGIN::Containers
