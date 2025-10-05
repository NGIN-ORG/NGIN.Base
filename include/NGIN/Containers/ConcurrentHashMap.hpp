/// @file ConcurrentHashMap.hpp
/// @brief Concurrent hash map with cooperative resizing and cache-friendly bucket groups.

#pragma once

#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

// Optimizations (optimistic reads, spin backoff, adaptive threshold) are always enabled.

namespace NGIN::Containers
{
    namespace detail
    {
        enum class SlotState : std::uint8_t
        {
            Empty = 0,
            PendingInsert,
            Occupied,
            Tombstone,
        };

        constexpr bool IsPowerOfTwo(std::size_t value) noexcept
        {
            return value && ((value & (value - 1)) == 0);
        }

        constexpr std::size_t ConstLog2(std::size_t value) noexcept
        {
            std::size_t shift = 0;
            while ((static_cast<std::size_t>(1) << shift) < value)
            {
                ++shift;
            }
            return shift;
        }

        template<class Key, class Value>
        struct SlotStorage
        {
            std::atomic<std::uint8_t> control {static_cast<std::uint8_t>(SlotState::Empty)};
            std::size_t               hash {0};
            alignas(Key) unsigned char keyStorage[sizeof(Key)] {};
            alignas(Value) unsigned char valueStorage[sizeof(Value)] {};

            constexpr SlotStorage() noexcept = default;

            [[nodiscard]] SlotState State(std::memory_order order = std::memory_order_acquire) const noexcept
            {
                return static_cast<SlotState>(control.load(order));
            }

            void Reset() noexcept
            {
                hash = 0;
                control.store(static_cast<std::uint8_t>(SlotState::Empty), std::memory_order_release);
            }

            bool TryLockFrom(SlotState expected) noexcept
            {
                auto expectedRaw = static_cast<std::uint8_t>(expected);
                return control.compare_exchange_strong(
                        expectedRaw,
                        static_cast<std::uint8_t>(SlotState::PendingInsert),
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed);
            }

            void UnlockTo(SlotState state) noexcept
            {
                control.store(static_cast<std::uint8_t>(state), std::memory_order_release);
            }

            template<class K, class V>
            void ConstructPayload(std::size_t h, K&& key, V&& value)
            {
                hash = h;
                try
                {
                    auto* keyPtr = ::new (static_cast<void*>(keyStorage)) Key(std::forward<K>(key));
                    try
                    {
                        ::new (static_cast<void*>(valueStorage)) Value(std::forward<V>(value));
                    } catch (...)
                    {
                        keyPtr->~Key();
                        hash = 0;
                        throw;
                    }
                } catch (...)
                {
                    hash = 0;
                    throw;
                }
            }

            void DestroyPayload() noexcept
            {
                ValueRef().~Value();
                KeyRef().~Key();
                hash = 0;
            }

            template<class V>
            void AssignValue(V&& value)
            {
                ValueRef() = std::forward<V>(value);
            }

            [[nodiscard]] Key& KeyRef() noexcept
            {
                return *std::launder(reinterpret_cast<Key*>(keyStorage));
            }

            [[nodiscard]] const Key& KeyRef() const noexcept
            {
                return *std::launder(reinterpret_cast<const Key*>(keyStorage));
            }

            [[nodiscard]] Value& ValueRef() noexcept
            {
                return *std::launder(reinterpret_cast<Value*>(valueStorage));
            }

            [[nodiscard]] const Value& ValueRef() const noexcept
            {
                return *std::launder(reinterpret_cast<const Value*>(valueStorage));
            }
        };

        template<class Key, class Value, std::size_t GroupSize>
        struct alignas(64) BucketGroup
        {
            SlotStorage<Key, Value> slots[GroupSize];

            constexpr BucketGroup() noexcept
            {
                Reset();
            }

            void Reset() noexcept
            {
                for (auto& slot: slots)
                    slot.Reset();
            }
        };
    }// namespace detail

    /// @brief Concurrent hash map leveraging open addressing and cooperative resizing.
    template<class Key,
             class Value,
             class Hash                         = std::hash<Key>,
             class Equal                        = std::equal_to<Key>,
             Memory::AllocatorConcept Allocator = Memory::SystemAllocator,
             std::size_t              GroupSize = 16>
    class ConcurrentHashMap
    {
        static_assert(detail::IsPowerOfTwo(GroupSize), "GroupSize must be a power of two.");

    public:
        using key_type                         = Key;
        using mapped_type                      = Value;
        using hash_type                        = Hash;
        using key_equal                        = Equal;
        using allocator_type                   = Allocator;
        using size_type                        = std::size_t;
        using value_type                       = std::pair<const Key, Value>;
        static constexpr size_type kGroupSize  = GroupSize;
        static constexpr double    kLoadFactor = 0.75;

        ConcurrentHashMap() : ConcurrentHashMap(64) {}

        explicit ConcurrentHashMap(size_type        initialCapacity,
                                   const Hash&      hash      = Hash {},
                                   const Equal&     equal     = Equal {},
                                   const Allocator& allocator = Allocator {})
            : m_hash(hash), m_equal(equal), m_allocator(allocator)
        {
            Initialize(initialCapacity);
        }

        ConcurrentHashMap(const ConcurrentHashMap&)            = delete;
        ConcurrentHashMap& operator=(const ConcurrentHashMap&) = delete;

        ConcurrentHashMap(ConcurrentHashMap&& other) noexcept
            : m_hash(std::move(other.m_hash)),
              m_equal(std::move(other.m_equal)),
              m_allocator(std::move(other.m_allocator))
        {
            other.DrainMigration();
            other.FlushAllShards();
            m_table    = other.m_table;
            m_capacity = other.m_capacity;
            m_mask     = other.m_mask;
            m_size.store(other.m_size.load(std::memory_order_relaxed), std::memory_order_relaxed);
            m_resizeThreshold = other.m_resizeThreshold;
            m_pubGroups.store(m_table.groups, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            m_pubMask.store(m_mask, std::memory_order_release);
            other.m_table           = {};
            other.m_capacity        = 0;
            other.m_mask            = 0;
            other.m_resizeThreshold = 0;
            other.m_size.store(0, std::memory_order_relaxed);
            for (auto& shard: other.m_sizeShards)
                shard.delta.store(0, std::memory_order_relaxed);
        }

        ConcurrentHashMap& operator=(ConcurrentHashMap&& other) noexcept
        {
            if (this != &other)
            {
                DrainMigration();
                DestroyTable();

                m_hash      = std::move(other.m_hash);
                m_equal     = std::move(other.m_equal);
                m_allocator = std::move(other.m_allocator);

                other.DrainMigration();
                other.FlushAllShards();
                m_table    = other.m_table;
                m_capacity = other.m_capacity;
                m_mask     = other.m_mask;
                m_size.store(other.m_size.load(std::memory_order_relaxed), std::memory_order_relaxed);
                m_resizeThreshold = other.m_resizeThreshold;
                m_pubGroups.store(m_table.groups, std::memory_order_relaxed);
                std::atomic_thread_fence(std::memory_order_release);
                m_pubMask.store(m_mask, std::memory_order_release);

                other.m_table           = {};
                other.m_capacity        = 0;
                other.m_mask            = 0;
                other.m_resizeThreshold = 0;
                other.m_size.store(0, std::memory_order_relaxed);
                for (auto& shard: other.m_sizeShards)
                    shard.delta.store(0, std::memory_order_relaxed);
            }
            return *this;
        }

        ~ConcurrentHashMap()
        {
            DrainMigration();
            DestroyTable();
        }

        [[nodiscard]] size_type Size() const noexcept
        {
            return ApproxSize();
        }

        [[nodiscard]] bool Empty() const noexcept
        {
            return Size() == 0;
        }

        // Quiesce: drain all in-flight migrations so subsequent queries observe a stable table.
        // Safe to call concurrently; may increase latency under contention.
        void Quiesce() noexcept
        {
            DrainMigration();
            // If last readers just dropped during DrainMigration, a second reclaim may be needed.
            ReclaimRetired();
        }

        void Clear() noexcept
        {
            // Acquire a guarded view so groups memory stays alive while clearing.
            ViewToken tok = AcquireGuardedView();
            if (!tok.view.groups)
            {
                ReleaseGuard(tok, false);
                return;
            }
            const size_type cap = tok.view.mask + 1;
            for (size_type i = 0; i < cap; ++i)
            {
                auto& slot  = SlotAt(tok.view, i);
                auto  state = slot.State(std::memory_order_acquire);
                if (state == detail::SlotState::Occupied)
                    slot.DestroyPayload();
                slot.Reset();
            }
            m_size.store(0, std::memory_order_release);
            for (auto& shard: m_sizeShards)
                shard.delta.store(0, std::memory_order_release);
            ReleaseGuard(tok, false);
        }

        bool Insert(const Key& key, const Value& value)
        {
            return EmplaceOrAssignInternal(key, value);
        }

        bool Insert(const Key& key, Value&& value)
        {
            return EmplaceOrAssignInternal(key, std::move(value));
        }

        bool Insert(Key&& key, const Value& value)
        {
            return EmplaceOrAssignInternal(std::move(key), value);
        }

        bool Insert(Key&& key, Value&& value)
        {
            return EmplaceOrAssignInternal(std::move(key), std::move(value));
        }

        template<class Updater>
        bool Upsert(const Key& key, Value&& value, Updater&& updater)
        {
            return UpsertInternal(key, std::move(value), std::forward<Updater>(updater));
        }

        template<class Updater>
        bool Upsert(Key&& key, Value&& value, Updater&& updater)
        {
            return UpsertInternal(std::move(key), std::move(value), std::forward<Updater>(updater));
        }

        bool Remove(const Key& key)
        {
            const std::size_t hash      = ComputeHash(key);
            ViewToken         token     = AcquireGuardedView();
            TableView         primary   = token.view;
            bool              finalized = false;

            bool removed = false;

            // Attempt in primary (current published) table first
            if (auto* primarySlot = AcquireForMutation(primary, key, hash))
            {
                primarySlot->DestroyPayload();
                primarySlot->UnlockTo(detail::SlotState::Tombstone);
                DecrementSizeShard();
                removed = true;
                if (token.mig)
                {
                    RemoveFromOld(token.mig, key, hash);
                    HelpMigration(token.mig);
                    finalized = FinalizeMigration(token.mig);
                }
                else
                {
                    MaybeStartMigration();
                }
            }
            else if (token.mig)
            {
                TableView oldView {token.mig->oldGroups, token.mig->oldMask};
                if (auto* secondarySlot = AcquireForMutation(oldView, key, hash))
                {
                    secondarySlot->DestroyPayload();
                    secondarySlot->UnlockTo(detail::SlotState::Tombstone);
                    DecrementSizeShard();
                    removed = true;
                    HelpMigration(token.mig);
                    finalized = FinalizeMigration(token.mig);
                }
            }
            ReleaseGuard(token, finalized);
            return removed;
        }

        [[nodiscard]] bool Contains(const Key& key) const noexcept
        {
            const std::size_t hash  = ComputeHash(key);
            ViewToken         token = const_cast<ConcurrentHashMap*>(this)->AcquireGuardedView();
            if (ContainsInView(token.view, key, hash))
            {
                const_cast<ConcurrentHashMap*>(this)->ReleaseGuard(token, false);
                return true;
            }
            bool found = false;
            if (token.mig)
            {
                TableView oldView {token.mig->oldGroups, token.mig->oldMask};
                found = ContainsInView(oldView, key, hash);
            }
            const_cast<ConcurrentHashMap*>(this)->ReleaseGuard(token, false);
            return found;
        }

        Value Get(const Key& key) const
        {
            Value result {};
            if (TryGet(key, result))
                return result;
            throw std::out_of_range("ConcurrentHashMap::Get - key not found");
        }

        bool TryGet(const Key& key, Value& outValue) const
        {
            const std::size_t hash = ComputeHash(key);
            // Fast optimistic path: snapshot epoch; if even and no migration pointer, read without guard.
            const size_type startEpoch = m_epoch.load(std::memory_order_acquire);
            if ((startEpoch & 1u) == 0)
            {
                TableView snap = CurrentView();
                if (snap.groups && m_migration.load(std::memory_order_acquire) == nullptr)
                {
                    if (TryCopyValueInView(snap, key, hash, outValue))
                    {
                        const size_type endEpoch = m_epoch.load(std::memory_order_acquire);
                        if (endEpoch == startEpoch && (endEpoch & 1u) == 0)
                            return true;
                    }
                    else
                    {
                        const size_type endEpoch = m_epoch.load(std::memory_order_acquire);
                        if (endEpoch == startEpoch && (endEpoch & 1u) == 0)
                            return false;
                    }
                }
            }
            ViewToken token = const_cast<ConcurrentHashMap*>(this)->AcquireGuardedView();
            if (TryCopyValueInView(token.view, key, hash, outValue))
            {
                const_cast<ConcurrentHashMap*>(this)->ReleaseGuard(token, false);
                return true;
            }
            bool result = false;
            if (token.mig)
            {
                TableView oldView {token.mig->oldGroups, token.mig->oldMask};
                result = TryCopyValueInView(oldView, key, hash, outValue);
            }
            const_cast<ConcurrentHashMap*>(this)->ReleaseGuard(token, false);
            return result;
        }

        [[nodiscard]] std::optional<Value> GetOptional(const Key& key) const
        {
            Value storage;
            if (TryGet(key, storage))
                return storage;
            return std::nullopt;
        }

        void Reserve(size_type desiredCapacity)
        {
            if (desiredCapacity <= m_capacity)
                return;
            StartMigration(desiredCapacity);
            MigrationState* migration = AcquireMigration();
            if (migration)
            {
                HelpMigration(migration, migration->oldGroupCount);
                const bool finalized = FinalizeMigration(migration);
                ReleaseAndMaybeDestroy(migration, finalized);
            }
        }

        [[nodiscard]] size_type Capacity() const noexcept
        {
            return m_capacity;
        }

        [[nodiscard]] double LoadFactor() const noexcept
        {
            if (m_capacity == 0)
                return 0.0;
            return static_cast<double>(Size()) / static_cast<double>(m_capacity);
        }

        template<class Callback>
        void ForEach(Callback&& callback) const
        {
            // Use guarded view; iteration sees a stable table (may miss concurrent inserts to a new resize in progress).
            auto&           self = *const_cast<ConcurrentHashMap*>(this);
            ViewToken       tok  = self.AcquireGuardedView();
            const size_type cap  = tok.view.mask ? (tok.view.mask + 1) : 0;
            for (size_type i = 0; i < cap; ++i)
            {
                const auto& slot = SlotAt(tok.view, i);
                if (slot.State(std::memory_order_acquire) == detail::SlotState::Occupied)
                    callback(slot.KeyRef(), slot.ValueRef());
            }
            self.ReleaseGuard(tok, false);
        }

    private:
        using Slot  = detail::SlotStorage<Key, Value>;
        using Group = detail::BucketGroup<Key, Value, GroupSize>;

        struct Table
        {
            Group*    groups {nullptr};
            size_type groupCount {0};
        };

        struct TableView
        {
            Group*    groups {nullptr};
            size_type mask {0};
        };

        struct MigrationState
        {
            Group*                 oldGroups {nullptr};
            size_type              oldGroupCount {0};
            size_type              oldMask {0};
            Group*                 newGroups {nullptr};
            size_type              newGroupCount {0};
            size_type              newMask {0};
            size_type              newThreshold {0};
            std::atomic<size_type> nextGroup {0};
            std::atomic<size_type> migratedGroups {0};
            std::atomic<size_type> activeUsers {0};
            std::atomic<bool>      finalized {false};
            std::atomic<bool>      destroyed {false};
            MigrationState*        nextRetired {nullptr};// intrusive singly-linked list for deferred reclamation
        };

        Table     m_table {};
        size_type m_capacity {0};
        size_type m_mask {0};
        // Global committed size (flushed deltas only). Sharded deltas amortize contention.
        std::atomic<size_type> m_size {0};
        struct SizeShard
        {
            std::atomic<std::intptr_t> delta {0};
        };
        static constexpr size_type kSizeShardCount = 64;// power of two, tune if needed
        // Default flush threshold (can be tuned at runtime via SetFlushThreshold).
        static constexpr size_type kDefaultFlushThreshold = 32;
        std::atomic<size_type>     m_flushThreshold {kDefaultFlushThreshold};
        alignas(64) SizeShard m_sizeShards[kSizeShardCount] {};
        size_type m_resizeThreshold {0};
        Hash      m_hash {};
        Equal     m_equal {};
        Allocator m_allocator {};
        // Atomically published groups pointer and mask for readers to obtain a stable snapshot.
        std::atomic<Group*>            m_pubGroups {nullptr};
        std::atomic<size_type>         m_pubMask {0};
        mutable std::atomic<size_type> m_readers {0};
        std::atomic<MigrationState*>   m_migration {nullptr};
        std::atomic<MigrationState*>   m_retired {nullptr};// list of finalized states awaiting reader drain
                                                           // Epoch (even=stable, odd=migration) for optimistic reads.
        std::atomic<size_type> m_epoch {0};

        // Fixed load factor (adaptive logic removed): threshold uses kLoadFactor.

        static constexpr size_type kGroupMask  = GroupSize - 1;
        static constexpr size_type kGroupShift = detail::ConstLog2(GroupSize);

        struct InsertProbe
        {
            Slot*             slot {nullptr};
            detail::SlotState previousState {detail::SlotState::Empty};
            bool              isNew {false};
        };

        // --- Phase 3: Sharded size accounting helpers ---
        [[nodiscard]] size_type GetShardIndex() const noexcept
        {
            // Thread-local cached index; lazily initialize.
            static thread_local size_type shardIndex = kSizeShardCount;// sentinel
            if (shardIndex >= kSizeShardCount)
            {
                // Derive pseudo-random stable index from thread id pointer hash.
                auto tid   = std::hash<std::thread::id>()(std::this_thread::get_id());
                shardIndex = static_cast<size_type>(tid) & (kSizeShardCount - 1);
            }
            return shardIndex;
        }

        void FlushShard(size_type shardIndex) noexcept
        {
            auto&         shard = m_sizeShards[shardIndex];
            std::intptr_t delta = shard.delta.exchange(0, std::memory_order_acq_rel);
            if (delta != 0)
            {
                if (delta > 0)
                    m_size.fetch_add(static_cast<size_type>(delta), std::memory_order_acq_rel);
                else
                    m_size.fetch_sub(static_cast<size_type>(-delta), std::memory_order_acq_rel);
            }
        }

        void FlushAllShards() noexcept
        {
            for (size_type i = 0; i < kSizeShardCount; ++i)
                FlushShard(i);
        }

        void IncrementSizeShard() noexcept
        {
            const size_type shardIndex = GetShardIndex();
            auto&           shard      = m_sizeShards[shardIndex];
            auto            newDelta   = shard.delta.fetch_add(1, std::memory_order_relaxed) + 1;
            const auto      limit      = static_cast<std::intptr_t>(m_flushThreshold.load(std::memory_order_relaxed));
            if (newDelta >= limit)
            {
                FlushShard(shardIndex);
            }
        }

        void DecrementSizeShard() noexcept
        {
            const size_type shardIndex = GetShardIndex();
            auto&           shard      = m_sizeShards[shardIndex];
            auto            newDelta   = shard.delta.fetch_sub(1, std::memory_order_relaxed) - 1;
            const auto      limit      = static_cast<std::intptr_t>(m_flushThreshold.load(std::memory_order_relaxed));
            if (newDelta <= -limit)
            {
                FlushShard(shardIndex);
            }
        }

        void SetFlushThreshold(size_type newThreshold) noexcept
        {
            if (newThreshold == 0)
                newThreshold = 1;// avoid zero which would cause constant flushing
            m_flushThreshold.store(newThreshold, std::memory_order_release);
        }

        [[nodiscard]] size_type ApproxSize() const noexcept
        {
            // Signed accumulation to avoid temporary underflow when negative shard deltas exceed committed base.
            std::int64_t total = static_cast<std::int64_t>(m_size.load(std::memory_order_acquire));
            for (size_type i = 0; i < kSizeShardCount; ++i)
            {
                total += m_sizeShards[i].delta.load(std::memory_order_acquire);
            }
            if (total < 0)
                total = 0;
            return static_cast<size_type>(total);
        }

        void Initialize(size_type initialCapacity)
        {
            DrainMigration();
            initialCapacity      = std::max<size_type>(GroupSize, std::bit_ceil(initialCapacity));
            size_type groupCount = initialCapacity / GroupSize;
            Group*    groups     = AllocateGroups(groupCount);

            m_table.groups     = groups;
            m_table.groupCount = groupCount;
            m_capacity         = groupCount * GroupSize;
            m_mask             = m_capacity - 1;
            m_resizeThreshold  = ComputeThreshold(m_capacity);
            m_size.store(0, std::memory_order_relaxed);
            for (auto& shard: m_sizeShards)
                shard.delta.store(0, std::memory_order_relaxed);
            // Publish initial table view: groups first, then mask.
            // Rationale: reader will load mask (acquire) then groups; seeing the new mask implies
            // visibility of the preceding groups store (via release on mask) preventing torn snapshot.
            m_pubGroups.store(m_table.groups, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            m_pubMask.store(m_mask, std::memory_order_release);
        }

        [[nodiscard]] bool ViewValid(const TableView& v) const noexcept
        {
            if (!v.groups)
                return v.mask == 0;// empty table case
            // (mask + 1) must be multiple of GroupSize.
            const size_type span = v.mask + 1;
            return (span & (GroupSize - 1)) == 0;// power-of-two capacity aligned to group size
        }

        Group* AllocateGroups(size_type groupCount)
        {
            if (groupCount == 0)
                return nullptr;
            const size_type bytes = sizeof(Group) * groupCount;
            auto*           raw   = static_cast<Group*>(m_allocator.Allocate(bytes, alignof(Group)));
            if (!raw)
                throw std::bad_alloc();
            for (size_type i = 0; i < groupCount; ++i)
            {
                ::new (static_cast<void*>(raw + i)) Group();
            }
            return raw;
        }

        void ReleaseGroups(Group* groups, size_type groupCount) noexcept
        {
            if (!groups)
                return;
            for (size_type i = 0; i < groupCount; ++i)
            {
                groups[i].~Group();
            }
            const size_type bytes = sizeof(Group) * groupCount;
            m_allocator.Deallocate(static_cast<void*>(groups), bytes, alignof(Group));
        }

        void DestroyTable() noexcept
        {
            if (!m_table.groups)
                return;

            TableView view = CurrentView();
            for (size_type index = 0; index < m_capacity; ++index)
            {
                auto& slot = SlotAt(view, index);
                if (slot.State(std::memory_order_acquire) == detail::SlotState::Occupied)
                {
                    slot.DestroyPayload();
                    slot.UnlockTo(detail::SlotState::Empty);
                }
            }

            ReleaseGroups(m_table.groups, m_table.groupCount);
            m_table.groups     = nullptr;
            m_table.groupCount = 0;
            m_capacity         = 0;
            m_mask             = 0;
            m_resizeThreshold  = 0;
            m_size.store(0, std::memory_order_relaxed);
            for (auto& shard: m_sizeShards)
                shard.delta.store(0, std::memory_order_relaxed);
            // Publish empty view (groups=null then mask=0) preserving ordering invariant.
            m_pubGroups.store(nullptr, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            m_pubMask.store(0, std::memory_order_release);
        }

        [[nodiscard]] TableView CurrentView() const noexcept
        {
            // Load mask first (acquire) then groups. If we observe a new mask value we are guaranteed
            // (by release store on mask) to also observe the earlier groups pointer store.
            size_type m = m_pubMask.load(std::memory_order_acquire);
            Group*    g = m_pubGroups.load(std::memory_order_acquire);
            // Invariant: either empty (g==nullptr, m==0) or both valid.
#if defined(NGIN_DEBUG) || !defined(NDEBUG)
            if ((g == nullptr) != (m == 0))
            {
                // Mask/groups publication invariant violated; force fail fast in debug.
                std::terminate();
            }
#endif
            return TableView {g, m};
        }
        struct ViewToken
        {
            TableView       view {};
            MigrationState* mig {nullptr};
            bool            usesReaderGuard {false};
        };

        ViewToken AcquireGuardedView() noexcept
        {
            // Register as passive reader first.
            m_readers.fetch_add(1, std::memory_order_acquire);
            for (;;)
            {
                // Fast path: try to pin an active migration; if successful, build view from migration state.
                if (MigrationState* s = AcquireMigration())
                {
                    // Avoid using potentially torn published (groups, mask) pair.
                    m_readers.fetch_sub(1, std::memory_order_release);
#if defined(NGIN_DEBUG) || !defined(NDEBUG)
                    if ((s->newGroups == nullptr) || (s->newMask == 0))
                    {
                        std::terminate();// new table must be non-empty when migration exists
                    }
#endif
                    return ViewToken {TableView {s->newGroups, s->newMask}, s, false};
                }
                // No migration active. Snapshot published view.
                TableView snap = CurrentView();
                // Re-check that migration did not start after snapshot.
                if (m_migration.load(std::memory_order_acquire) == nullptr)
                {
#if defined(NGIN_DEBUG) || !defined(NDEBUG)
                    if ((snap.groups == nullptr) != (snap.mask == 0))
                    {
                        std::terminate();
                    }
#endif
                    return ViewToken {snap, nullptr, true};
                }
                // Migration raced on: convert passive guard into active attempt by restarting.
                m_readers.fetch_sub(1, std::memory_order_release);
                m_readers.fetch_add(1, std::memory_order_acquire);
            }
        }

        void ReleaseGuard(ViewToken& tok, bool finalized = false) noexcept
        {
            if (tok.usesReaderGuard)
            {
                const size_type prev = m_readers.fetch_sub(1, std::memory_order_acq_rel);
                // If we were the last passive reader, attempt to reclaim any retired states.
                if (prev == 1)
                {
                    ReclaimRetired();
                }
            }
            else if (tok.mig)
            {
                ReleaseAndMaybeDestroy(tok.mig, finalized);
            }
            tok.mig             = nullptr;
            tok.usesReaderGuard = false;
        }

        static Slot& SlotAt(const TableView& view, size_type index) noexcept
        {
#if defined(NGIN_DEBUG) || !defined(NDEBUG)
            if (!view.groups)
            {
                std::terminate();
            }
#endif
            return view.groups[index >> kGroupShift].slots[index & kGroupMask];
        }

        [[nodiscard]] size_type ComputeThreshold(size_type capacity) const noexcept
        {
            if (capacity <= 1)
                return 1;
            const double    raw       = static_cast<double>(capacity) * kLoadFactor;
            const auto      threshold = static_cast<size_type>(raw);
            const size_type capped    = threshold >= capacity ? capacity - 1 : threshold;
            return capped == 0 ? 1 : capped;
        }

        void MaybeStartMigration(size_type additional = 0)
        {
            if (m_migration.load(std::memory_order_acquire))
                return;
            const size_type projected = ApproxSize() + additional;
            if (projected <= m_resizeThreshold)
                return;
            size_type       target     = m_capacity ? m_capacity * 2 : GroupSize;
            const size_type scaledBase = projected + (projected / 3) + GroupSize;
            const size_type scaled     = std::bit_ceil(std::max<size_type>(GroupSize, scaledBase));
            target                     = std::max(target, scaled);
            StartMigration(target);
        }

        MigrationState* AcquireMigration() const noexcept
        {
            while (true)
            {
                MigrationState* state = m_migration.load(std::memory_order_acquire);
                if (!state)
                    return nullptr;
                state->activeUsers.fetch_add(1, std::memory_order_acquire);
                if (state == m_migration.load(std::memory_order_acquire))
                    return state;
                state->activeUsers.fetch_sub(1, std::memory_order_acq_rel);
            }
        }

        void ReleaseMigration(MigrationState* state) const noexcept
        {
            if (state)
                state->activeUsers.fetch_sub(1, std::memory_order_acq_rel);
        }

        void DestroyMigrationState(MigrationState* state) noexcept
        {
            if (!state)
                return;
            // Defer if either active users or passive readers still present.
            if (state->activeUsers.load(std::memory_order_acquire) != 0 ||
                m_readers.load(std::memory_order_acquire) != 0)
            {
                MigrationState* head = m_retired.load(std::memory_order_relaxed);
                do
                {
                    state->nextRetired = head;
                } while (!m_retired.compare_exchange_weak(
                        head,
                        state,
                        std::memory_order_release,
                        std::memory_order_relaxed));
                return;
            }
            ReleaseGroups(state->oldGroups, state->oldGroupCount);
            state->oldGroups     = nullptr;
            state->oldGroupCount = 0;
            delete state;
        }

        void ReclaimRetired() noexcept
        {
            MigrationState* list     = m_retired.exchange(nullptr, std::memory_order_acq_rel);
            MigrationState* deferred = nullptr;
            while (list)
            {
                MigrationState* next = list->nextRetired;
                // Reclaim only when no active users AND no readers remain.
                if (list->activeUsers.load(std::memory_order_acquire) == 0 &&
                    m_readers.load(std::memory_order_acquire) == 0)
                {
                    ReleaseGroups(list->oldGroups, list->oldGroupCount);
                    list->oldGroups     = nullptr;
                    list->oldGroupCount = 0;
                    delete list;
                }
                else
                {
                    list->nextRetired = deferred;
                    deferred          = list;
                }
                list = next;
            }
            if (deferred)
            {
                // Push remaining back onto retired list head.
                MigrationState* head = m_retired.load(std::memory_order_relaxed);
                do
                {
                    // Append existing head to end of deferred chain.
                    MigrationState* tail = deferred;
                    while (tail->nextRetired)
                        tail = tail->nextRetired;
                    tail->nextRetired = head;
                } while (!m_retired.compare_exchange_weak(
                        head,
                        deferred,
                        std::memory_order_release,
                        std::memory_order_relaxed));
            }
        }

        void ReleaseAndMaybeDestroy(MigrationState* state, bool finalized) noexcept
        {
            if (!state)
                return;
            const bool alreadyFinalized = state->finalized.load(std::memory_order_acquire);
            ReleaseMigration(state);
            if (finalized)
            {
                if (!state->destroyed.exchange(true, std::memory_order_acq_rel))
                {
                    // Poison immediately; either immediate destruction or queued.
                    state->oldMask = 0;
                    DestroyMigrationState(state);
                }
                return;
            }
            if (alreadyFinalized)
            {
                if (!state->destroyed.exchange(true, std::memory_order_acq_rel))
                {
                    state->oldMask = 0;
                    DestroyMigrationState(state);
                }
            }
        }

        void StartMigration(size_type desiredCapacity)
        {
            // Use approximate size first (no full flush) to decide if a migration attempt is worthwhile.
            const size_type approxSize = ApproxSize();

            // Compute provisional target capacity using approx size; may grow again after precise flush.
            const size_type currentSize = approxSize;

            const size_type surgeBase = std::max<size_type>(GroupSize, currentSize / 2 + GroupSize);
            size_type       demand    = currentSize + surgeBase;
            if (demand < currentSize)// overflow check
                demand = std::numeric_limits<size_type>::max();

            size_type requiredCapacity;
            if (demand > (std::numeric_limits<size_type>::max() - 2) / 4)
            {
                requiredCapacity = std::numeric_limits<size_type>::max();
            }
            else
            {
                requiredCapacity = (demand * 4 + 2) / 3;// ceil(demand / 0.75)
            }

            size_type minimumCapacity = std::max(desiredCapacity, requiredCapacity);
            if (m_capacity)
            {
                const size_type growthBuffer = std::max<size_type>(m_capacity / 2, GroupSize);
                if (m_capacity > std::numeric_limits<size_type>::max() - growthBuffer)
                    minimumCapacity = std::numeric_limits<size_type>::max();
                else
                    minimumCapacity = std::max(minimumCapacity, m_capacity + growthBuffer);
            }
            else
            {
                minimumCapacity = std::max(minimumCapacity, GroupSize * 2);
            }

            minimumCapacity = std::max(minimumCapacity, static_cast<size_type>(GroupSize));

            const size_type maxPowerOfTwo = size_type {1} << (std::numeric_limits<size_type>::digits - 1);
            size_type       targetCapacity;
            if (minimumCapacity > maxPowerOfTwo)
            {
                targetCapacity = maxPowerOfTwo;
            }
            else
            {
                targetCapacity = std::bit_ceil(minimumCapacity);
            }

            size_type newGroupCount = targetCapacity / GroupSize;
            Group*    newGroups     = AllocateGroups(newGroupCount);

            auto* state          = new MigrationState();
            state->oldGroups     = m_table.groups;
            state->oldGroupCount = m_table.groupCount;
            state->oldMask       = m_mask;
            state->newGroups     = newGroups;
            state->newGroupCount = newGroupCount;
            state->newMask       = targetCapacity - 1;
            state->newThreshold  = ComputeThreshold(targetCapacity);
            state->nextGroup.store(0, std::memory_order_relaxed);
            state->migratedGroups.store(0, std::memory_order_relaxed);
            state->activeUsers.store(1, std::memory_order_relaxed);
            state->finalized.store(false, std::memory_order_relaxed);
            state->destroyed.store(false, std::memory_order_relaxed);

            MigrationState* expected = nullptr;
            if (m_migration.compare_exchange_strong(
                        expected,
                        state,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed))
            {
                // Winner: flush shards now to capture precise size for potential future growth heuristics.
                FlushAllShards();
                // Flip epoch to odd to signal migration in progress (only if currently even)
                size_type e = m_epoch.load(std::memory_order_relaxed);
                while ((e & 1u) == 0 && !m_epoch.compare_exchange_weak(e, e | 1u, std::memory_order_acq_rel)) {}
                m_table.groups     = newGroups;
                m_table.groupCount = newGroupCount;
                m_capacity         = targetCapacity;
                m_mask             = state->newMask;
                m_resizeThreshold  = state->newThreshold;
                // Publish new table view (groups then mask) with release ordering.
                m_pubGroups.store(m_table.groups, std::memory_order_relaxed);
                std::atomic_thread_fence(std::memory_order_release);
                m_pubMask.store(m_mask, std::memory_order_release);
                HelpMigration(state, 1);
                const bool finalized = FinalizeMigration(state);
                ReleaseMigration(state);
                if (finalized && !state->destroyed.exchange(true, std::memory_order_acq_rel))
                    DestroyMigrationState(state);
            }
            else
            {
                state->activeUsers.store(0, std::memory_order_relaxed);
                ReleaseGroups(newGroups, newGroupCount);
                delete state;
            }
        }

        void DrainMigration() noexcept
        {
            while (true)
            {
                MigrationState* migration = AcquireMigration();
                if (!migration)
                    break;
                HelpMigration(migration, migration->oldGroupCount);
                const bool finalized = FinalizeMigration(migration);
                ReleaseAndMaybeDestroy(migration, finalized);
            }
        }

        void HelpMigration(MigrationState* state, size_type budget = 1) noexcept
        {
            if (!state)
                return;
            const size_type totalGroups = state->oldGroupCount;
            size_type       processed   = 0;
            while (processed < budget)
            {
                const size_type groupIndex = state->nextGroup.fetch_add(1, std::memory_order_acq_rel);
                if (groupIndex >= totalGroups)
                    break;
                MigrateGroup(state, groupIndex);
                state->migratedGroups.fetch_add(1, std::memory_order_acq_rel);
                ++processed;
            }
        }

        bool FinalizeMigration(MigrationState* state) noexcept
        {
            if (!state)
                return false;
            if (state->migratedGroups.load(std::memory_order_acquire) < state->oldGroupCount)
                return false;

            MigrationState* expected = state;
            if (!m_migration.compare_exchange_strong(
                        expected,
                        nullptr,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed))
            {
                return false;
            }
            state->finalized.store(true, std::memory_order_release);
            // Return epoch to even if currently odd.
            size_type cur = m_epoch.load(std::memory_order_relaxed);
            if (cur & 1u)
                m_epoch.store(cur + 1, std::memory_order_release);
            return true;
        }

        void MigrateGroup(MigrationState* state, size_type groupIndex) noexcept
        {
            if (!state)
                return;
            TableView       oldView {state->oldGroups, state->oldMask};
            const size_type base = groupIndex << kGroupShift;
            bool            pendingWork;
            do
            {
                pendingWork = false;
                for (size_type offset = 0; offset < GroupSize; ++offset)
                {
                    const size_type index = (base + offset) & state->oldMask;
                    auto&           slot  = SlotAt(oldView, index);

                    while (true)
                    {
                        const auto slotState = slot.State(std::memory_order_acquire);
                        if (slotState == detail::SlotState::Empty)
                        {
                            break;
                        }
                        if (slotState == detail::SlotState::Tombstone)
                        {
                            if (slot.TryLockFrom(detail::SlotState::Tombstone))
                            {
                                slot.hash = 0;
                                slot.UnlockTo(detail::SlotState::Empty);
                            }
                            break;
                        }
                        if (slotState == detail::SlotState::PendingInsert)
                        {
                            pendingWork = true;
                            std::this_thread::yield();
                            break;
                        }
                        if (slotState == detail::SlotState::Occupied)
                        {
                            if (!slot.TryLockFrom(detail::SlotState::Occupied))
                            {
                                pendingWork = true;
                                std::this_thread::yield();
                                break;
                            }

                            Key               keyCopy   = std::move(slot.KeyRef());
                            Value             valueCopy = std::move(slot.ValueRef());
                            const std::size_t hash      = slot.hash;
                            slot.DestroyPayload();
                            slot.UnlockTo(detail::SlotState::Tombstone);

                            InsertMigrated(state, hash, std::move(keyCopy), std::move(valueCopy));
                            break;
                        }
                    }
                }
                if (pendingWork)
                    std::this_thread::yield();
            } while (pendingWork);
        }

        void InsertMigrated(MigrationState* state, std::size_t hash, Key&& key, Value&& value)
        {
            Key   migrationKey   = std::move(key);
            Value migrationValue = std::move(value);
            while (true)
            {
                TableView   newView = TableView {state->newGroups, state->newMask};
                InsertProbe probe   = LocateForInsert(newView, migrationKey, hash);
                if (!probe.slot)
                {
                    TableView   oldView {state->oldGroups, state->oldMask};
                    InsertProbe oldProbe = LocateForInsert(oldView, migrationKey, hash);
                    if (oldProbe.slot)
                    {
                        if (!oldProbe.isNew)
                        {
                            try
                            {
                                oldProbe.slot->AssignValue(migrationValue);
                            } catch (...)
                            {
                                oldProbe.slot->UnlockTo(detail::SlotState::Occupied);
                                throw;
                            }
                            oldProbe.slot->UnlockTo(detail::SlotState::Occupied);
                            return;
                        }

                        try
                        {
                            oldProbe.slot->ConstructPayload(hash, std::move(migrationKey), std::move(migrationValue));
                            oldProbe.slot->UnlockTo(detail::SlotState::Occupied);
                            return;
                        } catch (...)
                        {
                            oldProbe.slot->hash = 0;
                            oldProbe.slot->UnlockTo(oldProbe.previousState);
                            throw;
                        }
                    }

                    HelpMigration(state, state->oldGroupCount);
                    const bool finalized = FinalizeMigration(state);
                    if (finalized)
                    {
                        size_type targetCapacity = m_capacity ? m_capacity * 2 : GroupSize;
                        targetCapacity           = std::max(targetCapacity, m_capacity + GroupSize);
                        StartMigration(targetCapacity);
                        ViewToken tok2    = AcquireGuardedView();
                        TableView primary = tok2.view;
                        while (true)
                        {
                            InsertProbe retryProbe = LocateForInsert(primary, migrationKey, hash);
                            if (!retryProbe.slot)
                            {
                                size_type nextTarget = m_capacity ? m_capacity * 2 : GroupSize;
                                nextTarget           = std::max(nextTarget, m_capacity + GroupSize);
                                StartMigration(nextTarget);
                                ReleaseGuard(tok2, false);
                                tok2    = AcquireGuardedView();
                                primary = tok2.view;
                                continue;
                            }

                            if (!retryProbe.isNew)
                            {
                                try
                                {
                                    retryProbe.slot->AssignValue(migrationValue);
                                } catch (...)
                                {
                                    retryProbe.slot->UnlockTo(detail::SlotState::Occupied);
                                    ReleaseGuard(tok2, false);
                                    throw;
                                }
                                retryProbe.slot->UnlockTo(detail::SlotState::Occupied);
                                ReleaseGuard(tok2, false);
                                return;
                            }

                            try
                            {
                                retryProbe.slot->ConstructPayload(hash, std::move(migrationKey), std::move(migrationValue));
                                retryProbe.slot->UnlockTo(detail::SlotState::Occupied);
                                ReleaseGuard(tok2, false);
                                return;
                            } catch (...)
                            {
                                retryProbe.slot->hash = 0;
                                retryProbe.slot->UnlockTo(retryProbe.previousState);
                                ReleaseGuard(tok2, false);
                                throw;
                            }
                        }
                    }

                    std::this_thread::yield();
                    continue;
                }

                if (!probe.isNew)
                {
                    try
                    {
                        probe.slot->AssignValue(migrationValue);
                    } catch (...)
                    {
                        probe.slot->UnlockTo(detail::SlotState::Occupied);
                        throw;
                    }
                    probe.slot->UnlockTo(detail::SlotState::Occupied);
                    return;
                }

                try
                {
                    probe.slot->ConstructPayload(hash, std::move(migrationKey), std::move(migrationValue));
                    probe.slot->UnlockTo(detail::SlotState::Occupied);
                    return;
                } catch (...)
                {
                    probe.slot->hash = 0;
                    probe.slot->UnlockTo(probe.previousState);
                    throw;
                }
            }
        }

        void RemoveFromOld(MigrationState* migration, const Key& key, std::size_t hash)
        {
            if (!migration)
                return;
            TableView oldView {migration->oldGroups, migration->oldMask};
            if (auto* secondarySlot = AcquireForMutation(oldView, key, hash))
            {
                secondarySlot->DestroyPayload();
                secondarySlot->UnlockTo(detail::SlotState::Tombstone);
            }
        }

        template<class K>
        InsertProbe LocateForInsert(const TableView& view, const K& key, std::size_t hash)
        {
            size_type index = hash & view.mask;
            // Yield budget: defensive upper bound on consecutive yields without progress to avoid pathological livelock.
            // The theoretical maximum probes is capacity (mask+1). We allow additional yields before abandoning.
            static constexpr size_type kExtraYieldBudget = 2 * 1024;// tunable
            const size_type            capacity          = view.mask + 1;
            size_type                  yields            = 0;
            size_type                  steps             = 0;// number of examined slots
            for (size_type probe = 0; probe <= view.mask; ++probe)
            {
                auto& slot  = SlotAt(view, index);
                auto  state = slot.State(std::memory_order_acquire);
                switch (state)
                {
                    case detail::SlotState::Empty:
                        if (slot.TryLockFrom(detail::SlotState::Empty))
                        {
                            // instrumentation removed
                            return {&slot, detail::SlotState::Empty, true};
                        }
                        break;
                    case detail::SlotState::Tombstone:
                        if (slot.TryLockFrom(detail::SlotState::Tombstone))
                        {
                            // instrumentation removed
                            return {&slot, detail::SlotState::Tombstone, true};
                        }
                        break;
                    case detail::SlotState::Occupied: {
                        if (slot.hash == hash)
                        {
                            if (slot.TryLockFrom(detail::SlotState::Occupied))
                            {
                                const bool match = m_equal(slot.KeyRef(), key);
                                if (match)
                                {
                                    // instrumentation removed
                                    return {&slot, detail::SlotState::Occupied, false};
                                }
                                slot.UnlockTo(detail::SlotState::Occupied);
                            }
                            else
                            {
                                std::this_thread::yield();
                                ++yields;
                                // instrumentation removed
                            }
                        }
                        break;
                    }
                    case detail::SlotState::PendingInsert:
                        std::this_thread::yield();
                        ++yields;
                        // instrumentation removed
                        break;
                }
                if (yields > capacity + kExtraYieldBudget)
                {
                    // instrumentation removed
                    return {};// Abandon to let higher level potentially trigger migration/backoff.
                }
                index = (index + 1) & view.mask;
                ++steps;
            }
            // instrumentation removed
            return {};// Table appears saturated or heavy contention.
        }

        template<class K>
        Slot* AcquireForMutation(const TableView& view, const K& key, std::size_t hash) noexcept
        {
            if (!ViewValid(view))
                return nullptr;
            if (!view.groups)
                return nullptr;
            size_type index = hash & view.mask;
            for (size_type probe = 0; probe <= view.mask; ++probe)
            {
                auto& slot  = SlotAt(view, index);
                auto  state = slot.State(std::memory_order_acquire);
                if (state == detail::SlotState::Empty)
                    return nullptr;
                if (state == detail::SlotState::PendingInsert)
                {
                    std::this_thread::yield();
                }
                else if (state == detail::SlotState::Occupied && slot.hash == hash)
                {
                    if (slot.TryLockFrom(detail::SlotState::Occupied))
                    {
                        if (m_equal(slot.KeyRef(), key))
                            return &slot;
                        slot.UnlockTo(detail::SlotState::Occupied);
                    }
                    else
                    {
                        std::this_thread::yield();
                    }
                }
                index = (index + 1) & view.mask;
            }
            return nullptr;
        }

        template<class K>
        bool ContainsInView(const TableView& view, const K& key, std::size_t hash) const noexcept
        {
            if (!ViewValid(view) || !view.groups)
                return false;
            size_type index = hash & view.mask;
            for (size_type probe = 0; probe <= view.mask;)
            {
                auto&      slot  = SlotAt(view, index);
                const auto state = slot.State(std::memory_order_acquire);
                if (state == detail::SlotState::Empty)
                    return false;
                if (state == detail::SlotState::PendingInsert)
                {
                    std::this_thread::yield();
                    ++probe;
                    index = (index + 1) & view.mask;
                    continue;
                }
                else if (state == detail::SlotState::Occupied)
                {
                    const std::size_t observedHash = slot.hash;
                    if (observedHash == hash)
                    {
                        const Key& candidateKey = slot.KeyRef();
                        const auto verifyState  = slot.State(std::memory_order_acquire);
                        if (verifyState == detail::SlotState::Occupied && slot.hash == observedHash &&
                            m_equal(candidateKey, key))
                        {
                            return true;
                        }
                    }
                }
                ++probe;
                index = (index + 1) & view.mask;
            }
            return false;
        }

        template<class K>
        bool TryCopyValueInView(const TableView& view, const K& key, std::size_t hash, Value& outValue) const
        {
            if (!ViewValid(view) || !view.groups)
                return false;
            size_type index = hash & view.mask;
            for (size_type probe = 0; probe <= view.mask;)
            {
                auto&      slot  = SlotAt(view, index);
                const auto state = slot.State(std::memory_order_acquire);
                if (state == detail::SlotState::Empty)
                    return false;
                if (state == detail::SlotState::PendingInsert)
                {
                    std::this_thread::yield();
                    ++probe;
                    index = (index + 1) & view.mask;
                    continue;
                }
                else if (state == detail::SlotState::Occupied)
                {
                    const std::size_t observedHash = slot.hash;
                    if (observedHash == hash)
                    {
                        Value      snapshot     = slot.ValueRef();
                        const Key& candidateKey = slot.KeyRef();
                        const auto verifyState  = slot.State(std::memory_order_acquire);
                        if (verifyState == detail::SlotState::Occupied && slot.hash == observedHash &&
                            m_equal(candidateKey, key))
                        {
                            outValue = std::move(snapshot);
                            return true;
                        }
                    }
                }
                ++probe;
                index = (index + 1) & view.mask;
            }
            return false;
        }

        template<class K, class V>
        bool EmplaceOrAssignInternal(K&& key, V&& value)
        {
            const std::size_t hash         = ComputeHash(key);
            size_type         attemptCount = 0;// counts LocateForInsert failures (returned empty probe)
            // stats removed
            while (true)
            {
                ViewToken   token     = AcquireGuardedView();
                TableView   primary   = token.view;
                bool        finalized = false;
                InsertProbe probe     = LocateForInsert(primary, key, hash);
                if (!probe.slot)
                {
                    // If in migration, help advance it before backing off.
                    if (token.mig)
                    {
                        HelpMigration(token.mig, 8);
                        finalized = FinalizeMigration(token.mig);
                    }
                    ReleaseGuard(token, finalized);
                    ++attemptCount;
                    // Escalate: if we repeatedly fail to find a slot, forcibly start a migration to grow capacity.
                    if ((attemptCount & (attemptCount - 1)) == 0)// on powers of two attempts: 1,2,4,8,...
                    {
                        size_type desired = m_capacity ? m_capacity * 2 : (GroupSize * 2);
                        StartMigration(desired);
                    }
                    else
                    {
                        MaybeStartMigration();
                    }
                    // Spin + jitter backoff
                    {
                        static thread_local uint32_t seed     = 0x9E3779B9u ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
                        auto                         nextRand = [&]() noexcept { uint32_t x = seed; x ^= x << 13; x ^= x >> 17; x ^= x << 5; seed = x; return x; };
                        const int                    power    = attemptCount < 12 ? static_cast<int>(attemptCount) : 12;
                        int                          spins    = (32 << power) + static_cast<int>(nextRand() & 31u);
                        if (attemptCount > 20)
                            spins = 256;
                        for (int i = 0; i < spins; ++i)
                        {
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86))
                            _mm_pause();
#else
                            asm volatile("");
#endif
                        }
                        if (attemptCount > 8)
                            std::this_thread::yield();
                    }
                    continue;
                }

                if (!probe.isNew)
                {
                    try
                    {
                        probe.slot->AssignValue(std::forward<V>(value));
                    } catch (...)
                    {
                        probe.slot->UnlockTo(detail::SlotState::Occupied);
                        ReleaseGuard(token, finalized);
                        throw;
                    }
                    probe.slot->UnlockTo(detail::SlotState::Occupied);
                    // stats removed
                    if (token.mig)
                    {
                        HelpMigration(token.mig);
                        finalized = FinalizeMigration(token.mig);
                    }
                    else
                    {
                        MaybeStartMigration();
                    }
                    ReleaseGuard(token, finalized);
                    return false;
                }

                try
                {
                    probe.slot->ConstructPayload(hash, std::forward<K>(key), std::forward<V>(value));
                    probe.slot->UnlockTo(detail::SlotState::Occupied);
                } catch (...)
                {
                    probe.slot->hash = 0;
                    probe.slot->UnlockTo(probe.previousState);
                    ReleaseGuard(token, finalized);
                    throw;
                }

                IncrementSizeShard();
                // adaptive logic removed (relied on removed stats)

                if (token.mig)
                {
                    HelpMigration(token.mig);
                    finalized = FinalizeMigration(token.mig);
                }
                else
                {
                    MaybeStartMigration(1);
                }
                ReleaseGuard(token, finalized);
                return true;
            }
        }

        template<class K, class V, class Updater>
        bool UpsertInternal(K&& key, V&& value, Updater&& updater)
        {
            const std::size_t hash         = ComputeHash(key);
            size_type         attemptCount = 0;
            // stats removed
            while (true)
            {
                ViewToken   token     = AcquireGuardedView();
                TableView   primary   = token.view;
                bool        finalized = false;
                InsertProbe probe     = LocateForInsert(primary, key, hash);
                if (!probe.slot)
                {
                    if (token.mig)
                    {
                        HelpMigration(token.mig, 8);
                        finalized = FinalizeMigration(token.mig);
                    }
                    ReleaseGuard(token, finalized);
                    ++attemptCount;
                    if ((attemptCount & (attemptCount - 1)) == 0)
                    {
                        size_type desired = m_capacity ? m_capacity * 2 : (GroupSize * 2);
                        StartMigration(desired);
                    }
                    else
                    {
                        MaybeStartMigration();
                    }
                    // Spin + jitter backoff
                    {
                        static thread_local uint32_t seed     = 0x85EBCA77u ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
                        auto                         nextRand = [&]() noexcept { uint32_t x = seed; x ^= x << 13; x ^= x >> 17; x ^= x << 5; seed = x; return x; };
                        const int                    power    = attemptCount < 12 ? static_cast<int>(attemptCount) : 12;
                        int                          spins    = (32 << power) + static_cast<int>(nextRand() & 31u);
                        if (attemptCount > 20)
                            spins = 256;
                        for (int i = 0; i < spins; ++i)
                        {
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86))
                            _mm_pause();
#else
                            asm volatile("");
#endif
                        }
                        if (attemptCount > 8)
                            std::this_thread::yield();
                    }
                    continue;
                }

                if (!probe.isNew)
                {
                    try
                    {
                        std::invoke(std::forward<Updater>(updater), probe.slot->ValueRef(), std::forward<V>(value));
                    } catch (...)
                    {
                        probe.slot->UnlockTo(detail::SlotState::Occupied);
                        ReleaseGuard(token, finalized);
                        throw;
                    }
                    probe.slot->UnlockTo(detail::SlotState::Occupied);
                    // stats removed
                    if (token.mig)
                    {
                        HelpMigration(token.mig);
                        finalized = FinalizeMigration(token.mig);
                    }
                    else
                    {
                        MaybeStartMigration();
                    }
                    ReleaseGuard(token, finalized);
                    return false;
                }

                try
                {
                    probe.slot->ConstructPayload(hash, std::forward<K>(key), std::forward<V>(value));
                    probe.slot->UnlockTo(detail::SlotState::Occupied);
                } catch (...)
                {
                    probe.slot->hash = 0;
                    probe.slot->UnlockTo(probe.previousState);
                    ReleaseGuard(token, finalized);
                    throw;
                }

                IncrementSizeShard();
                // adaptive logic removed (relied on removed stats)

                if (token.mig)
                {
                    HelpMigration(token.mig);
                    finalized = FinalizeMigration(token.mig);
                }
                else
                {
                    MaybeStartMigration(1);
                }
                ReleaseGuard(token, finalized);
                return true;
            }
        }

        template<class K>
        [[nodiscard]] std::size_t ComputeHash(const K& key) const
        {
            return static_cast<std::size_t>(std::invoke(m_hash, key));
        }

    public:
        // Diagnostics API removed.
    };
}// namespace NGIN::Containers
