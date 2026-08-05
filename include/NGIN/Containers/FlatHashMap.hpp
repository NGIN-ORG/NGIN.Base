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
#include <NGIN/Primitives.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
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

        constexpr std::size_t NextPow2(std::size_t value) noexcept
        {
            if (value <= 1)
                return 1;
            return std::bit_ceil(value);
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
        FlatHashMap(const FlatHashMap& other)
            : m_hash(other.m_hash), m_equal(other.m_equal), m_allocator(other.m_allocator)
        {
            Initialize_(other.m_capacity);
            for (size_type i = 0; i < other.m_capacity; ++i)
            {
                if (!other.m_buckets[i].occupied)
                    continue;
                InsertExisting_(other.m_buckets[i].hash, other.KeyRef_(i), other.ValueRef_(i));
            }
        }

        /// @brief Replaces this map with a copy of another map.
        FlatHashMap& operator=(const FlatHashMap& other)
        {
            if (this == &other)
                return *this;

            ClearAndRelease_();

            if constexpr (Memory::AllocatorPropagationTraits<AllocatorType>::PropagateOnCopyAssignment)
            {
                m_allocator = other.m_allocator;
            }

            m_hash  = other.m_hash;
            m_equal = other.m_equal;

            Initialize_(other.m_capacity);
            for (size_type i = 0; i < other.m_capacity; ++i)
            {
                if (!other.m_buckets[i].occupied)
                    continue;
                InsertExisting_(other.m_buckets[i].hash, other.KeyRef_(i), other.ValueRef_(i));
            }
            return *this;
        }

        /// @brief Transfers entries and allocator state from another map.
        FlatHashMap(FlatHashMap&& other) noexcept
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
        FlatHashMap& operator=(FlatHashMap&& other) noexcept
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
                for (auto it = other.begin(); it != other.end(); ++it)
                {
                    auto kv = *it;
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
        [[nodiscard]] Value*       GetPtr(const Key& key) noexcept { return GetPtrImpl_(key); }
        /// @copydoc GetPtr(const Key&)
        [[nodiscard]] const Value* GetPtr(const Key& key) const noexcept { return GetPtrImpl_(key); }

        /// @brief Returns a mutable value pointer through heterogeneous lookup.
        template<class K>
            requires requires(const Hash& h, const KeyEqual& eq, const K& k, const Key& kk) { h(k); eq(k, kk); eq(kk, k); }
        [[nodiscard]] Value* GetPtr(const K& key) noexcept
        {
            return GetPtrImpl_(key);
        }

        /// @brief Returns a read-only value pointer through heterogeneous lookup.
        template<class K>
            requires requires(const Hash& h, const KeyEqual& eq, const K& k, const Key& kk) { h(k); eq(k, kk); eq(kk, k); }
        [[nodiscard]] const Value* GetPtr(const K& key) const noexcept
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
        void Reserve(UIntSize count)
        {
            const auto desired = static_cast<double>(count) / kMaxLoadFactor;
            size_type  buckets = static_cast<size_type>(desired) + 1;
            if (buckets < kInitialCapacity)
                buckets = kInitialCapacity;
            buckets = detail::NextPow2(buckets);
            if (buckets <= m_capacity)
                return;
            Rehash(static_cast<UIntSize>(buckets));
        }

        /// @brief Rebuilds the table with at least the requested number of buckets.
        ///
        /// This operation invalidates every iterator, pointer, and reference into the map.
        void Rehash(UIntSize newBucketCount)
        {
            size_type target = detail::NextPow2(static_cast<size_type>(newBucketCount));
            if (target < kInitialCapacity)
                target = kInitialCapacity;
            if (target == m_capacity)
                return;

            Bucket*   oldBuckets  = m_buckets;
            size_type oldCapacity = m_capacity;

            m_buckets  = nullptr;
            m_capacity = 0;
            m_mask     = 0;
            m_size     = 0;
            Initialize_(target);

            if (oldBuckets)
            {
                for (size_type i = 0; i < oldCapacity; ++i)
                {
                    if (!oldBuckets[i].occupied)
                        continue;
                    const auto h = oldBuckets[i].hash;
                    InsertExisting_(h, KeyRef_(oldBuckets, i), ValueRef_(oldBuckets, i));
                    DestroyAt_(oldBuckets, i);
                }
                DeallocateBuckets_(oldBuckets, oldCapacity);
            }
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
        Iterator      Begin() { return Iterator(this, 0); }
        /// @brief Returns the mutable end iterator.
        Iterator      End() { return Iterator(this, m_capacity); }
        /// @brief Returns a read-only iterator to the first occupied entry.
        ConstIterator Begin() const { return ConstIterator(this, 0); }
        /// @brief Returns the read-only end iterator.
        ConstIterator End() const { return ConstIterator(this, m_capacity); }
        /// @brief Returns a read-only iterator to the first occupied entry.
        ConstIterator CBegin() const { return ConstIterator(this, 0); }
        /// @brief Returns the read-only end iterator.
        ConstIterator CEnd() const { return ConstIterator(this, m_capacity); }

        /// @brief Standard-library-compatible spelling of Begin().
        Iterator      begin() { return Begin(); }
        /// @brief Standard-library-compatible spelling of End().
        Iterator      end() { return End(); }
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

        [[nodiscard]] Bucket* AllocateBuckets_(size_type capacity)
        {
            const auto bytes = capacity * sizeof(Bucket);
            void*      mem   = m_allocator.Allocate(bytes, alignof(Bucket));
            if (!mem)
                throw std::bad_alloc();
            std::memset(mem, 0, bytes);
            return static_cast<Bucket*>(mem);
        }

        void DeallocateBuckets_(Bucket* buckets, size_type capacity) noexcept
        {
            const auto bytes = capacity * sizeof(Bucket);
            m_allocator.Deallocate(buckets, bytes, alignof(Bucket));
        }

        void Initialize_(size_type requestedCapacity)
        {
            size_type cap = detail::NextPow2(requestedCapacity);
            if (cap < kInitialCapacity)
                cap = kInitialCapacity;
            if (!detail::IsPowerOfTwo(cap))
                cap = detail::NextPow2(cap);

            m_buckets  = AllocateBuckets_(cap);
            m_capacity = cap;
            m_mask     = cap - 1;
            m_size     = 0;
        }

        [[nodiscard]] double LoadFactor_() const noexcept
        {
            if (m_capacity == 0)
                return 0.0;
            return static_cast<double>(m_size) / static_cast<double>(m_capacity);
        }

        void MaybeGrow_()
        {
            if (LoadFactor_() <= kMaxLoadFactor)
                return;
            Rehash(static_cast<UIntSize>(m_capacity * 2));
        }

        template<class K>
        [[nodiscard]] std::size_t ComputeHash_(const K& key) const
        {
            return static_cast<std::size_t>(m_hash(key));
        }

        template<class K>
        [[nodiscard]] size_type FindIndex_(const K& key, std::size_t h) const noexcept
        {
            if (!m_buckets || m_capacity == 0)
                return kNotFound;
            size_type index = h & m_mask;
            for (size_type probed = 0; probed < m_capacity; ++probed)
            {
                const Bucket& b = m_buckets[index];
                if (!b.occupied)
                    return kNotFound;
                if (b.hash == h && m_equal(KeyRef_(index), key))
                    return index;
                index = (index + 1) & m_mask;
            }
            return kNotFound;
        }

        template<class K>
        [[nodiscard]] size_type FindInsertSlot_(const K& key, std::size_t h) const noexcept
        {
            size_type index = h & m_mask;
            for (size_type probed = 0; probed < m_capacity; ++probed)
            {
                const Bucket& b = m_buckets[index];
                if (!b.occupied)
                    return index;
                if (b.hash == h && m_equal(KeyRef_(index), key))
                    return index;
                index = (index + 1) & m_mask;
            }
            return kNotFound;
        }

        template<class K>
        [[nodiscard]] Value* GetPtrImpl_(const K& key) const noexcept
        {
            const auto h   = ComputeHash_(key);
            const auto idx = FindIndex_(key, h);
            if (idx == kNotFound)
                return nullptr;
            return const_cast<Value*>(&ValueRef_(idx));
        }

        template<class K, class V>
        void InsertImpl_(K&& key, V&& value)
        {
            MaybeGrow_();

            const auto h   = ComputeHash_(key);
            const auto idx = FindInsertSlot_(key, h);
            if (idx == kNotFound)
            {
                Rehash(static_cast<UIntSize>((std::max) (kInitialCapacity, m_capacity * 2)));
                InsertImpl_(std::forward<K>(key), std::forward<V>(value));
                return;
            }

            Bucket& b = m_buckets[idx];
            if (b.occupied)
            {
                ValueRef_(idx) = std::forward<V>(value);
                return;
            }

            b.hash     = h;
            b.occupied = true;

            ::new (static_cast<void*>(b.keyStorage)) Key(std::forward<K>(key));
            try
            {
                ::new (static_cast<void*>(b.valueStorage)) Value(std::forward<V>(value));
            } catch (...)
            {
                KeyRef_(idx).~Key();
                b.hash     = 0;
                b.occupied = false;
                throw;
            }

            ++m_size;
        }

        void InsertExisting_(std::size_t h, const Key& key, const Value& value)
        {
            const auto idx = FindInsertSlot_(key, h);
            if (idx == kNotFound)
                throw std::bad_alloc();

            Bucket& b  = m_buckets[idx];
            b.hash     = h;
            b.occupied = true;
            ::new (static_cast<void*>(b.keyStorage)) Key(key);
            try
            {
                ::new (static_cast<void*>(b.valueStorage)) Value(value);
            } catch (...)
            {
                KeyRef_(idx).~Key();
                b.hash     = 0;
                b.occupied = false;
                throw;
            }
            ++m_size;
        }

        template<class K>
        void RemoveImpl_(const K& key)
        {
            const auto h   = ComputeHash_(key);
            size_type  idx = FindIndex_(key, h);
            if (idx == kNotFound)
                return;

            DestroyAt_(idx);
            --m_size;
            BackwardShiftFrom_(idx);
        }

        void BackwardShiftFrom_(size_type holeIndex) noexcept
        {
            size_type hole = holeIndex;
            size_type next = (hole + 1) & m_mask;

            while (m_buckets[next].occupied)
            {
                const size_type home           = m_buckets[next].hash & m_mask;
                const auto      distHomeToNext = detail::Distance(home, next, m_mask);
                const auto      distHomeToHole = detail::Distance(home, hole, m_mask);

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

            d.hash     = s.hash;
            d.occupied = true;

            ::new (static_cast<void*>(d.keyStorage)) Key(std::move(KeyRef_(src)));
            ::new (static_cast<void*>(d.valueStorage)) Value(std::move(ValueRef_(src)));

            DestroyAt_(src);
        }

        static constexpr size_type kNotFound = static_cast<size_type>(-1);

        [[no_unique_address]] Hash          m_hash {};
        [[no_unique_address]] KeyEqual      m_equal {};
        [[no_unique_address]] AllocatorType m_allocator {};

        Bucket*   m_buckets {nullptr};
        size_type m_capacity {0};
        size_type m_mask {0};
        size_type m_size {0};
    };

}// namespace NGIN::Containers
