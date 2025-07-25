/// @file FlatHashMap.hpp
/// @brief Header file for a flat hashmap implementation with growth support.

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

namespace NGIN::Containers
{
    /// @brief Flat hashmap implementation with dynamic growth.
    template<typename Key, typename Value>
        requires requires { Value(); }///TODO: Replace with a proper concept
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

        Value& GetRef(const Key& key);

        const Value& GetRef(const Key& key) const;

        /// @brief Checks if the key exists in the hashmap.
        bool Contains(const Key& key) const;

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

    private:
        static constexpr F32 loadFactorThreshold = 0.75f;
        static constexpr UIntSize initialCapacity = 16;

        struct Entry
        {
            Key key;
            Value value;
            bool isOccupied;

            /// @brief Default constructor initializing as unoccupied.
            inline Entry()
                : isOccupied(false) {}
        };

        UIntSize Hash(const Key& key) const;
        void Rehash();


        Vector<Entry> m_table;
        UIntSize m_size;

        NGIN_ALWAYS_INLINE bool NeedsRehash() const
        {
            return static_cast<F32>(m_size) / static_cast<F32>(Capacity()) > loadFactorThreshold;
        }
    };

    //---------------------------- IMPLEMENTATION ----------------------------//
    template<typename Key, typename Value>
        requires requires { Value(); }
    inline bool FlatHashMap<Key, Value>::Contains(const Key& key) const
    {
        UIntSize index = Hash(key) & (Capacity() - 1);
        UIntSize i     = 1;
        while (m_table[index].isOccupied)
        {
            const UIntSize i2 = i * i;
            if (m_table[index].key == key)
            {
                return true;
            }
            if (i2 > Capacity())
            {
                break;
            }
            index = (index + i2) & (Capacity() - 1);
            ++i;
        }
        return false;
    }
    template<typename Key, typename Value>
        requires requires { Value(); }
    inline void FlatHashMap<Key, Value>::Clear()
    {
        for (UIntSize i = 0; i < m_table.Size(); ++i)
        {
            m_table[i].isOccupied = false;
        }
        m_size = 0;
    }

    template<typename Key, typename Value>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline UIntSize FlatHashMap<Key, Value>::Hash(const Key& key) const
    {
        return std::hash<Key> {}(key);
    }

    template<typename Key, typename Value>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline void FlatHashMap<Key, Value>::Rehash()
    {
        // Create a new table with double the capacity
        Vector<Entry> newTable(Capacity() * 2);

        for (int i = 0; i < newTable.Capacity(); i++)
        {
            newTable.EmplaceBack();
        }

        for (UIntSize j = 0; j < m_table.Size(); ++j)
        {
            if (m_table[j].isOccupied)
            {
                // Find the index to insert the new entry
                UIntSize index = Hash(m_table[j].key) & (newTable.Capacity() - 1);
                // Quadratic probing
                UIntSize i = 1;
                while (newTable[index].isOccupied)
                {
                    const UIntSize i2 = i * i;

                    // Rehash if the table is full, should NEVER happend
                    if (i2 > newTable.Capacity()) [[unlikely]]
                    {
                        NGIN::Unreachable();
                    }
                    index = (index + i2) & (newTable.Capacity() - 1);
                    ++i;
                }

                // Insert the new entry
                newTable[index].key        = m_table[j].key;
                newTable[index].value      = m_table[j].value;
                newTable[index].isOccupied = true;
            }
        }
        m_table = std::move(newTable);
    }



    template<typename Key, typename Value>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline FlatHashMap<Key, Value>::FlatHashMap()
        : m_table(initialCapacity), m_size(0)
    {
        for (int i = 0; i < m_table.Capacity(); i++)
        {
            m_table.EmplaceBack();
        }
    }

    template<typename Key, typename Value>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline void FlatHashMap<Key, Value>::Insert(const Key& key, const Value& value)
    {
        if (NeedsRehash())
        {
            Rehash();
        }

        // Find the index to insert the new entry
        UIntSize index = Hash(key) & (Capacity() - 1);

        // Quadratic probing
        UIntSize i = 1;
        while (m_table[index].isOccupied)
        {
            const UIntSize i2 = i * i;

            // Rehash if the table is full, should basically never happen
            if (i2 > Capacity()) [[unlikely]]
            {
                Rehash();
                i = 1;
                continue;
            }

            // If the key already exists, update the value
            if (m_table[index].key == key)
            {
                m_table[index].value = value;
                return;
            }

            index = (index + i2) & (Capacity() - 1);
            ++i;

        }

        // Insert the new entry 
        m_table[index].key = key;
        m_table[index].value = value;
        m_table[index].isOccupied = true;
        ++m_size;
    }

    template<typename Key, typename Value>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline void FlatHashMap<Key, Value>::Insert(const Key& key, Value&& value)
    {
        if (NeedsRehash())
        {
            Rehash();
        }
        // Find the index to insert the new entry
        UIntSize index = Hash(key) & (Capacity() - 1);
        // Quadratic probing
        UIntSize i = 1;
        while (m_table[index].isOccupied)
        {
            const UIntSize i2 = i * i;
            // Rehash if the table is full, should basically never happen
            if (i2 > Capacity()) [[unlikely]]
            {
                Rehash();
                i = 1;
                continue;
            }
            // If the key already exists, update the value
            if (m_table[index].key == key)
            {
                m_table[index].value = std::move(value);
                return;
            }
            index = (index + i2) & (Capacity() - 1);
            ++i;
        }
        // Insert the new entry
        m_table[index].key        = key;
        m_table[index].value      = std::move(value);
        m_table[index].isOccupied = true;
        ++m_size;
    }

    template<typename Key, typename Value>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline void FlatHashMap<Key, Value>::Remove(const Key& key)
    {
        //Find the index of the key
        UIntSize index = Hash(key) & (Capacity() - 1);
        // Quadratic probing
        UIntSize i = 1;
        while (m_table[index].isOccupied)
        {
            const UIntSize i2 = i * i;
            // If the key is found, mark the entry as unoccupied
            if (m_table[index].key == key)
            {
                m_table[index].isOccupied = false;
                --m_size;
                return;
            }
            index = (index + i2) & (Capacity() - 1);
            ++i;
        }
    }

    template<typename Key, typename Value>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline Value FlatHashMap<Key, Value>::Get(const Key& key) const
    {
        UIntSize index = Hash(key) & (Capacity() - 1);
        UIntSize i     = 1;
        while (m_table[index].isOccupied)
        {
            const UIntSize i2 = i * i;
            if (m_table[index].key == key)
            {
                return m_table[index].value;
            }

            if (i2 > Capacity())
            {
                break;
            }

            index = (index + i2) & (Capacity() - 1);
            ++i;
        }

        throw std::out_of_range("Key not found in hashmap");
    }

    template<typename Key, typename Value>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline Value& FlatHashMap<Key, Value>::operator[](const Key& key)
    {
        if (NeedsRehash())
        {
            Rehash();
        }

        UIntSize index = Hash(key) & (Capacity() - 1);
        UIntSize i     = 1;
        while (m_table[index].isOccupied)
        {
            const UIntSize i2 = i * i;
            if (m_table[index].key == key)
            {
                return m_table[index].value;
            }
            if (i2 > Capacity())
            {
                Rehash();
                i = 1;
                continue;
            }
            index = (index + i2) & (Capacity() - 1);
            ++i;
        }

        // Insert the new entry
        m_table[index].key        = key;
        m_table[index].isOccupied = true;
        m_table[index].value      = Value();
        ++m_size;
        return m_table[index].value;
    }

    template<typename Key, typename Value>
        requires requires { Value(); }///TODO: Replace with a proper concept
    inline const Value& FlatHashMap<Key, Value>::operator[](const Key& key) const
    {
        UIntSize index = Hash(key) & (Capacity() - 1);
        UIntSize i     = 1;
        while (m_table[index].isOccupied)
        {
            const UIntSize i2 = i * i;
            if (m_table[index].key == key)
            {
                return m_table[index].value;
            }
            if (i2 > Capacity())
            {
                break;
            }
            index = (index + i2) & (Capacity() - 1);
            ++i;
        }

        throw std::out_of_range("Key not found in hashmap");
    }





}// namespace NGIN::Containers