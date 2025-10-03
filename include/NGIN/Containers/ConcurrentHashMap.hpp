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
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <new>

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
            alignas(Key) unsigned char   keyStorage[sizeof(Key)] {};
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
                    }
                    catch (...)
                    {
                        keyPtr->~Key();
                        hash = 0;
                        throw;
                    }
                }
                catch (...)
                {
                    hash = 0;
                    throw;
                }
            }

            void DestroyPayload() noexcept
            {
                KeyRef().~Key();
                ValueRef().~Value();
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
             class Hash   = std::hash<Key>,
             class Equal  = std::equal_to<Key>,
             Memory::AllocatorConcept Allocator = Memory::SystemAllocator,
             std::size_t GroupSize              = 16>
    class ConcurrentHashMap
    {
        static_assert(detail::IsPowerOfTwo(GroupSize), "GroupSize must be a power of two.");

    public:
        using key_type        = Key;
        using mapped_type     = Value;
        using hash_type       = Hash;
        using key_equal       = Equal;
        using allocator_type  = Allocator;
        using size_type       = std::size_t;
        using value_type      = std::pair<const Key, Value>;
        static constexpr size_type kGroupSize  = GroupSize;
        static constexpr double    kLoadFactor = 0.75;

        ConcurrentHashMap() : ConcurrentHashMap(64) {}

        explicit ConcurrentHashMap(size_type initialCapacity,
                                    const Hash& hash   = Hash {},
                                    const Equal& equal = Equal {},
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
            m_table            = other.m_table;
            m_capacity         = other.m_capacity;
            m_mask             = other.m_mask;
            m_size.store(other.m_size.load(std::memory_order_relaxed), std::memory_order_relaxed);
            m_resizeThreshold  = other.m_resizeThreshold;
            other.m_table      = {};
            other.m_capacity   = 0;
            other.m_mask       = 0;
            other.m_resizeThreshold = 0;
            other.m_size.store(0, std::memory_order_relaxed);
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
                m_table            = other.m_table;
                m_capacity         = other.m_capacity;
                m_mask             = other.m_mask;
                m_size.store(other.m_size.load(std::memory_order_relaxed), std::memory_order_relaxed);
                m_resizeThreshold  = other.m_resizeThreshold;

                other.m_table      = {};
                other.m_capacity   = 0;
                other.m_mask       = 0;
                other.m_resizeThreshold = 0;
                other.m_size.store(0, std::memory_order_relaxed);
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
            return m_size.load(std::memory_order_acquire);
        }

        [[nodiscard]] bool Empty() const noexcept
        {
            return Size() == 0;
        }

        void Clear() noexcept
        {
            DrainMigration();
            if (!m_table.groups)
                return;

            TableView view = CurrentView();
            for (size_type index = 0; index < m_capacity; ++index)
            {
                auto& slot  = SlotAt(view, index);
                auto  state = slot.State(std::memory_order_acquire);
                if (state == detail::SlotState::Occupied)
                {
                    slot.DestroyPayload();
                }
                slot.Reset();
            }
            m_size.store(0, std::memory_order_release);
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
            const std::size_t hash = ComputeHash(key);
            TableView         primary = CurrentView();

            if (auto* slot = AcquireForMutation(primary, key, hash))
            {
                slot->DestroyPayload();
                slot->UnlockTo(detail::SlotState::Tombstone);
                m_size.fetch_sub(1, std::memory_order_acq_rel);
                if (auto* migration = m_migration.load(std::memory_order_acquire))
                {
                    RemoveFromOld(migration, key, hash);
                    HelpMigration(migration);
                }
                else
                {
                    MaybeStartMigration();
                }
                return true;
            }

            if (auto* migration = m_migration.load(std::memory_order_acquire))
            {
                TableView oldView {migration->oldGroups, migration->oldMask};
                if (auto* slot = AcquireForMutation(oldView, key, hash))
                {
                    slot->DestroyPayload();
                    slot->UnlockTo(detail::SlotState::Tombstone);
                    m_size.fetch_sub(1, std::memory_order_acq_rel);
                    HelpMigration(migration);
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] bool Contains(const Key& key) const noexcept
        {
            const std::size_t hash = ComputeHash(key);
            TableView         primary = CurrentView();

            if (ContainsInView(primary, key, hash))
                return true;

            if (auto* migration = m_migration.load(std::memory_order_acquire))
            {
                TableView oldView {migration->oldGroups, migration->oldMask};
                if (ContainsInView(oldView, key, hash))
                    return true;
            }
            return false;
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
            TableView         primary = CurrentView();

            if (TryCopyValueInView(primary, key, hash, outValue))
                return true;

            if (auto* migration = m_migration.load(std::memory_order_acquire))
            {
                TableView oldView {migration->oldGroups, migration->oldMask};
                if (TryCopyValueInView(oldView, key, hash, outValue))
                    return true;
            }
            return false;
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
            if (auto* migration = m_migration.load(std::memory_order_acquire))
            {
                HelpMigration(migration, migration->oldGroupCount);
                FinalizeMigration(migration);
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
            return static_cast<double>(m_size.load(std::memory_order_acquire)) /
                   static_cast<double>(m_capacity);
        }

        template<class Callback>
        void ForEach(Callback&& callback) const
        {
            const_cast<ConcurrentHashMap*>(this)->DrainMigration();
            TableView view = CurrentView();
            for (size_type index = 0; index < m_capacity; ++index)
            {
                const auto& slot = SlotAt(view, index);
                if (slot.State(std::memory_order_acquire) == detail::SlotState::Occupied)
                {
                    callback(slot.KeyRef(), slot.ValueRef());
                }
            }
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
            Group*                oldGroups {nullptr};
            size_type             oldGroupCount {0};
            size_type             oldMask {0};
            Group*                newGroups {nullptr};
            size_type             newGroupCount {0};
            size_type             newMask {0};
            size_type             newThreshold {0};
            std::atomic<size_type> nextGroup {0};
            std::atomic<size_type> migratedGroups {0};
        };

        Table                         m_table {};
        size_type                     m_capacity {0};
        size_type                     m_mask {0};
        std::atomic<size_type>        m_size {0};
        size_type                     m_resizeThreshold {0};
        Hash                          m_hash {};
        Equal                         m_equal {};
        Allocator                     m_allocator {};
        std::atomic<MigrationState*>  m_migration {nullptr};

        static constexpr size_type kGroupMask  = GroupSize - 1;
        static constexpr size_type kGroupShift = detail::ConstLog2(GroupSize);

        struct InsertProbe
        {
            Slot*             slot {nullptr};
            detail::SlotState previousState {detail::SlotState::Empty};
            bool              isNew {false};
        };

        void Initialize(size_type initialCapacity)
        {
            DrainMigration();
            initialCapacity = std::max<size_type>(GroupSize, std::bit_ceil(initialCapacity));
            size_type groupCount = initialCapacity / GroupSize;
            Group*    groups     = AllocateGroups(groupCount);

            m_table.groups     = groups;
            m_table.groupCount = groupCount;
            m_capacity         = groupCount * GroupSize;
            m_mask             = m_capacity - 1;
            m_resizeThreshold  = ComputeThreshold(m_capacity);
            m_size.store(0, std::memory_order_relaxed);
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
        }

        [[nodiscard]] TableView CurrentView() const noexcept
        {
            return TableView {m_table.groups, m_mask};
        }

        static Slot& SlotAt(const TableView& view, size_type index) noexcept
        {
            return view.groups[index >> kGroupShift].slots[index & kGroupMask];
        }

        [[nodiscard]] size_type ComputeThreshold(size_type capacity) const noexcept
        {
            if (capacity <= 1)
                return 1;
            const double raw       = static_cast<double>(capacity) * kLoadFactor;
            const auto   threshold = static_cast<size_type>(raw);
            const size_type capped = threshold >= capacity ? capacity - 1 : threshold;
            return capped == 0 ? 1 : capped;
        }

        void MaybeStartMigration(size_type additional = 0)
        {
            if (m_migration.load(std::memory_order_acquire))
                return;
            const size_type projected = m_size.load(std::memory_order_acquire) + additional;
            if (projected <= m_resizeThreshold)
                return;
            StartMigration(m_capacity ? m_capacity * 2 : GroupSize);
        }

        void StartMigration(size_type desiredCapacity)
        {
            desiredCapacity = std::max<size_type>(GroupSize, std::bit_ceil(desiredCapacity));
            size_type newGroupCount = desiredCapacity / GroupSize;
            Group*    newGroups     = AllocateGroups(newGroupCount);

            auto* state            = new MigrationState();
            state->oldGroups       = m_table.groups;
            state->oldGroupCount   = m_table.groupCount;
            state->oldMask         = m_mask;
            state->newGroups       = newGroups;
            state->newGroupCount   = newGroupCount;
            state->newMask         = desiredCapacity - 1;
            state->newThreshold    = ComputeThreshold(desiredCapacity);
            state->nextGroup.store(0, std::memory_order_relaxed);
            state->migratedGroups.store(0, std::memory_order_relaxed);

            MigrationState* expected = nullptr;
            if (m_migration.compare_exchange_strong(
                        expected,
                        state,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed))
            {
                m_table.groups     = newGroups;
                m_table.groupCount = newGroupCount;
                m_capacity         = desiredCapacity;
                m_mask             = state->newMask;
                m_resizeThreshold  = state->newThreshold;
                HelpMigration(state, 1);
            }
            else
            {
                ReleaseGroups(newGroups, newGroupCount);
                delete state;
            }
        }

        void DrainMigration() noexcept
        {
            while (auto* migration = m_migration.load(std::memory_order_acquire))
            {
                HelpMigration(migration, migration->oldGroupCount);
                FinalizeMigration(migration);
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
            if (state->migratedGroups.load(std::memory_order_acquire) >= totalGroups)
            {
                FinalizeMigration(state);
            }
        }

        void FinalizeMigration(MigrationState* state) noexcept
        {
            if (!state)
                return;
            if (state->migratedGroups.load(std::memory_order_acquire) < state->oldGroupCount)
                return;

            MigrationState* expected = state;
            if (m_migration.compare_exchange_strong(
                        expected,
                        nullptr,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed))
            {
                ReleaseGroups(state->oldGroups, state->oldGroupCount);
                delete state;
            }
        }

        void MigrateGroup(MigrationState* state, size_type groupIndex) noexcept
        {
            if (!state)
                return;
            TableView oldView {state->oldGroups, state->oldMask};
            const size_type base = groupIndex << kGroupShift;
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
                        std::this_thread::yield();
                        continue;
                    }
                    if (slotState == detail::SlotState::Occupied)
                    {
                        if (!slot.TryLockFrom(detail::SlotState::Occupied))
                        {
                            std::this_thread::yield();
                            continue;
                        }

                        Key   keyCopy   = std::move(slot.KeyRef());
                        Value valueCopy = std::move(slot.ValueRef());
                        const std::size_t hash = slot.hash;
                        slot.DestroyPayload();
                        slot.UnlockTo(detail::SlotState::Tombstone);

                        InsertMigrated(state, hash, std::move(keyCopy), std::move(valueCopy));
                        break;
                    }
                }
            }
        }

        void InsertMigrated(MigrationState* state, std::size_t hash, Key&& key, Value&& value)
        {
            TableView newView {state->newGroups, state->newMask};
            while (true)
            {
                InsertProbe probe = LocateForInsert(newView, key, hash);
                if (!probe.slot)
                {
                    // Table unexpectedly full; trigger a follow-up growth.
                    StartMigration(m_capacity ? m_capacity * 2 : GroupSize);
                    continue;
                }

                if (!probe.isNew)
                {
                    try
                    {
                        probe.slot->AssignValue(value);
                    }
                    catch (...)
                    {
                        probe.slot->UnlockTo(detail::SlotState::Occupied);
                        throw;
                    }
                    probe.slot->UnlockTo(detail::SlotState::Occupied);
                    return;
                }

                try
                {
                    probe.slot->ConstructPayload(hash, std::move(key), std::move(value));
                    probe.slot->UnlockTo(detail::SlotState::Occupied);
                    return;
                }
                catch (...)
                {
                    probe.slot->hash = 0;
                    probe.slot->UnlockTo(probe.previousState);
                    throw;
                }
            }
        }

        void UpdateOldValue(MigrationState* migration, const Key& key, std::size_t hash, const Value& value)
        {
            if (!migration)
                return;
            TableView oldView {migration->oldGroups, migration->oldMask};
            size_type index = hash & oldView.mask;
            for (size_type probe = 0; probe <= oldView.mask; ++probe)
            {
                auto& slot  = SlotAt(oldView, index);
                auto  state = slot.State(std::memory_order_acquire);
                if (state == detail::SlotState::Empty)
                    return;
                if (state == detail::SlotState::PendingInsert)
                {
                    std::this_thread::yield();
                }
                else if (state == detail::SlotState::Occupied && slot.hash == hash && m_equal(slot.KeyRef(), key))
                {
                    if (slot.TryLockFrom(detail::SlotState::Occupied))
                    {
                        try
                        {
                            slot.AssignValue(value);
                        }
                        catch (...)
                        {
                            slot.UnlockTo(detail::SlotState::Occupied);
                            throw;
                        }
                        slot.UnlockTo(detail::SlotState::Occupied);
                    }
                    return;
                }
                index = (index + 1) & oldView.mask;
            }
        }

        void RemoveFromOld(MigrationState* migration, const Key& key, std::size_t hash)
        {
            if (!migration)
                return;
            TableView oldView {migration->oldGroups, migration->oldMask};
            if (auto* slot = AcquireForMutation(oldView, key, hash))
            {
                slot->DestroyPayload();
                slot->UnlockTo(detail::SlotState::Tombstone);
            }
        }

        template<class K>
        InsertProbe LocateForInsert(const TableView& view, const K& key, std::size_t hash)
        {
            size_type index = hash & view.mask;
            for (size_type probe = 0; probe <= view.mask; ++probe)
            {
                auto& slot  = SlotAt(view, index);
                auto  state = slot.State(std::memory_order_acquire);
                switch (state)
                {
                case detail::SlotState::Empty:
                    if (slot.TryLockFrom(detail::SlotState::Empty))
                        return {&slot, detail::SlotState::Empty, true};
                    break;
                case detail::SlotState::Tombstone:
                    if (slot.TryLockFrom(detail::SlotState::Tombstone))
                        return {&slot, detail::SlotState::Tombstone, true};
                    break;
                case detail::SlotState::Occupied:
                    if (slot.hash == hash && m_equal(slot.KeyRef(), key))
                    {
                        if (slot.TryLockFrom(detail::SlotState::Occupied))
                            return {&slot, detail::SlotState::Occupied, false};
                    }
                    break;
                case detail::SlotState::PendingInsert:
                    std::this_thread::yield();
                    break;
                }
                index = (index + 1) & view.mask;
            }
            return {};
        }

        template<class K>
        Slot* AcquireForMutation(const TableView& view, const K& key, std::size_t hash) noexcept
        {
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
                else if (state == detail::SlotState::Occupied && slot.hash == hash && m_equal(slot.KeyRef(), key))
                {
                    if (slot.TryLockFrom(detail::SlotState::Occupied))
                        return &slot;
                }
                index = (index + 1) & view.mask;
            }
            return nullptr;
        }

        template<class K>
        bool ContainsInView(const TableView& view, const K& key, std::size_t hash) const noexcept
        {
            size_type index = hash & view.mask;
            for (size_type probe = 0; probe <= view.mask; ++probe)
            {
                const auto& slot  = SlotAt(view, index);
                const auto  state = slot.State(std::memory_order_acquire);
                if (state == detail::SlotState::Empty)
                    return false;
                if (state == detail::SlotState::PendingInsert)
                {
                    std::this_thread::yield();
                }
                else if (state == detail::SlotState::Occupied && slot.hash == hash && m_equal(slot.KeyRef(), key))
                {
                    const auto verify = slot.State(std::memory_order_acquire);
                    if (verify == detail::SlotState::Occupied)
                        return true;
                }
                index = (index + 1) & view.mask;
            }
            return false;
        }

        template<class K>
        bool TryCopyValueInView(const TableView& view, const K& key, std::size_t hash, Value& outValue) const
        {
            size_type index = hash & view.mask;
            for (size_type probe = 0; probe <= view.mask; ++probe)
            {
                const auto& slot  = SlotAt(view, index);
                const auto  state = slot.State(std::memory_order_acquire);
                if (state == detail::SlotState::Empty)
                    return false;
                if (state == detail::SlotState::PendingInsert)
                {
                    std::this_thread::yield();
                }
                else if (state == detail::SlotState::Occupied && slot.hash == hash && m_equal(slot.KeyRef(), key))
                {
                    Value snapshot = slot.ValueRef();
                    const auto verify = slot.State(std::memory_order_acquire);
                    if (verify == detail::SlotState::Occupied)
                    {
                        outValue = std::move(snapshot);
                        return true;
                    }
                }
                index = (index + 1) & view.mask;
            }
            return false;
        }

        template<class K, class V>
        bool EmplaceOrAssignInternal(K&& key, V&& value)
        {
            const std::size_t hash = ComputeHash(key);
            while (true)
            {
                TableView         primary   = CurrentView();
                MigrationState*   migration = m_migration.load(std::memory_order_acquire);
                InsertProbe       probe     = LocateForInsert(primary, key, hash);
                if (!probe.slot)
                {
                    MaybeStartMigration();
                    continue;
                }

                if (!probe.isNew)
                {
                    try
                    {
                        probe.slot->AssignValue(std::forward<V>(value));
                    }
                    catch (...)
                    {
                        probe.slot->UnlockTo(detail::SlotState::Occupied);
                        throw;
                    }
                    probe.slot->UnlockTo(detail::SlotState::Occupied);
                    if (migration)
                    {
                        UpdateOldValue(migration, probe.slot->KeyRef(), hash, probe.slot->ValueRef());
                        HelpMigration(migration);
                    }
                    else
                    {
                        MaybeStartMigration();
                    }
                    return false;
                }

                try
                {
                    probe.slot->ConstructPayload(hash, std::forward<K>(key), std::forward<V>(value));
                    probe.slot->UnlockTo(detail::SlotState::Occupied);
                }
                catch (...)
                {
                    probe.slot->hash = 0;
                    probe.slot->UnlockTo(probe.previousState);
                    throw;
                }

                m_size.fetch_add(1, std::memory_order_acq_rel);

                if (migration)
                {
                    UpdateOldValue(migration, probe.slot->KeyRef(), hash, probe.slot->ValueRef());
                    HelpMigration(migration);
                }
                else
                {
                    MaybeStartMigration();
                }
                return true;
            }
        }

        template<class K, class V, class Updater>
        bool UpsertInternal(K&& key, V&& value, Updater&& updater)
        {
            const std::size_t hash = ComputeHash(key);
            while (true)
            {
                TableView         primary   = CurrentView();
                MigrationState*   migration = m_migration.load(std::memory_order_acquire);
                InsertProbe       probe     = LocateForInsert(primary, key, hash);
                if (!probe.slot)
                {
                    MaybeStartMigration();
                    continue;
                }

                if (!probe.isNew)
                {
                    try
                    {
                        std::invoke(std::forward<Updater>(updater), probe.slot->ValueRef(), std::forward<V>(value));
                    }
                    catch (...)
                    {
                        probe.slot->UnlockTo(detail::SlotState::Occupied);
                        throw;
                    }
                    probe.slot->UnlockTo(detail::SlotState::Occupied);
                    if (migration)
                    {
                        UpdateOldValue(migration, probe.slot->KeyRef(), hash, probe.slot->ValueRef());
                        HelpMigration(migration);
                    }
                    else
                    {
                        MaybeStartMigration();
                    }
                    return false;
                }

                try
                {
                    probe.slot->ConstructPayload(hash, std::forward<K>(key), std::forward<V>(value));
                    probe.slot->UnlockTo(detail::SlotState::Occupied);
                }
                catch (...)
                {
                    probe.slot->hash = 0;
                    probe.slot->UnlockTo(probe.previousState);
                    throw;
                }

                m_size.fetch_add(1, std::memory_order_acq_rel);

                if (migration)
                {
                    UpdateOldValue(migration, probe.slot->KeyRef(), hash, probe.slot->ValueRef());
                    HelpMigration(migration);
                }
                else
                {
                    MaybeStartMigration();
                }
                return true;
            }
        }

        template<class K>
        [[nodiscard]] std::size_t ComputeHash(const K& key) const
        {
            return static_cast<std::size_t>(std::invoke(m_hash, key));
        }
    };
}// namespace NGIN::Containers
