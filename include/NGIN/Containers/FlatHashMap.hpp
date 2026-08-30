/// @file FlatHashMap.hpp
/// @brief Header-only flat open-addressing hash map with allocator support and explicit lifetime management.
///
/// Semantics / constraints (performance-first):
/// - Capacity is always a power-of-two (>= 16); probing uses `hash & (capacity - 1)`.
/// - Deletion uses backward-shift (no tombstones). This can relocate entries, so:
///   - Any `Remove()` may invalidate iterators, pointers, and references (not just to the erased element).
/// - To keep `Remove()` robust and fast, `Key` and `Value` must be nothrow-move-constructible.
/// - Any `Rehash()`/growth invalidates all iterators, pointers, and references.

#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Memory/detail/CheckedArithmetic.hpp>
#include <NGIN/Primitives.hpp>

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace NGIN::Containers
{
    namespace detail
    {
        constexpr bool IsPowerOfTwo(std::size_t value) noexcept
        {
            return value && ((value & (value - 1)) == 0);
        }

        [[nodiscard]] constexpr bool TryNextPow2(std::size_t value, std::size_t& result) noexcept
        {
            if (value <= 1)
            {
                result = 1;
                return true;
            }

            constexpr int sizeBits = std::numeric_limits<std::size_t>::digits;
            const int     width    = std::bit_width(value - 1);
            if (width >= sizeBits)
                return false;
            result = std::size_t {1} << width;
            return true;
        }

        constexpr std::size_t Distance(std::size_t from, std::size_t to, std::size_t mask) noexcept
        {
            return (to - from) & mask;
        }
    }// namespace detail

    /// @brief Flat open-addressing hash map.
    ///
    /// @tparam Key Key type used for lookup and storage.
    /// @tparam Value Mapped value type.
    /// @tparam Hash Hash function for keys and compatible heterogeneous lookup keys.
    /// @tparam KeyEqual Equality predicate for keys and compatible heterogeneous lookup keys.
    /// @tparam AllocatorType Allocator used for bucket storage.
    ///
    /// Design notes:
    /// - Linear probing.
    /// - Backward-shift deletion (no tombstones).
    /// - Explicit lifetime storage: buckets do not default-construct keys/values.
    template<typename Key,
             typename Value,
             typename Hash                          = std::hash<Key>,
             typename KeyEqual                      = std::equal_to<Key>,
             Memory::AllocatorConcept AllocatorType = Memory::SystemAllocator>
    class FlatHashMap
    {
    public:
        using key_type       = Key;
        using mapped_type    = Value;
        using hash_type      = Hash;
        using key_equal      = KeyEqual;
        using allocator_type = AllocatorType;
        using size_type      = std::size_t;

        struct KeyValueRef;
        class Iterator;

        static constexpr double    kMaxLoadFactor   = 0.75;
        static constexpr size_type kInitialCapacity = 16;

        static_assert(std::is_nothrow_move_constructible_v<Key> && std::is_nothrow_move_constructible_v<Value>,
                      "FlatHashMap requires nothrow move constructible Key and Value (backward-shift deletion).");

        /// @brief Constructs an empty map with the default initial capacity.
        FlatHashMap() { Initialize_(kInitialCapacity); }

        /// @brief Constructs an empty map with explicit capacity, predicates, and allocator.
        explicit FlatHashMap(size_type            initialCapacity,
                             const Hash&          hash      = Hash {},
                             const KeyEqual&      equal     = KeyEqual {},
                             const AllocatorType& allocator = AllocatorType {})
            : m_hash(hash), m_equal(equal), m_allocator(allocator)
        {
            Initialize_(initialCapacity);
        }

        /// @brief Copies all entries and allocator state from another map.
        /// @throws std::bad_alloc When bucket allocation fails.
        /// @details Construction is transactional: a failed entry copy releases all partially constructed state.
        FlatHashMap(const FlatHashMap& other)
            requires(std::copy_constructible<Key> && std::copy_constructible<Value> &&
                     std::copy_constructible<Hash> && std::copy_constructible<KeyEqual> &&
                     std::copy_constructible<AllocatorType>)
            : m_hash(other.m_hash), m_equal(other.m_equal), m_allocator(other.m_allocator)
        {
            try
            {
                Initialize_(other.m_capacity);
                CopyEntriesFrom_(other);
            } catch (...)
            {
                ClearAndRelease_();
                throw;
            }
        }

        /// @brief Replaces this map with a copy of another map.
        /// @details Entry construction and allocation are completed before this map is modified.
        FlatHashMap& operator=(const FlatHashMap& other)
            requires(std::copy_constructible<Key> && std::copy_constructible<Value> &&
                     std::copy_constructible<Hash> && std::copy_constructible<KeyEqual> &&
                     std::copy_constructible<AllocatorType>)
        {
            if (this == &other)
                return *this;

            if constexpr (Memory::AllocatorPropagationTraits<AllocatorType>::PropagateOnCopyAssignment)
            {
                FlatHashMap replacement(other);
                SwapAll_(replacement);
            }
            else
            {
                FlatHashMap replacement(other.m_capacity, other.m_hash, other.m_equal, m_allocator);
                replacement.CopyEntriesFrom_(other);
                SwapContents_(replacement);
            }
            return *this;
        }

        /// @brief Transfers entries and allocator state from another map.
        FlatHashMap(FlatHashMap&& other) noexcept(
                std::is_nothrow_move_constructible_v<Hash> &&
                std::is_nothrow_move_constructible_v<KeyEqual> &&
                std::is_nothrow_move_constructible_v<AllocatorType>)
            : m_hash(std::move(other.m_hash)),
              m_equal(std::move(other.m_equal)),
              m_allocator(std::move(other.m_allocator)),
              m_buckets(other.m_buckets),
              m_capacity(other.m_capacity),
              m_mask(other.m_mask),
              m_size(other.m_size)
        {
            other.m_buckets  = nullptr;
            other.m_capacity = 0;
            other.m_mask     = 0;
            other.m_size     = 0;
        }

        /// @brief Replaces this map by transferring or relocating another map's entries.
        FlatHashMap& operator=(FlatHashMap&& other) noexcept(
                std::is_nothrow_move_assignable_v<Hash> &&
                std::is_nothrow_move_assignable_v<KeyEqual> &&
                ((!Memory::AllocatorPropagationTraits<AllocatorType>::PropagateOnMoveAssignment &&
                  Memory::AllocatorPropagationTraits<AllocatorType>::IsAlwaysEqual) ||
                 (Memory::AllocatorPropagationTraits<AllocatorType>::PropagateOnMoveAssignment &&
                  std::is_nothrow_move_assignable_v<AllocatorType>) ))
        {
            if (this == &other)
                return *this;

            if constexpr (Memory::AllocatorPropagationTraits<AllocatorType>::PropagateOnMoveAssignment)
            {
                ClearAndRelease_();
                m_hash      = std::move(other.m_hash);
                m_equal     = std::move(other.m_equal);
                m_allocator = std::move(other.m_allocator);
                m_buckets   = other.m_buckets;
                m_capacity  = other.m_capacity;
                m_mask      = other.m_mask;
                m_size      = other.m_size;

                other.m_buckets  = nullptr;
                other.m_capacity = 0;
                other.m_mask     = 0;
                other.m_size     = 0;
            }
            else if constexpr (Memory::AllocatorPropagationTraits<AllocatorType>::IsAlwaysEqual)
            {
                ClearAndRelease_();
                m_hash     = std::move(other.m_hash);
                m_equal    = std::move(other.m_equal);
                m_buckets  = other.m_buckets;
                m_capacity = other.m_capacity;
                m_mask     = other.m_mask;
                m_size     = other.m_size;

                other.m_buckets  = nullptr;
                other.m_capacity = 0;
                other.m_mask     = 0;
                other.m_size     = 0;
            }
            else
            {
                Clear();
                Reserve(other.m_size);
                for (Iterator it = other.begin(); it != other.end(); ++it)
                {
                    KeyValueRef kv = *it;
                    Insert(kv.key, std::move(kv.value));
                }
                other.Clear();
            }

            return *this;
        }

        /// @brief Destroys all entries and releases bucket storage.
        ~FlatHashMap() { ClearAndRelease_(); }

        //--------------------------------------------------------------------------
        // Core ops
        //--------------------------------------------------------------------------

        /// @brief Inserts a key-value pair or replaces the mapped value for an equivalent key.
        void Insert(const Key& key, const Value& value) { InsertImpl_(key, value); }
        /// @copydoc Insert(const Key&, const Value&)
        void Insert(const Key& key, Value&& value) { InsertImpl_(key, std::move(value)); }

        /// @brief Inserts or replaces an entry using compatible forwarded key and value types.
        template<class K, class V>
        void Insert(K&& key, V&& value)
        {
            InsertImpl_(std::forward<K>(key), std::forward<V>(value));
        }

        /// @brief Removes an equivalent key when present.
        ///
        /// Backward-shift deletion may invalidate every iterator, pointer, and reference into the map.
        void Remove(const Key& key) { RemoveImpl_(key); }

        /// @brief Removes an entry through heterogeneous lookup when the hash and equality types support it.
        template<class K>
            requires requires(const Hash& h, const KeyEqual& eq, const K& k, const Key& kk) { h(k); eq(k, kk); eq(kk, k); }
        void Remove(const K& key)
        {
            RemoveImpl_(key);
        }

        /// @brief Returns a copy of the value for a key.
        /// @throws std::out_of_range When the key is absent.
        [[nodiscard]] Value Get(const Key& key) const
        {
            const Value* p = GetPtr(key);
            if (!p)
                throw std::out_of_range("Key not found in hashmap");
            return *p;
        }

        /// @brief Returns a value copy through heterogeneous lookup.
        /// @throws std::out_of_range When the key is absent.
        template<class K>
            requires requires(const Hash& h, const KeyEqual& eq, const K& k, const Key& kk) { h(k); eq(k, kk); eq(kk, k); }
        [[nodiscard]] Value Get(const K& key) const
        {
            const Value* p = GetPtr(key);
            if (!p)
                throw std::out_of_range("Key not found in hashmap");
            return *p;
        }

        /// @brief Returns mutable access to the value for a key.
        /// @throws std::out_of_range When the key is absent.
        [[nodiscard]] Value& GetRef(const Key& key)
        {
            Value* p = GetPtr(key);
            if (!p)
                throw std::out_of_range("Key not found in hashmap");
            return *p;
        }

        /// @brief Returns read-only access to the value for a key.
        /// @throws std::out_of_range When the key is absent.
        [[nodiscard]] const Value& GetRef(const Key& key) const
        {
            const Value* p = GetPtr(key);
            if (!p)
                throw std::out_of_range("Key not found in hashmap");
            return *p;
        }

        /// @brief Returns mutable value access through heterogeneous lookup.
        template<class K>
            requires requires(const Hash& h, const KeyEqual& eq, const K& k, const Key& kk) { h(k); eq(k, kk); eq(kk, k); }
        [[nodiscard]] Value& GetRef(const K& key)
        {
            Value* p = GetPtr(key);
            if (!p)
                throw std::out_of_range("Key not found in hashmap");
            return *p;
        }

        /// @brief Returns read-only value access through heterogeneous lookup.
        template<class K>
            requires requires(const Hash& h, const KeyEqual& eq, const K& k, const Key& kk) { h(k); eq(k, kk); eq(kk, k); }
        [[nodiscard]] const Value& GetRef(const K& key) const
        {
            const Value* p = GetPtr(key);
            if (!p)
                throw std::out_of_range("Key not found in hashmap");
            return *p;
        }

        /// @brief Returns a pointer to the mapped value, or `nullptr` when absent.
        [[nodiscard]] Value* GetPtr(const Key& key) { return GetPtrImpl_(key); }
        /// @copydoc GetPtr(const Key&)
        [[nodiscard]] const Value* GetPtr(const Key& key) const { return GetPtrImpl_(key); }

        /// @brief Returns a mutable value pointer through heterogeneous lookup.
        template<class K>
            requires requires(const Hash& h, const KeyEqual& eq, const K& k, const Key& kk) { h(k); eq(k, kk); eq(kk, k); }
        [[nodiscard]] Value* GetPtr(const K& key)
        {
            return GetPtrImpl_(key);
        }

        /// @brief Returns a read-only value pointer through heterogeneous lookup.
        template<class K>
            requires requires(const Hash& h, const KeyEqual& eq, const K& k, const Key& kk) { h(k); eq(k, kk); eq(kk, k); }
        [[nodiscard]] const Value* GetPtr(const K& key) const
        {
            return GetPtrImpl_(key);
        }

        /// @brief Returns whether an equivalent key exists.
        [[nodiscard]] bool Contains(const Key& key) const { return GetPtr(key) != nullptr; }

        /// @brief Returns whether a compatible heterogeneous key exists.
        template<class K>
            requires requires(const Hash& h, const KeyEqual& eq, const K& k, const Key& kk) { h(k); eq(k, kk); eq(kk, k); }
        [[nodiscard]] bool Contains(const K& key) const
        {
            return GetPtr(key) != nullptr;
        }

        /// @brief Destroys every entry while retaining bucket capacity.
        void Clear()
        {
            if (!m_buckets)
                return;
            for (size_type i = 0; i < m_capacity; ++i)
            {
                if (m_buckets[i].occupied)
                {
                    DestroyAt_(i);
                    m_buckets[i].occupied = false;
                }
            }
            m_size = 0;
        }

        //--------------------------------------------------------------------------
        // Capacity
        //--------------------------------------------------------------------------

        /// @brief Returns the number of stored entries.
        [[nodiscard]] NGIN_ALWAYS_INLINE UIntSize Size() const { return static_cast<UIntSize>(m_size); }
        /// @brief Returns the number of allocated buckets.
        [[nodiscard]] NGIN_ALWAYS_INLINE UIntSize Capacity() const { return static_cast<UIntSize>(m_capacity); }

        /// @brief Ensures capacity for at least `count` entries without growth.
        /// @throws std::length_error When the requested capacity cannot be represented.
        /// @throws std::bad_alloc When bucket allocation fails.
        void Reserve(UIntSize count)
        {
            const size_type buckets = MinimumBucketsForEntries_(static_cast<size_type>(count));
            if (buckets <= m_capacity)
                return;
            Rehash(static_cast<UIntSize>(buckets));
        }

        /// @brief Rebuilds the table with at least the requested number of buckets.
        ///
        /// This operation invalidates every iterator, pointer, and reference into the map.
        /// It temporarily retains both bucket arrays, but does not invoke the user hash or equality functions.
        /// @throws std::length_error When the requested bucket count cannot be represented.
        /// @throws std::bad_alloc When bucket allocation fails.
        void Rehash(UIntSize newBucketCount)
        {
            const size_type minimumForEntries = MinimumBucketsForEntries_(m_size);
            const size_type requested         = (std::max) (static_cast<size_type>(newBucketCount), minimumForEntries);
            const size_type target            = NormalizeBucketCount_(requested);
            if (target == m_capacity)
                return;

            const size_type bytes      = BucketBytes_(target);
            Bucket* const   newBuckets = AllocateBuckets_(target);
            try
            {
                const size_type newMask = target - 1;
                for (size_type index = 0; index < m_capacity; ++index)
                {
                    if (!m_buckets[index].occupied)
                        continue;

                    const std::size_t hash        = m_buckets[index].hash;
                    const size_type   destination = FindEmptySlot_(newBuckets, newMask, hash);
                    ConstructBucket_(
                            newBuckets[destination],
                            hash,
                            std::move(KeyRef_(index)),
                            std::move(ValueRef_(index)));
                }
            } catch (...)
            {
                DestroyAndDeallocateBuckets_(newBuckets, target, bytes);
                throw;
            }

            Bucket* const   oldBuckets  = m_buckets;
            const size_type oldCapacity = m_capacity;
            m_buckets                   = newBuckets;
            m_capacity                  = target;
            m_mask                      = target - 1;
            DestroyAndDeallocateBuckets_(oldBuckets, oldCapacity, BucketBytes_(oldCapacity));
        }

        //--------------------------------------------------------------------------
        // operator[]
        //--------------------------------------------------------------------------

        /// @brief Returns a mapped value, inserting a default value when the key is absent.
        Value& operator[](const Key& key)
            requires std::default_initializable<Value>
        {
            if (Value* p = GetPtr(key))
                return *p;
            Insert(key, Value {});
            return *GetPtr(key);
        }

        /// @brief Returns a mapped value without insertion.
        /// @throws std::out_of_range When the key is absent.
        const Value& operator[](const Key& key) const
        {
            return GetRef(key);
        }

        //--------------------------------------------------------------------------
        // Iteration
        //--------------------------------------------------------------------------

        /// @brief Mutable key-value reference returned by Iterator.
        struct KeyValueRef
        {
            const Key& key;
            Value&     value;
        };

        /// @brief Read-only key-value reference returned by ConstIterator.
        struct KeyValueConstRef
        {
            const Key&   key;
            const Value& value;
        };

        /// @brief Forward iterator over occupied map entries.
        class Iterator
        {
        public:
            using difference_type   = std::ptrdiff_t;
            using value_type        = KeyValueRef;
            using reference         = KeyValueRef;
            using pointer           = void;
            using iterator_category = std::forward_iterator_tag;

            /// @brief Constructs an unbound iterator.
            Iterator() = default;
            /// @brief Constructs an iterator at a bucket index and advances to an occupied entry.
            Iterator(FlatHashMap* map, size_type idx) : m_map(map), m_index(idx) { Advance_(); }

            /// @brief Returns references to the current key and mapped value.
            reference operator*() const { return {m_map->KeyRef_(m_index), m_map->ValueRef_(m_index)}; }

            /// @brief Advances to the next occupied entry.
            Iterator& operator++()
            {
                ++m_index;
                Advance_();
                return *this;
            }

            /// @brief Compares iterator ownership and position.
            bool operator==(const Iterator& other) const { return m_map == other.m_map && m_index == other.m_index; }
            /// @brief Returns whether iterator ownership or position differs.
            bool operator!=(const Iterator& other) const { return !(*this == other); }

        private:
            void Advance_()
            {
                if (!m_map)
                    return;
                while (m_index < m_map->m_capacity && !m_map->m_buckets[m_index].occupied)
                    ++m_index;
            }

            FlatHashMap* m_map {nullptr};
            size_type    m_index {0};
        };

        /// @brief Read-only forward iterator over occupied map entries.
        class ConstIterator
        {
        public:
            using difference_type   = std::ptrdiff_t;
            using value_type        = KeyValueConstRef;
            using reference         = KeyValueConstRef;
            using pointer           = void;
            using iterator_category = std::forward_iterator_tag;

            /// @brief Constructs an unbound read-only iterator.
            ConstIterator() = default;
            /// @brief Constructs an iterator at a bucket index and advances to an occupied entry.
            ConstIterator(const FlatHashMap* map, size_type idx) : m_map(map), m_index(idx) { Advance_(); }

            /// @brief Returns read-only references to the current key and mapped value.
            reference operator*() const { return {m_map->KeyRef_(m_index), m_map->ValueRef_(m_index)}; }

            /// @brief Advances to the next occupied entry.
            ConstIterator& operator++()
            {
                ++m_index;
                Advance_();
                return *this;
            }

            /// @brief Compares iterator ownership and position.
            bool operator==(const ConstIterator& other) const { return m_map == other.m_map && m_index == other.m_index; }
            /// @brief Returns whether iterator ownership or position differs.
            bool operator!=(const ConstIterator& other) const { return !(*this == other); }

        private:
            void Advance_()
            {
                if (!m_map)
                    return;
                while (m_index < m_map->m_capacity && !m_map->m_buckets[m_index].occupied)
                    ++m_index;
            }

            const FlatHashMap* m_map {nullptr};
            size_type          m_index {0};
        };

        /// @brief Returns an iterator to the first occupied entry.
        Iterator Begin() { return Iterator(this, 0); }
        /// @brief Returns the mutable end iterator.
        Iterator End() { return Iterator(this, m_capacity); }
        /// @brief Returns a read-only iterator to the first occupied entry.
        ConstIterator Begin() const { return ConstIterator(this, 0); }
        /// @brief Returns the read-only end iterator.
        ConstIterator End() const { return ConstIterator(this, m_capacity); }
        /// @brief Returns a read-only iterator to the first occupied entry.
        ConstIterator CBegin() const { return ConstIterator(this, 0); }
        /// @brief Returns the read-only end iterator.
        ConstIterator CEnd() const { return ConstIterator(this, m_capacity); }

        /// @brief Standard-library-compatible spelling of Begin().
        Iterator begin() { return Begin(); }
        /// @brief Standard-library-compatible spelling of End().
        Iterator end() { return End(); }
        /// @brief Standard-library-compatible read-only spelling of Begin().
        ConstIterator begin() const { return Begin(); }
        /// @brief Standard-library-compatible read-only spelling of End().
        ConstIterator end() const { return End(); }
        /// @brief Standard-library-compatible spelling of CBegin().
        ConstIterator cbegin() const { return CBegin(); }
        /// @brief Standard-library-compatible spelling of CEnd().
        ConstIterator cend() const { return CEnd(); }

    private:
        struct Bucket
        {
            std::size_t hash;
            bool        occupied;

            alignas(Key) std::byte keyStorage[sizeof(Key)];
            alignas(Value) std::byte valueStorage[sizeof(Value)];
        };

        static_assert(std::is_trivially_default_constructible_v<Bucket>);

        [[nodiscard]] static Key& KeyRef_(Bucket* buckets, size_type idx) noexcept
        {
            return *std::launder(reinterpret_cast<Key*>(buckets[idx].keyStorage));
        }

        [[nodiscard]] static const Key& KeyRef_(const Bucket* buckets, size_type idx) noexcept
        {
            return *std::launder(reinterpret_cast<const Key*>(buckets[idx].keyStorage));
        }

        [[nodiscard]] static Value& ValueRef_(Bucket* buckets, size_type idx) noexcept
        {
            return *std::launder(reinterpret_cast<Value*>(buckets[idx].valueStorage));
        }

        [[nodiscard]] static const Value& ValueRef_(const Bucket* buckets, size_type idx) noexcept
        {
            return *std::launder(reinterpret_cast<const Value*>(buckets[idx].valueStorage));
        }

        [[nodiscard]] Key&         KeyRef_(size_type idx) noexcept { return KeyRef_(m_buckets, idx); }
        [[nodiscard]] const Key&   KeyRef_(size_type idx) const noexcept { return KeyRef_(m_buckets, idx); }
        [[nodiscard]] Value&       ValueRef_(size_type idx) noexcept { return ValueRef_(m_buckets, idx); }
        [[nodiscard]] const Value& ValueRef_(size_type idx) const noexcept { return ValueRef_(m_buckets, idx); }

        void DestroyAt_(Bucket* buckets, size_type idx) noexcept
        {
            if constexpr (!std::is_trivially_destructible_v<Value>)
            {
                ValueRef_(buckets, idx).~Value();
            }
            if constexpr (!std::is_trivially_destructible_v<Key>)
            {
                KeyRef_(buckets, idx).~Key();
            }
            buckets[idx].hash     = 0;
            buckets[idx].occupied = false;
        }

        void DestroyAt_(size_type idx) noexcept { DestroyAt_(m_buckets, idx); }

        template<class K, class V>
        void ConstructBucket_(Bucket& bucket, const std::size_t hash, K&& key, V&& value)
        {
            ::new (static_cast<void*>(bucket.keyStorage)) Key(std::forward<K>(key));
            try
            {
                ::new (static_cast<void*>(bucket.valueStorage)) Value(std::forward<V>(value));
            } catch (...)
            {
                std::destroy_at(std::launder(reinterpret_cast<Key*>(bucket.keyStorage)));
                throw;
            }

            // Occupancy is the publication flag: both object lifetimes begin before it becomes true.
            bucket.hash     = hash;
            bucket.occupied = true;
        }

        void ClearAndRelease_() noexcept
        {
            if (!m_buckets)
                return;
            for (size_type i = 0; i < m_capacity; ++i)
            {
                if (m_buckets[i].occupied)
                    DestroyAt_(i);
            }
            DeallocateBuckets_(m_buckets, m_capacity);
            m_buckets  = nullptr;
            m_capacity = 0;
            m_mask     = 0;
            m_size     = 0;
        }

        [[nodiscard]] static size_type BucketBytes_(const size_type capacity)
        {
            size_type bytes = 0;
            if (!Memory::detail::CheckedMultiply(capacity, sizeof(Bucket), bytes))
                throw std::length_error("FlatHashMap bucket storage exceeds addressable size");
            return bytes;
        }

        [[nodiscard]] static size_type NormalizeBucketCount_(const size_type requested)
        {
            const size_type minimum = (std::max) (requested, kInitialCapacity);
            size_type       result  = 0;
            if (!detail::TryNextPow2(minimum, result) || result > (std::numeric_limits<size_type>::max)() / sizeof(Bucket))
                throw std::length_error("FlatHashMap bucket count exceeds addressable size");
            return result;
        }

        [[nodiscard]] static size_type MinimumBucketsForEntries_(const size_type entries)
        {
            const size_type quotient  = entries / 3;
            const size_type remainder = entries % 3;
            size_type       buckets   = 0;
            if (!Memory::detail::CheckedAdd(entries, quotient, buckets) ||
                (remainder != 0 && !Memory::detail::CheckedAdd(buckets, size_type {1}, buckets)))
            {
                throw std::length_error("FlatHashMap entry capacity exceeds addressable size");
            }
            return NormalizeBucketCount_(buckets);
        }

        [[nodiscard]] Bucket* AllocateBuckets_(const size_type capacity)
        {
            const size_type bytes  = BucketBytes_(capacity);
            void* const     memory = m_allocator.Allocate(bytes, alignof(Bucket));
            if (!memory)
                throw std::bad_alloc();
            std::memset(memory, 0, bytes);
            return static_cast<Bucket*>(memory);
        }

        void DeallocateBuckets_(Bucket* buckets, const size_type capacity) noexcept
        {
            if (!buckets)
                return;
            const size_type bytes = capacity * sizeof(Bucket);
            m_allocator.Deallocate(buckets, bytes, alignof(Bucket));
        }

        void DestroyAndDeallocateBuckets_(Bucket* buckets, const size_type capacity, const size_type bytes) noexcept
        {
            if (!buckets)
                return;
            for (size_type index = 0; index < capacity; ++index)
            {
                if (buckets[index].occupied)
                    DestroyAt_(buckets, index);
            }
            m_allocator.Deallocate(buckets, bytes, alignof(Bucket));
        }

        void Initialize_(size_type requestedCapacity)
        {
            const size_type cap = NormalizeBucketCount_(requestedCapacity);
            m_buckets           = AllocateBuckets_(cap);
            m_capacity          = cap;
            m_mask              = cap - 1;
            m_size              = 0;
        }

        [[nodiscard]] bool NeedsGrowthForInsert_() const noexcept
        {
            return m_capacity == 0 || m_size >= (m_capacity - (m_capacity / 4));
        }

        template<class K>
        [[nodiscard]] std::size_t ComputeHash_(const K& key) const
        {
            return static_cast<std::size_t>(m_hash(key));
        }

        template<class K>
        [[nodiscard]] size_type FindIndex_(const K& key, const std::size_t hash) const
        {
            if (!m_buckets || m_capacity == 0)
                return kNotFound;
            size_type index = hash & m_mask;
            for (size_type probed = 0; probed < m_capacity; ++probed)
            {
                const Bucket& bucket = m_buckets[index];
                if (!bucket.occupied)
                    return kNotFound;
                if (bucket.hash == hash && m_equal(KeyRef_(index), key))
                    return index;
                index = (index + 1) & m_mask;
            }
            return kNotFound;
        }

        template<class K>
        [[nodiscard]] size_type FindInsertSlot_(const K& key, const std::size_t hash) const
        {
            size_type index = hash & m_mask;
            for (size_type probed = 0; probed < m_capacity; ++probed)
            {
                const Bucket& bucket = m_buckets[index];
                if (!bucket.occupied)
                    return index;
                if (bucket.hash == hash && m_equal(KeyRef_(index), key))
                    return index;
                index = (index + 1) & m_mask;
            }
            return kNotFound;
        }

        [[nodiscard]] static size_type FindEmptySlot_(
                const Bucket*     buckets,
                const size_type   mask,
                const std::size_t hash) noexcept
        {
            size_type index = hash & mask;
            while (buckets[index].occupied)
                index = (index + 1) & mask;
            return index;
        }

        template<class K>
        [[nodiscard]] Value* GetPtrImpl_(const K& key) const
        {
            const std::size_t hash  = ComputeHash_(key);
            const size_type   index = FindIndex_(key, hash);
            if (index == kNotFound)
                return nullptr;
            return const_cast<Value*>(&ValueRef_(index));
        }

        template<class K, class V>
        void InsertImpl_(K&& key, V&& value)
        {
            const std::size_t hash  = ComputeHash_(key);
            const size_type   index = FindInsertSlot_(key, hash);
            if (index != kNotFound && m_buckets[index].occupied)
            {
                ValueRef_(index) = std::forward<V>(value);
                return;
            }

            if (NeedsGrowthForInsert_() || index == kNotFound)
            {
                Key   stagedKey(std::forward<K>(key));
                Value stagedValue(std::forward<V>(value));
                Rehash(GrownCapacity_());
                const size_type destination = FindEmptySlot_(m_buckets, m_mask, hash);
                ConstructBucket_(m_buckets[destination], hash, std::move(stagedKey), std::move(stagedValue));
                ++m_size;
                return;
            }

            ConstructBucket_(m_buckets[index], hash, std::forward<K>(key), std::forward<V>(value));
            ++m_size;
        }

        void InsertExistingCopy_(const std::size_t hash, const Key& key, const Value& value)
            requires(std::copy_constructible<Key> && std::copy_constructible<Value>)
        {
            const size_type index = FindEmptySlot_(m_buckets, m_mask, hash);
            ConstructBucket_(m_buckets[index], hash, key, value);
            ++m_size;
        }

        void CopyEntriesFrom_(const FlatHashMap& other)
            requires(std::copy_constructible<Key> && std::copy_constructible<Value>)
        {
            for (size_type index = 0; index < other.m_capacity; ++index)
            {
                if (!other.m_buckets[index].occupied)
                    continue;
                InsertExistingCopy_(other.m_buckets[index].hash, other.KeyRef_(index), other.ValueRef_(index));
            }
        }

        [[nodiscard]] size_type GrownCapacity_() const
        {
            size_type target = 0;
            if (!Memory::detail::CheckedAdd(m_capacity, m_capacity, target))
                throw std::length_error("FlatHashMap growth exceeds addressable size");
            return NormalizeBucketCount_((std::max) (target, kInitialCapacity));
        }

        void SwapContents_(FlatHashMap& other) noexcept(
                std::is_nothrow_swappable_v<Hash> && std::is_nothrow_swappable_v<KeyEqual>)
        {
            using std::swap;
            swap(m_hash, other.m_hash);
            swap(m_equal, other.m_equal);
            swap(m_buckets, other.m_buckets);
            swap(m_capacity, other.m_capacity);
            swap(m_mask, other.m_mask);
            swap(m_size, other.m_size);
        }

        void SwapAll_(FlatHashMap& other) noexcept(
                noexcept(SwapContents_(other)) && std::is_nothrow_swappable_v<AllocatorType>)
        {
            using std::swap;
            SwapContents_(other);
            try
            {
                swap(m_allocator, other.m_allocator);
            } catch (...)
            {
                SwapContents_(other);
                throw;
            }
        }

        template<class K>
        void RemoveImpl_(const K& key)
        {
            const std::size_t hash  = ComputeHash_(key);
            const size_type   index = FindIndex_(key, hash);
            if (index == kNotFound)
                return;

            DestroyAt_(index);
            --m_size;
            BackwardShiftFrom_(index);
        }

        void BackwardShiftFrom_(size_type holeIndex) noexcept
        {
            size_type hole = holeIndex;
            size_type next = (hole + 1) & m_mask;

            while (m_buckets[next].occupied)
            {
                const size_type home           = m_buckets[next].hash & m_mask;
                const size_type distHomeToNext = detail::Distance(home, next, m_mask);
                const size_type distHomeToHole = detail::Distance(home, hole, m_mask);

                if (distHomeToHole < distHomeToNext)
                {
                    MoveBucket_(hole, next);
                    hole = next;
                }
                next = (next + 1) & m_mask;
            }
        }

        void MoveBucket_(size_type dst, size_type src) noexcept
        {
            Bucket& d = m_buckets[dst];
            Bucket& s = m_buckets[src];

            ::new (static_cast<void*>(d.keyStorage)) Key(std::move(KeyRef_(src)));
            ::new (static_cast<void*>(d.valueStorage)) Value(std::move(ValueRef_(src)));

            d.hash     = s.hash;
            d.occupied = true;

            DestroyAt_(src);
        }

        static constexpr size_type kNotFound = static_cast<size_type>(-1);

        NGIN_NO_UNIQUE_ADDRESS Hash          m_hash {};
        NGIN_NO_UNIQUE_ADDRESS KeyEqual      m_equal {};
        NGIN_NO_UNIQUE_ADDRESS AllocatorType m_allocator {};

        Bucket*   m_buckets {nullptr};
        size_type m_capacity {0};
        size_type m_mask {0};
        size_type m_size {0};
    };

}// namespace NGIN::Containers
