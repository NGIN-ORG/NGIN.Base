/// @file ConcurrentHashMap.hpp
/// @brief Sharded concurrent hash map scaffold with policy-pluggable reclamation.
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Sync/SpinLock.hpp>

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace NGIN::Containers
{
    enum class ReclamationPolicy : std::uint8_t
    {
        ManualQuiesce,
        HazardPointers,
        LocalEpoch,
    };
}// namespace NGIN::Containers

#include <NGIN/Containers/detail/ConcurrentHashMapDetail.hpp>

namespace NGIN::Containers
{

    /// @brief Concurrent hash map scaffold with sharded writer locks and pinned lock-free reads.
    ///
    /// Current state:
    /// - Shared core architecture is in place.
    /// - `ManualQuiesce` now has a dedicated deferred-reclamation backend.
    /// - `LocalEpoch` and `HazardPointers` still compile through the same scaffolded pinned-reader contract.
    /// - Automatic policies currently use the same safe retirement path until dedicated epoch and hazard
    ///   implementations land.
    template<class Key,
             class Value,
             class Hash                          = std::hash<Key>,
             class Equal                         = std::equal_to<Key>,
             Memory::AllocatorConcept Allocator  = Memory::SystemAllocator,
             ReclamationPolicy        Policy     = ReclamationPolicy::LocalEpoch,
             std::size_t              ShardCount = 64>
    class ConcurrentHashMap
    {
        static_assert(ShardCount > 0, "ShardCount must be greater than zero.");

    public:
        using key_type       = Key;
        using mapped_type    = Value;
        using hash_type      = Hash;
        using key_equal      = Equal;
        using allocator_type = Allocator;
        using size_type      = std::size_t;
        using value_type     = std::pair<const Key, Value>;

        static constexpr double            kLoadFactor         = 0.75;
        static constexpr ReclamationPolicy kReclamationPolicy  = Policy;
        static constexpr size_type         kShardCount         = ShardCount;
        static constexpr size_type         kMinBucketsPerShard = 8;

        ConcurrentHashMap()
            : ConcurrentHashMap(64)
        {
        }

        explicit ConcurrentHashMap(size_type        initialCapacity,
                                   const Hash&      hash      = Hash {},
                                   const Equal&     equal     = Equal {},
                                   const Allocator& allocator = Allocator {})
            : m_hash(hash), m_equal(equal), m_allocator(allocator)
        {
            const size_type perShardCapacity = detail::CeilDivide(std::max<size_type>(initialCapacity, ShardCount), ShardCount);
            const size_type bucketCount      = BucketCountForElements(perShardCapacity);

            for (auto& shard: m_shards)
            {
                Table* table = AllocateEmptyTable(bucketCount);
                shard.table.store(table, std::memory_order_release);
                shard.bucketCount.store(bucketCount, std::memory_order_release);
                shard.size = 0;
            }
        }

        ConcurrentHashMap(const ConcurrentHashMap&)                    = delete;
        auto operator=(const ConcurrentHashMap&) -> ConcurrentHashMap& = delete;
        ConcurrentHashMap(ConcurrentHashMap&&)                         = delete;
        auto operator=(ConcurrentHashMap&&) -> ConcurrentHashMap&      = delete;

        ~ConcurrentHashMap()
        {
            for (auto& shard: m_shards)
            {
                std::lock_guard<Sync::SpinLock> lock(shard.writeLock);
                shard.reclaimer.Drain();

                Table* table = shard.table.exchange(nullptr, std::memory_order_acq_rel);
                shard.bucketCount.store(0, std::memory_order_release);
                shard.size = 0;

                if (table)
                {
                    DestroyTable(table);
                }
            }
        }

        [[nodiscard]] auto Size() const noexcept -> size_type
        {
            return m_size.load(std::memory_order_acquire);
        }

        [[nodiscard]] auto Empty() const noexcept -> bool
        {
            return Size() == 0;
        }

        [[nodiscard]] auto Capacity() const noexcept -> size_type
        {
            size_type capacity = 0;
            for (const auto& shard: m_shards)
            {
                capacity += shard.bucketCount.load(std::memory_order_acquire);
            }
            return capacity;
        }

        [[nodiscard]] auto LoadFactor() const noexcept -> double
        {
            const size_type capacity = Capacity();
            if (capacity == 0)
            {
                return 0.0;
            }
            return static_cast<double>(Size()) / static_cast<double>(capacity);
        }

        bool Insert(const Key& key, const Value& value)
        {
            return InsertOrAssignInternal(key, value);
        }

        bool Insert(const Key& key, Value&& value)
        {
            return InsertOrAssignInternal(key, std::move(value));
        }

        bool Insert(Key&& key, const Value& value)
        {
            return InsertOrAssignInternal(std::move(key), value);
        }

        bool Insert(Key&& key, Value&& value)
        {
            return InsertOrAssignInternal(std::move(key), std::move(value));
        }

        bool InsertOrAssign(const Key& key, const Value& value)
        {
            return InsertOrAssignInternal(key, value);
        }

        bool InsertOrAssign(const Key& key, Value&& value)
        {
            return InsertOrAssignInternal(key, std::move(value));
        }

        bool InsertOrAssign(Key&& key, const Value& value)
        {
            return InsertOrAssignInternal(std::move(key), value);
        }

        bool InsertOrAssign(Key&& key, Value&& value)
        {
            return InsertOrAssignInternal(std::move(key), std::move(value));
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
            const std::size_t               hash  = ComputeHash(key);
            Shard&                          shard = ShardForHash(hash);
            std::lock_guard<Sync::SpinLock> lock(shard.writeLock);

            Table* table = shard.table.load(std::memory_order_acquire);
            if (!table)
            {
                return false;
            }

            const size_type bucketIndex = BucketIndex(hash, table->bucketCount);
            Node* const     oldHead     = table->buckets[bucketIndex].load(std::memory_order_acquire);
            if (!FindNodeInChain(oldHead, key, hash))
            {
                return false;
            }

            Node* newHead = CloneChainWithoutKey(oldHead, key, hash);
            table->buckets[bucketIndex].store(newHead, std::memory_order_release);
            RetireChain(shard, oldHead);

            --shard.size;
            m_size.fetch_sub(1, std::memory_order_acq_rel);
            shard.reclaimer.Poll();
            return true;
        }

        [[nodiscard]] bool Contains(const Key& key) const noexcept
        {
            const std::size_t  hash  = ComputeHash(key);
            const Shard&       shard = ShardForHash(hash);
            auto               guard = shard.reclaimer.Enter();
            const Table* const table = shard.reclaimer.Protect(shard.table, guard);
            return FindNodeInTable(table, key, hash) != nullptr;
        }

        auto Get(const Key& key) const -> Value
        {
            if (auto value = GetOptional(key); value.has_value())
            {
                return std::move(*value);
            }
            throw std::out_of_range("ConcurrentHashMap::Get - key not found");
        }

        bool TryGet(const Key& key, Value& outValue) const
        {
            const std::size_t  hash  = ComputeHash(key);
            const Shard&       shard = ShardForHash(hash);
            auto               guard = shard.reclaimer.Enter();
            const Table* const table = shard.reclaimer.Protect(shard.table, guard);
            if (const Node* node = FindNodeInTable(table, key, hash))
            {
                outValue = node->value;
                return true;
            }
            return false;
        }

        [[nodiscard]] auto GetOptional(const Key& key) const -> std::optional<Value>
        {
            const std::size_t  hash  = ComputeHash(key);
            const Shard&       shard = ShardForHash(hash);
            auto               guard = shard.reclaimer.Enter();
            const Table* const table = shard.reclaimer.Protect(shard.table, guard);
            if (const Node* node = FindNodeInTable(table, key, hash))
            {
                return node->value;
            }
            return std::nullopt;
        }

        void Clear()
        {
            for (auto& shard: m_shards)
            {
                std::lock_guard<Sync::SpinLock> lock(shard.writeLock);

                Table* const current = shard.table.load(std::memory_order_acquire);
                if (!current)
                {
                    continue;
                }

                Table* replacement = AllocateEmptyTable(current->bucketCount);
                shard.table.store(replacement, std::memory_order_release);
                shard.bucketCount.store(replacement->bucketCount, std::memory_order_release);

                if (shard.size != 0)
                {
                    m_size.fetch_sub(shard.size, std::memory_order_acq_rel);
                    shard.size = 0;
                }

                RetireTable(shard, current);
                shard.reclaimer.Poll();
            }
        }

        void Reserve(size_type desiredCapacity)
        {
            const size_type perShardCapacity = detail::CeilDivide(std::max<size_type>(desiredCapacity, ShardCount), ShardCount);
            for (auto& shard: m_shards)
            {
                std::lock_guard<Sync::SpinLock> lock(shard.writeLock);
                EnsureShardCapacity(shard, perShardCapacity);
                shard.reclaimer.Poll();
            }
        }

        template<class Callback>
        void ForEach(Callback&& callback) const
        {
            for (const auto& shard: m_shards)
            {
                auto               guard = shard.reclaimer.Enter();
                const Table* const table = shard.reclaimer.Protect(shard.table, guard);
                if (!table)
                {
                    continue;
                }

                for (size_type bucketIndex = 0; bucketIndex < table->bucketCount; ++bucketIndex)
                {
                    const Node* node = table->buckets[bucketIndex].load(std::memory_order_acquire);
                    while (node)
                    {
                        callback(node->key, node->value);
                        node = node->next;
                    }
                }
            }
        }

        template<class Callback>
        void SnapshotForEach(Callback&& callback) const
        {
            ForEach(std::forward<Callback>(callback));
        }

        void Quiesce() noexcept
        {
            for (auto& shard: m_shards)
            {
                std::lock_guard<Sync::SpinLock> lock(shard.writeLock);
                shard.reclaimer.Quiesce();
            }
        }

    private:
        using Node      = detail::ConcurrentHashMapNode<Key, Value>;
        using Table     = detail::ConcurrentHashMapTable<Node>;
        using Reclaimer = detail::ConcurrentHashMapReclaimer<Policy>;

        struct alignas(64) Shard
        {
            mutable Sync::SpinLock writeLock {};
            std::atomic<Table*>    table {nullptr};
            std::atomic<size_type> bucketCount {0};
            Reclaimer              reclaimer {};
            size_type              size {0};
        };

        template<class K>
        [[nodiscard]] auto ComputeHash(const K& key) const -> std::size_t
        {
            return static_cast<std::size_t>(std::invoke(m_hash, key));
        }

        [[nodiscard]] auto FindNodeInChain(const Node* head, const Key& key, const std::size_t hash) const -> const Node*
        {
            const Node* current = head;
            while (current)
            {
                if (current->hash == hash && m_equal(current->key, key))
                {
                    return current;
                }
                current = current->next;
            }
            return nullptr;
        }

        [[nodiscard]] auto FindNodeInTable(const Table* table, const Key& key, const std::size_t hash) const -> const Node*
        {
            if (!table || table->bucketCount == 0)
            {
                return nullptr;
            }

            const size_type bucketIndex = BucketIndex(hash, table->bucketCount);
            return FindNodeInChain(table->buckets[bucketIndex].load(std::memory_order_acquire), key, hash);
        }

        [[nodiscard]] auto ShardIndex(const std::size_t hash) const noexcept -> size_type
        {
            if constexpr (detail::IsPowerOfTwo(ShardCount))
            {
                return hash & (ShardCount - 1);
            }
            return hash % ShardCount;
        }

        [[nodiscard]] auto ShardForHash(const std::size_t hash) noexcept -> Shard&
        {
            return m_shards[ShardIndex(hash)];
        }

        [[nodiscard]] auto ShardForHash(const std::size_t hash) const noexcept -> const Shard&
        {
            return m_shards[ShardIndex(hash)];
        }

        [[nodiscard]] static auto BucketIndex(const std::size_t hash, const size_type bucketCount) noexcept -> size_type
        {
            return hash & (bucketCount - 1);
        }

        [[nodiscard]] static auto BucketCountForElements(const size_type desiredElements) noexcept -> size_type
        {
            const size_type adjusted = std::max<size_type>(
                    kMinBucketsPerShard,
                    desiredElements == 0 ? kMinBucketsPerShard
                                         : detail::CeilDivide(desiredElements * 4 + 2, 3));
            return std::bit_ceil(adjusted);
        }

        [[nodiscard]] auto AllocateBytes(const size_type bytes, const size_type alignment) -> void*
        {
            void* memory = m_allocator.Allocate(bytes, alignment);
            if (!memory)
            {
                throw std::bad_alloc();
            }
            return memory;
        }

        template<class T, class... Args>
        [[nodiscard]] auto AllocateObject(Args&&... args) -> T*
        {
            void* memory = AllocateBytes(sizeof(T), alignof(T));
            try
            {
                return ::new (memory) T(std::forward<Args>(args)...);
            } catch (...)
            {
                m_allocator.Deallocate(memory, sizeof(T), alignof(T));
                throw;
            }
        }

        template<class T>
        void DestroyObject(T* object) noexcept
        {
            if (!object)
            {
                return;
            }
            object->~T();
            m_allocator.Deallocate(object, sizeof(T), alignof(T));
        }

        [[nodiscard]] auto AllocateBuckets(const size_type bucketCount) -> typename Table::Bucket*
        {
            using Bucket = typename Table::Bucket;

            auto* buckets = static_cast<Bucket*>(
                    AllocateBytes(sizeof(Bucket) * bucketCount, alignof(Bucket)));
            size_type initialized = 0;
            try
            {
                for (; initialized < bucketCount; ++initialized)
                {
                    ::new (static_cast<void*>(buckets + initialized)) Bucket(nullptr);
                }
            } catch (...)
            {
                for (size_type i = 0; i < initialized; ++i)
                {
                    buckets[i].~Bucket();
                }
                m_allocator.Deallocate(buckets, sizeof(Bucket) * bucketCount, alignof(Bucket));
                throw;
            }

            return buckets;
        }

        void DestroyBuckets(typename Table::Bucket* buckets, const size_type bucketCount) noexcept
        {
            if (!buckets)
            {
                return;
            }

            using Bucket = typename Table::Bucket;
            for (size_type i = 0; i < bucketCount; ++i)
            {
                buckets[i].~Bucket();
            }
            m_allocator.Deallocate(buckets, sizeof(Bucket) * bucketCount, alignof(Bucket));
        }

        [[nodiscard]] auto AllocateEmptyTable(const size_type bucketCount) -> Table*
        {
            auto* table        = AllocateObject<Table>();
            table->bucketCount = bucketCount;
            try
            {
                table->buckets = AllocateBuckets(bucketCount);
            } catch (...)
            {
                DestroyObject(table);
                throw;
            }
            return table;
        }

        template<class K, class V>
        [[nodiscard]] auto AllocateNode(const std::size_t hash, Node* next, K&& key, V&& value) -> Node*
        {
            return AllocateObject<Node>(hash, next, std::forward<K>(key), std::forward<V>(value));
        }

        void DestroyChain(Node* head) noexcept
        {
            while (head)
            {
                Node* next = head->next;
                DestroyObject(head);
                head = next;
            }
        }

        void DestroyTable(Table* table) noexcept
        {
            if (!table)
            {
                return;
            }

            for (size_type bucketIndex = 0; bucketIndex < table->bucketCount; ++bucketIndex)
            {
                DestroyChain(table->buckets[bucketIndex].load(std::memory_order_relaxed));
            }

            DestroyBuckets(table->buckets, table->bucketCount);
            DestroyObject(table);
        }

        static void DestroyChainThunk(void* context, void* object) noexcept
        {
            auto* self = static_cast<ConcurrentHashMap*>(context);
            self->DestroyChain(static_cast<Node*>(object));
        }

        static void DestroyTableThunk(void* context, void* object) noexcept
        {
            auto* self = static_cast<ConcurrentHashMap*>(context);
            self->DestroyTable(static_cast<Table*>(object));
        }

        void RetireChain(Shard& shard, Node* chainHead)
        {
            shard.reclaimer.Retire(chainHead, this, &DestroyChainThunk);
        }

        void RetireTable(Shard& shard, Table* table)
        {
            shard.reclaimer.Retire(table, this, &DestroyTableThunk);
        }

        [[nodiscard]] auto CloneChain(const Node* head) -> Node*
        {
            std::vector<const Node*> nodes;
            for (const Node* current = head; current; current = current->next)
            {
                nodes.push_back(current);
            }

            Node* newHead = nullptr;
            try
            {
                for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
                {
                    const Node* source = *it;
                    newHead            = AllocateNode(source->hash, newHead, source->key, source->value);
                }
            } catch (...)
            {
                DestroyChain(newHead);
                throw;
            }

            return newHead;
        }

        template<class NewValueFactory>
        [[nodiscard]] auto CloneChainReplacingValue(const Node*       head,
                                                    const Key&        key,
                                                    const std::size_t hash,
                                                    NewValueFactory&& factory) -> Node*
        {
            std::vector<const Node*> nodes;
            for (const Node* current = head; current; current = current->next)
            {
                nodes.push_back(current);
            }

            Node* newHead = nullptr;
            try
            {
                for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
                {
                    const Node* source = *it;
                    if (source->hash == hash && m_equal(source->key, key))
                    {
                        auto value = std::invoke(factory, source->value);
                        newHead    = AllocateNode(source->hash, newHead, source->key, std::move(value));
                    }
                    else
                    {
                        newHead = AllocateNode(source->hash, newHead, source->key, source->value);
                    }
                }
            } catch (...)
            {
                DestroyChain(newHead);
                throw;
            }

            return newHead;
        }

        [[nodiscard]] auto CloneChainWithoutKey(const Node* head, const Key& key, const std::size_t hash) -> Node*
        {
            std::vector<const Node*> nodes;
            for (const Node* current = head; current; current = current->next)
            {
                nodes.push_back(current);
            }

            Node* newHead = nullptr;
            try
            {
                for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
                {
                    const Node* source = *it;
                    if (source->hash == hash && m_equal(source->key, key))
                    {
                        continue;
                    }
                    newHead = AllocateNode(source->hash, newHead, source->key, source->value);
                }
            } catch (...)
            {
                DestroyChain(newHead);
                throw;
            }

            return newHead;
        }

        template<class K, class V>
        [[nodiscard]] auto CloneChainWithPrepended(const Node* head, const std::size_t hash, K&& key, V&& value) -> Node*
        {
            Node* cloned = CloneChain(head);
            try
            {
                return AllocateNode(hash, cloned, std::forward<K>(key), std::forward<V>(value));
            } catch (...)
            {
                DestroyChain(cloned);
                throw;
            }
        }

        [[nodiscard]] auto ResizeTable(const Table* current, const size_type desiredElements) -> Table*
        {
            const size_type newBucketCount = BucketCountForElements(desiredElements);
            if (current && current->bucketCount >= newBucketCount)
            {
                return nullptr;
            }

            Table* replacement = AllocateEmptyTable(newBucketCount);
            try
            {
                if (current)
                {
                    for (size_type bucketIndex = 0; bucketIndex < current->bucketCount; ++bucketIndex)
                    {
                        for (const Node* node = current->buckets[bucketIndex].load(std::memory_order_relaxed);
                             node;
                             node = node->next)
                        {
                            const size_type targetIndex =
                                    BucketIndex(node->hash, replacement->bucketCount);
                            Node* head = replacement->buckets[targetIndex].load(std::memory_order_relaxed);
                            Node* copy = AllocateNode(node->hash, head, node->key, node->value);
                            replacement->buckets[targetIndex].store(copy, std::memory_order_relaxed);
                        }
                    }
                }
            } catch (...)
            {
                DestroyTable(replacement);
                throw;
            }

            return replacement;
        }

        void EnsureShardCapacity(Shard& shard, const size_type desiredElements)
        {
            Table* current = shard.table.load(std::memory_order_acquire);
            Table* resized = ResizeTable(current, desiredElements);
            if (!resized)
            {
                return;
            }

            shard.table.store(resized, std::memory_order_release);
            shard.bucketCount.store(resized->bucketCount, std::memory_order_release);
            RetireTable(shard, current);
        }

        template<class K, class V>
        bool InsertOrAssignInternal(K&& key, V&& value)
        {
            const std::size_t               hash  = ComputeHash(key);
            Shard&                          shard = ShardForHash(hash);
            std::lock_guard<Sync::SpinLock> lock(shard.writeLock);

            EnsureShardCapacity(shard, shard.size + 1);

            Table* const    table       = shard.table.load(std::memory_order_acquire);
            const size_type bucketIndex = BucketIndex(hash, table->bucketCount);
            Node* const     oldHead     = table->buckets[bucketIndex].load(std::memory_order_acquire);

            bool  inserted = FindNodeInChain(oldHead, key, hash) == nullptr;
            Node* newHead  = nullptr;
            if (inserted)
            {
                newHead = CloneChainWithPrepended(oldHead, hash, std::forward<K>(key), std::forward<V>(value));
            }
            else
            {
                auto replaceFactory = [&value](const Value&) -> Value {
                    return Value(std::forward<V>(value));
                };
                newHead = CloneChainReplacingValue(oldHead, key, hash, replaceFactory);
            }

            table->buckets[bucketIndex].store(newHead, std::memory_order_release);
            RetireChain(shard, oldHead);

            if (inserted)
            {
                ++shard.size;
                m_size.fetch_add(1, std::memory_order_acq_rel);
            }

            shard.reclaimer.Poll();
            return inserted;
        }

        template<class K, class V, class Updater>
        bool UpsertInternal(K&& key, V&& value, Updater&& updater)
        {
            const std::size_t               hash  = ComputeHash(key);
            Shard&                          shard = ShardForHash(hash);
            std::lock_guard<Sync::SpinLock> lock(shard.writeLock);

            EnsureShardCapacity(shard, shard.size + 1);

            Table* const    table       = shard.table.load(std::memory_order_acquire);
            const size_type bucketIndex = BucketIndex(hash, table->bucketCount);
            Node* const     oldHead     = table->buckets[bucketIndex].load(std::memory_order_acquire);

            const bool inserted = FindNodeInChain(oldHead, key, hash) == nullptr;
            Node*      newHead  = nullptr;
            if (inserted)
            {
                newHead = CloneChainWithPrepended(oldHead, hash, std::forward<K>(key), std::forward<V>(value));
                ++shard.size;
                m_size.fetch_add(1, std::memory_order_acq_rel);
            }
            else
            {
                auto replaceFactory = [&value, &updater](const Value& current) -> Value {
                    Value next = current;
                    std::invoke(updater, next, std::forward<V>(value));
                    return next;
                };
                newHead = CloneChainReplacingValue(oldHead, key, hash, replaceFactory);
            }

            table->buckets[bucketIndex].store(newHead, std::memory_order_release);
            RetireChain(shard, oldHead);
            shard.reclaimer.Poll();
            return inserted;
        }

        std::atomic<size_type> m_size {0};
        alignas(64) Shard m_shards[ShardCount] {};
        [[no_unique_address]] Hash      m_hash {};
        [[no_unique_address]] Equal     m_equal {};
        [[no_unique_address]] Allocator m_allocator {};
    };
}// namespace NGIN::Containers
