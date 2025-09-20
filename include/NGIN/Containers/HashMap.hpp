/// @file FlatHashMap.hpp
/// @brief Header-only flat open-addressing hashmap with growth support.

#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Containers/Vector.hpp>

#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>
#include <algorithm>
#include <type_traits>
#include <iterator>

namespace NGIN::Containers
{
    /// @brief Flat hashmap implementation with dynamic growth.
    /// Linear probing + backward-shift deletion (no tombstones).
    template<typename Key, typename Value, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
        requires requires { Value(); }// TODO: Replace with a proper concept
    class FlatHashMap
    {

    public:
        /// @brief Constructs a new FlatHashMap with the specified initial capacity.
        FlatHashMap();

        /// @brief Inserts or updates a key-value pair.
        void Insert(const Key& key, const Value& value);

        /// @brief Inserts or updates a key-value pair.
        void Insert(const Key& key, Value&& value);

        /// @brief Removes a key-value pair by its key.
        void Remove(const Key& key);

        /// @brief Retrieves the value associated with the key.
        /// @param key The key to search for.
        /// @return The value if found, throws std::out_of_range otherwise.
        Value Get(const Key& key) const;

        Value& operator[](const Key& key);

        const Value& operator[](const Key& key) const;

        /// @brief Reference-returning lookup; throws if not found.
        Value& GetRef(const Key& key);

        /// @brief Reference-returning lookup; throws if not found.
        const Value& GetRef(const Key& key) const;

        /// @brief Pointer-returning lookup for no-throw access. Returns nullptr if missing.
        Value* GetPtr(const Key& key) noexcept;

        /// @brief Pointer-returning lookup for no-throw access. Returns nullptr if missing.
        const Value* GetPtr(const Key& key) const noexcept;

        /// @brief Checks if the key exists in the hashmap.
        bool Contains(const Key& key) const;

        // Heterogeneous lookup (enabled when Hash/KeyEqual are transparent)
        template<class K>
            requires requires(const Hash& h, const KeyEqual& eq, const K& k, const Key& kk) {
                h(k);
                eq(k, kk);
                eq(kk, k);
            }
        bool Contains(const K& key) const;

        template<class K>
            requires requires(const Hash& h, const KeyEqual& eq, const K& k, const Key& kk) {
                h(k);
                eq(k, kk);
                eq(kk, k);
            }
        const Value* GetPtr(const K& key) const noexcept;

        template<class K>
            requires requires(const Hash& h, const KeyEqual& eq, const K& k, const Key& kk) {
                h(k);
                eq(k, kk);
                eq(kk, k);
            }
        Value* GetPtr(const K& key) noexcept;

        /// @brief Clears all entries in the hashmap.
        void Clear();

        /// @brief Returns the current number of elements in the hashmap.
        NGIN_ALWAYS_INLINE UIntSize Size() const
        {
            return m_size;
        }

        /// @brief Returns the current capacity of the hashmap.
        NGIN_ALWAYS_INLINE UIntSize Capacity() const
        {
            return m_table.Capacity();
        }

        /// @brief Ensure capacity for at least `count` elements without rehash during insert.
        void Reserve(UIntSize count);

        /// @brief Rehash to a specific bucket count (rounded to power-of-two, >= m_size / loadFactor).
        void Rehash(UIntSize newBucketCount);

        // === Iterators over occupied entries ===
        struct KeyValueRef
        {
            const Key& key;
            Value&     value;
        };
        struct KeyValueConstRef
        {
            const Key&   key;
            const Value& value;
        };

        class Iterator
        {
        public:
            using difference_type   = std::ptrdiff_t;
            using value_type        = KeyValueRef;
            using reference         = KeyValueRef;
            using pointer           = void;
            using iterator_category = std::forward_iterator_tag;

            Iterator() = default;
            Iterator(FlatHashMap* map, UIntSize idx) : m_map(map), m_index(idx) { AdvanceToOccupied(); }

            reference operator*() const { return {m_map->m_table[m_index].key, m_map->m_table[m_index].value}; }

            Iterator& operator++()
            {
                ++m_index;
                AdvanceToOccupied();
                return *this;
            }
            bool operator==(const Iterator& other) const { return m_map == other.m_map && m_index == other.m_index; }
            bool operator!=(const Iterator& other) const { return !(*this == other); }

        private:
            void AdvanceToOccupied()
            {
                if (!m_map)
                    return;
                while (m_index < m_map->m_table.Size() && !m_map->m_table[m_index].isOccupied)
                    ++m_index;
            }
            FlatHashMap* m_map {nullptr};
            UIntSize     m_index {0};
        };

        class ConstIterator
        {
        public:
            using difference_type   = std::ptrdiff_t;
            using value_type        = KeyValueConstRef;
            using reference         = KeyValueConstRef;
            using pointer           = void;
            using iterator_category = std::forward_iterator_tag;

            ConstIterator() = default;
            ConstIterator(const FlatHashMap* map, UIntSize idx) : m_map(map), m_index(idx) { AdvanceToOccupied(); }

            reference operator*() const { return {m_map->m_table[m_index].key, m_map->m_table[m_index].value}; }

            ConstIterator& operator++()
            {
                ++m_index;
                AdvanceToOccupied();
                return *this;
            }
            bool operator==(const ConstIterator& other) const { return m_map == other.m_map && m_index == other.m_index; }
            bool operator!=(const ConstIterator& other) const { return !(*this == other); }

        private:
            void AdvanceToOccupied()
            {
                if (!m_map)
                    return;
                while (m_index < m_map->m_table.Size() && !m_map->m_table[m_index].isOccupied)
                    ++m_index;
            }
            const FlatHashMap* m_map {nullptr};
            UIntSize           m_index {0};
        };

        // PascalCase (project style) and std-style iterator accessors
        Iterator      Begin() { return Iterator(this, 0); }
        Iterator      End() { return Iterator(this, m_table.Size()); }
        ConstIterator Begin() const { return ConstIterator(this, 0); }
        ConstIterator End() const { return ConstIterator(this, m_table.Size()); }
        ConstIterator CBegin() const { return ConstIterator(this, 0); }
        ConstIterator CEnd() const { return ConstIterator(this, m_table.Size()); }

        Iterator      begin() { return Begin(); }
        Iterator      end() { return End(); }
        ConstIterator begin() const { return Begin(); }
        ConstIterator end() const { return End(); }
        ConstIterator cbegin() const { return CBegin(); }
        ConstIterator cend() const { return CEnd(); }

    private:
        static constexpr F32      loadFactorThreshold = 0.75f;
        static constexpr UIntSize initialCapacity     = 16;

        struct Entry
        {
            Key   key;
            Value value;
            bool  isOccupied;

            /// @brief Default constructor initializing as unoccupied.
            inline Entry()
                : isOccupied(false) {}
        };

        // helpers
        static constexpr UIntSize NextPow2(UIntSize x) noexcept
        {
            if (x <= 1)
                return 1;
            --x;
            x |= x >> 1;
            x |= x >> 2;
            x |= x >> 4;
            x |= x >> 8;
            x |= x >> 16;
#if SIZE_MAX > 0xffffffff
            x |= x >> 32;
#endif
            return x + 1;
        }

        template<class K>
        NGIN_ALWAYS_INLINE UIntSize ComputeHash(const K& key) const
        {
            return static_cast<UIntSize>(m_hash(key));
        }

        void RehashGrow();
        void ReinsertAll(Vector<Entry>& newTable);

        // backward-shift deletion (linear probing)
        void BackwardShiftFrom(UIntSize holeIndex);


        Vector<Entry> m_table;
        UIntSize      m_size;
        Hash          m_hash {};
        KeyEqual      m_equal {};

        NGIN_ALWAYS_INLINE bool NeedsRehash() const
        {
            return static_cast<F32>(m_size) / static_cast<F32>(Capacity()) > loadFactorThreshold;
        }
    };

    //---------------------------- IMPLEMENTATION ----------------------------//
    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }
    inline bool FlatHashMap<Key, Value, Hash, KeyEqual>::Contains(const Key& key) const
    {
        const auto mask   = Capacity() - 1;
        UIntSize   index  = ComputeHash(key) & mask;
        UIntSize   probed = 0;
        while (probed < Capacity())
        {
            if (!m_table[index].isOccupied)
                return false;
            if (m_equal(m_table[index].key, key))
                return true;
            index = (index + 1) & mask;
            ++probed;
        }
        return false;// table full and not found
    }
    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }
    inline void FlatHashMap<Key, Value, Hash, KeyEqual>::Clear()
    {
        for (UIntSize i = 0; i < m_table.Size(); ++i)
        {
            m_table[i].isOccupied = false;
        }
        m_size = 0;
    }

    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline void FlatHashMap<Key, Value, Hash, KeyEqual>::RehashGrow()
    {
        // Create a new table with double the capacity (power-of-two is preserved)
        Vector<Entry> newTable(Capacity() * 2);

        for (UIntSize i = 0; i < newTable.Capacity(); i++)
        {
            newTable.EmplaceBack();
        }

        ReinsertAll(newTable);
        m_table = std::move(newTable);
    }

    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline FlatHashMap<Key, Value, Hash, KeyEqual>::FlatHashMap()
        : m_table(initialCapacity), m_size(0), m_hash(Hash {}), m_equal(KeyEqual {})
    {
        for (UIntSize i = 0; i < m_table.Capacity(); i++)
        {
            m_table.EmplaceBack();
        }
    }

    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline void FlatHashMap<Key, Value, Hash, KeyEqual>::Insert(const Key& key, const Value& value)
    {
        if (NeedsRehash())
        {
            RehashGrow();
        }
        const auto mask   = Capacity() - 1;
        UIntSize   index  = ComputeHash(key) & mask;
        UIntSize   probed = 0;
        while (probed < Capacity())
        {
            if (!m_table[index].isOccupied)
            {
                m_table[index].key        = key;
                m_table[index].value      = value;
                m_table[index].isOccupied = true;
                ++m_size;
                return;
            }
            // If the key already exists, update the value
            if (m_equal(m_table[index].key, key))
            {
                m_table[index].value = value;
                return;
            }
            index = (index + 1) & mask;
            ++probed;
        }
        // Should not happen with resize policy
        NGIN::Unreachable();
    }

    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline void FlatHashMap<Key, Value, Hash, KeyEqual>::Insert(const Key& key, Value&& value)
    {
        if (NeedsRehash())
        {
            RehashGrow();
        }
        const auto mask   = Capacity() - 1;
        UIntSize   index  = ComputeHash(key) & mask;
        UIntSize   probed = 0;
        while (probed < Capacity())
        {
            if (!m_table[index].isOccupied)
            {
                m_table[index].key        = key;
                m_table[index].value      = std::move(value);
                m_table[index].isOccupied = true;
                ++m_size;
                return;
            }
            // If the key already exists, update the value
            if (m_equal(m_table[index].key, key))
            {
                m_table[index].value = std::move(value);
                return;
            }
            index = (index + 1) & mask;
            ++probed;
        }
        NGIN::Unreachable();
    }

    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline void FlatHashMap<Key, Value, Hash, KeyEqual>::Remove(const Key& key)
    {
        const auto mask   = Capacity() - 1;
        UIntSize   index  = ComputeHash(key) & mask;
        UIntSize   probed = 0;
        while (probed < Capacity())
        {
            if (!m_table[index].isOccupied)
                return;// not found
            if (m_equal(m_table[index].key, key))
            {
                // Robin Hood deletion via backward shift to keep cluster continuous.
                m_table[index].isOccupied = false;
                --m_size;
                BackwardShiftFrom(index);
                return;
            }
            index = (index + 1) & mask;
            ++probed;
        }
    }

    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline Value FlatHashMap<Key, Value, Hash, KeyEqual>::Get(const Key& key) const
    {
        const auto mask   = Capacity() - 1;
        UIntSize   index  = ComputeHash(key) & mask;
        UIntSize   probed = 0;
        while (probed < Capacity())
        {
            if (!m_table[index].isOccupied)
                break;
            if (m_equal(m_table[index].key, key))
                return m_table[index].value;
            index = (index + 1) & mask;
            ++probed;
        }

        throw std::out_of_range("Key not found in hashmap");
    }

    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline Value& FlatHashMap<Key, Value, Hash, KeyEqual>::operator[](const Key& key)
    {
        if (NeedsRehash())
        {
            RehashGrow();
        }

        const auto mask   = Capacity() - 1;
        UIntSize   index  = ComputeHash(key) & mask;
        UIntSize   probed = 0;
        while (probed < Capacity())
        {
            if (!m_table[index].isOccupied)
            {
                // Insert default
                m_table[index].key        = key;
                m_table[index].isOccupied = true;
                m_table[index].value      = Value();
                ++m_size;
                return m_table[index].value;
            }
            if (m_equal(m_table[index].key, key))
            {
                return m_table[index].value;
            }
            index = (index + 1) & mask;
            ++probed;
        }
        // If table managed properly, shouldn't reach here
        NGIN::Unreachable();
    }

    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline const Value& FlatHashMap<Key, Value, Hash, KeyEqual>::operator[](const Key& key) const
    {
        const auto mask   = Capacity() - 1;
        UIntSize   index  = ComputeHash(key) & mask;
        UIntSize   probed = 0;
        while (probed < Capacity())
        {
            if (!m_table[index].isOccupied)
                break;
            if (m_equal(m_table[index].key, key))
                return m_table[index].value;
            index = (index + 1) & mask;
            ++probed;
        }

        throw std::out_of_range("Key not found in hashmap");
    }

    // === GetRef / GetPtr ===
    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }
    inline Value& FlatHashMap<Key, Value, Hash, KeyEqual>::GetRef(const Key& key)
    {
        const auto* p = GetPtr(key);
        if (!p)
            throw std::out_of_range("Key not found in hashmap");
        return *const_cast<Value*>(p);
    }
    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }
    inline const Value& FlatHashMap<Key, Value, Hash, KeyEqual>::GetRef(const Key& key) const
    {
        const auto* p = GetPtr(key);
        if (!p)
            throw std::out_of_range("Key not found in hashmap");
        return *p;
    }
    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }
    inline Value* FlatHashMap<Key, Value, Hash, KeyEqual>::GetPtr(const Key& key) noexcept
    {
        const auto* cp = const_cast<const FlatHashMap*>(this)->GetPtr(key);
        return const_cast<Value*>(cp);
    }
    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }
    inline const Value* FlatHashMap<Key, Value, Hash, KeyEqual>::GetPtr(const Key& key) const noexcept
    {
        const auto mask   = Capacity() - 1;
        UIntSize   index  = ComputeHash(key) & mask;
        UIntSize   probed = 0;
        while (probed < Capacity())
        {
            if (!m_table[index].isOccupied)
                return nullptr;
            if (m_equal(m_table[index].key, key))
                return &m_table[index].value;
            index = (index + 1) & mask;
            ++probed;
        }
        return nullptr;
    }

    // Heterogeneous overloads
    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }
    template<class K>
        requires requires(const Hash& h, const KeyEqual& eq, const K& k, const Key& kk) { h(k); eq(k, kk); eq(kk, k); }
    inline bool FlatHashMap<Key, Value, Hash, KeyEqual>::Contains(const K& key) const
    {
        return GetPtr(key) != nullptr;
    }
    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }
    template<class K>
        requires requires(const Hash& h, const KeyEqual& eq, const K& k, const Key& kk) { h(k); eq(k, kk); eq(kk, k); }
    inline const Value* FlatHashMap<Key, Value, Hash, KeyEqual>::GetPtr(const K& key) const noexcept
    {
        const auto mask   = Capacity() - 1;
        UIntSize   index  = ComputeHash(key) & mask;
        UIntSize   probed = 0;
        while (probed < Capacity())
        {
            if (!m_table[index].isOccupied)
                return nullptr;
            if (m_equal(m_table[index].key, key))
                return &m_table[index].value;
            index = (index + 1) & mask;
            ++probed;
        }
        return nullptr;
    }
    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }
    template<class K>
        requires requires(const Hash& h, const KeyEqual& eq, const K& k, const Key& kk) { h(k); eq(k, kk); eq(kk, k); }
    inline Value* FlatHashMap<Key, Value, Hash, KeyEqual>::GetPtr(const K& key) noexcept
    {
        const auto* cp = const_cast<const FlatHashMap*>(this)->GetPtr(key);
        return const_cast<Value*>(cp);
    }

    // Reserve / Rehash
    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }
    inline void FlatHashMap<Key, Value, Hash, KeyEqual>::Reserve(UIntSize count)
    {
        // target bucket count so that count <= loadFactor * buckets
        const F32 desired = static_cast<F32>(count) / loadFactorThreshold;
        UIntSize  buckets = static_cast<UIntSize>(desired) + 1;
        if (buckets < initialCapacity)
            buckets = initialCapacity;
        buckets = NextPow2(buckets);
        if (buckets <= Capacity())
            return;
        Rehash(buckets);
    }

    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }
    inline void FlatHashMap<Key, Value, Hash, KeyEqual>::Rehash(UIntSize newBucketCount)
    {
        UIntSize buckets = NextPow2(newBucketCount);
        if (buckets < initialCapacity)
            buckets = initialCapacity;
        if (buckets == Capacity())
            return;

        Vector<Entry> newTable(buckets);
        for (UIntSize i = 0; i < newTable.Capacity(); ++i)
            newTable.EmplaceBack();
        ReinsertAll(newTable);
        m_table = std::move(newTable);
    }

    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }
    inline void FlatHashMap<Key, Value, Hash, KeyEqual>::ReinsertAll(Vector<Entry>& newTable)
    {
        const auto newMask = newTable.Capacity() - 1;
        for (UIntSize j = 0; j < m_table.Size(); ++j)
        {
            if (!m_table[j].isOccupied)
                continue;
            UIntSize index  = ComputeHash(m_table[j].key) & newMask;
            UIntSize probed = 0;
            while (probed < newTable.Capacity())
            {
                if (!newTable[index].isOccupied)
                {
                    newTable[index].key        = std::move(m_table[j].key);
                    newTable[index].value      = std::move(m_table[j].value);
                    newTable[index].isOccupied = true;
                    break;
                }
                index = (index + 1) & newMask;
                ++probed;
            }
            if (probed >= newTable.Capacity())
                NGIN::Unreachable();
        }
    }

    template<typename Key, typename Value, typename Hash, typename KeyEqual>
        requires requires { Value(); }
    inline void FlatHashMap<Key, Value, Hash, KeyEqual>::BackwardShiftFrom(UIntSize holeIndex)
    {
        const auto mask = Capacity() - 1;
        UIntSize   prev = holeIndex;
        UIntSize   curr = (prev + 1) & mask;
        while (m_table[curr].isOccupied)
        {
            // Compute this element's ideal position
            UIntSize home = ComputeHash(m_table[curr].key) & mask;
            // Distance from home to current
            const bool atHome = (home == curr);
            if (atHome)
                break;// don't shift elements that are at their home bucket
            // Move current back into the hole
            m_table[prev].key        = std::move(m_table[curr].key);
            m_table[prev].value      = std::move(m_table[curr].value);
            m_table[prev].isOccupied = true;
            // Vacate current and advance
            m_table[curr].isOccupied = false;
            prev                     = curr;
            curr                     = (curr + 1) & mask;
        }
    }

}// namespace NGIN::Containers
