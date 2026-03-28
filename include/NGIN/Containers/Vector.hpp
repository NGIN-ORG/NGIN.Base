/// @file Vector.hpp
/// @brief Declaration and inline implementation of the Vector container class.
/// @details
/// A dynamically resizable array-like container that uses an externally-owned
/// allocator and stores elements contiguously.  If you default-construct,
/// it assumes `Allocator::Instance()` exists; otherwise you must supply a
/// reference to an allocator yourself.
#pragma once

#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Meta/TypeTraits.hpp>
#include <NGIN/Primitives.hpp>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace NGIN::Containers
{
    /// @tparam T Element type
    /// @tparam Alloc Allocator satisfying AllocatorConcept (value-stored). Defaults to SystemAllocator.
    template<class T, NGIN::Memory::AllocatorConcept Alloc = NGIN::Memory::SystemAllocator>
    class Vector
    {
    public:
        using Value     = T;
        using AllocType = Alloc;

        Vector() noexcept(std::is_nothrow_default_constructible_v<Alloc>) = default;
        explicit Vector(std::size_t initialCapacity, Alloc alloc = Alloc {}) : m_alloc(std::move(alloc))
        {
            if (initialCapacity)
                Reserve(initialCapacity);
        }
        Vector(std::initializer_list<T> init, Alloc alloc = Alloc {}) : m_alloc(std::move(alloc))
        {
            Reserve(init.size());
            for (const auto& v: init)
                ::new (&m_data[m_size++]) T(v);
        }
        Vector(const Vector& other) : m_alloc(other.m_alloc)
        {
            Reserve(other.m_size);
            for (std::size_t i = 0; i < other.m_size; ++i)
                ::new (&m_data[i]) T(other.m_data[i]);
            m_size = other.m_size;
        }
        Vector& operator=(const Vector& other)
        {
            if (this != &other)
            {
                AssignFromCopy(other);
            }
            return *this;
        }
        Vector(Vector&& other) noexcept(std::is_nothrow_move_constructible_v<Alloc>)
            : m_alloc(std::move(other.m_alloc)), m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity)
        {
            other.m_data = nullptr;
            other.m_size = other.m_capacity = 0;
        }
        Vector& operator=(Vector&& other) noexcept(
                (NGIN::Memory::AllocatorPropagationTraits<Alloc>::PropagateOnMoveAssignment &&
                 std::is_nothrow_move_assignable_v<Alloc>) ||
                NGIN::Memory::AllocatorPropagationTraits<Alloc>::IsAlwaysEqual)
        {
            if (this != &other)
            {
                if constexpr (CanStealOnMove())
                {
                    if constexpr (NGIN::Memory::AllocatorPropagationTraits<Alloc>::PropagateOnMoveAssignment)
                    {
                        ReleaseStorage();
                        m_alloc = std::move(other.m_alloc);
                        StealStorageFrom(other);
                    }
                    else if constexpr (NGIN::Memory::AllocatorPropagationTraits<Alloc>::IsAlwaysEqual)
                    {
                        ReleaseStorage();
                        StealStorageFrom(other);
                    }
                    else if (CanStealStorageFrom(other))
                    {
                        ReleaseStorage();
                        StealStorageFrom(other);
                    }
                    else
                    {
                        AssignFromMoved(other);
                    }
                }
                else
                {
                    AssignFromMoved(other);
                }
            }
            return *this;
        }
        ~Vector()
        {
            ReleaseStorage();
        }

        //=== Element modifiers ===//

        /// @brief Push by copy.
        T& PushBack(const T& value)
        {
            EnsureCapacityForOne();
            ::new (&m_data[m_size]) T(value);
            return m_data[m_size++];
        }

        /// @brief Push by move.
        T& PushBack(T&& value)
        {
            EnsureCapacityForOne();
            ::new (&m_data[m_size]) T(std::move(value));
            return m_data[m_size++];
        }

        /// @brief In-place construct at the end.
        template<typename... Args>
        T& EmplaceBack(Args&&... args)
        {
            EnsureCapacityForOne();
            ::new (&m_data[m_size]) T(std::forward<Args>(args)...);
            return m_data[m_size++];
        }

        /// @brief Insert by copy at index (shifts elements right).
        void PushAt(UIntSize index, const T& value)
        {
            if (index > m_size)
                throw std::out_of_range("Vector::PushAt: index out of range");
            EnsureCapacityForOne();
            if (index == m_size)
            {
                ::new (&m_data[m_size++]) T(value);
                return;
            }
            const std::size_t tailCount = m_size - index;
            if constexpr (Meta::TypeTraits<T>::IsBitwiseRelocatable())
            {
                std::memmove(static_cast<void*>(m_data + index + 1), static_cast<void*>(m_data + index), tailCount * sizeof(T));
                ::new (&m_data[index]) T(value);
                ++m_size;
            }
            else
            {
                // Create space at end then shift via move assignment (basic guarantee).
                ::new (&m_data[m_size]) T(std::move(m_data[m_size - 1]));
                for (std::size_t i = m_size - 1; i > index; --i)
                    m_data[i] = std::move(m_data[i - 1]);
                m_data[index].~T();
                ::new (&m_data[index]) T(value);
                ++m_size;
            }
        }

        /// @brief Insert by move at index (shifts elements right).
        void PushAt(UIntSize index, T&& value)
        {
            if (index > m_size)
                throw std::out_of_range("Vector::PushAt: index out of range");
            EnsureCapacityForOne();
            if (index == m_size)
            {
                ::new (&m_data[m_size++]) T(std::move(value));
                return;
            }
            const std::size_t tailCount = m_size - index;
            if constexpr (Meta::TypeTraits<T>::IsBitwiseRelocatable())
            {
                std::memmove(static_cast<void*>(m_data + index + 1), static_cast<void*>(m_data + index), tailCount * sizeof(T));
                ::new (&m_data[index]) T(std::move(value));
                ++m_size;
            }
            else
            {
                ::new (&m_data[m_size]) T(std::move(m_data[m_size - 1]));
                for (std::size_t i = m_size - 1; i > index; --i)
                    m_data[i] = std::move(m_data[i - 1]);
                m_data[index].~T();
                ::new (&m_data[index]) T(std::move(value));
                ++m_size;
            }
        }

        /// @brief In-place insert at index (shifts elements right).
        template<typename... Args>
        void EmplaceAt(UIntSize index, Args&&... args)
        {
            if (index > m_size)
                throw std::out_of_range("Vector::EmplaceAt: index out of range");
            EnsureCapacityForOne();
            if (index == m_size)
            {
                ::new (&m_data[m_size++]) T(std::forward<Args>(args)...);
                return;
            }
            const std::size_t tailCount = m_size - index;
            if constexpr (Meta::TypeTraits<T>::IsBitwiseRelocatable())
            {
                std::memmove(static_cast<void*>(m_data + index + 1), static_cast<void*>(m_data + index), tailCount * sizeof(T));
                ::new (&m_data[index]) T(std::forward<Args>(args)...);
                ++m_size;
            }
            else
            {
                ::new (&m_data[m_size]) T(std::move(m_data[m_size - 1]));
                for (std::size_t i = m_size - 1; i > index; --i)
                    m_data[i] = std::move(m_data[i - 1]);
                m_data[index].~T();
                ::new (&m_data[index]) T(std::forward<Args>(args)...);
                ++m_size;
            }
        }

        /// @brief Pop the last element.
        void PopBack()
        {
            if (m_size == 0)
                throw std::out_of_range("Vector::PopBack: vector is empty");
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                m_data[m_size - 1].~T();
            }
            --m_size;
        }

        /// @brief Erase at index (shifts down).
        void Erase(UIntSize index)
        {
            if (index >= m_size)
                throw std::out_of_range("Vector::Erase: index out of range");
            if constexpr (Meta::TypeTraits<T>::IsBitwiseRelocatable())
            {
                std::memmove(static_cast<void*>(m_data + index), static_cast<void*>(m_data + index + 1), (m_size - index - 1) * sizeof(T));
                --m_size;
            }
            else
            {
                for (UIntSize i = index; i + 1 < m_size; ++i)
                    m_data[i] = std::move(m_data[i + 1]);
                if constexpr (!std::is_trivially_destructible_v<T>)
                {
                    m_data[m_size - 1].~T();
                }
                --m_size;
            }
        }

        /// @brief Remove all elements (capacity remains).
        void Clear() noexcept
        {
            DestroyElements(m_data, m_size);
            m_size = 0;
        }

        //=== Capacity management ===//

        /// @brief Ensure at least `newCapacity` slots.
        void Reserve(UIntSize newCapacity)
        {
            if (newCapacity <= m_capacity)
                return;
            if (newCapacity > (std::numeric_limits<std::size_t>::max() / (sizeof(T) ? sizeof(T) : 1)))
                throw std::length_error("Vector::Reserve size overflow");
            void* mem = m_alloc.Allocate(newCapacity * sizeof(T), alignof(T));
            if (!mem)
                throw std::bad_alloc();
            T*       newData = static_cast<T*>(mem);
            UIntSize i       = 0;
            try
            {
                if constexpr (Meta::TypeTraits<T>::IsBitwiseRelocatable())
                {
                    if (m_size)
                        std::memcpy(newData, m_data, m_size * sizeof(T));
                }
                else if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
                {
                    for (; i < m_size; ++i)
                        ::new (&newData[i]) T(std::move(m_data[i]));
                }
                else
                {
                    for (; i < m_size; ++i)
                        ::new (&newData[i]) T(m_data[i]);
                }
            } catch (...)
            {
                for (UIntSize j = 0; j < i; ++j)
                    newData[j].~T();
                m_alloc.Deallocate(newData, newCapacity * sizeof(T), alignof(T));
                throw;
            }
            if constexpr (!Meta::TypeTraits<T>::IsBitwiseRelocatable())
            {
                DestroyElements(m_data, m_size);
            }
            if (m_data)
                m_alloc.Deallocate(m_data, m_capacity * sizeof(T), alignof(T));
            m_data     = newData;
            m_capacity = newCapacity;
        }

        /// @brief Shrink capacity to match size.
        void ShrinkToFit()
        {
            if (m_size == m_capacity)
                return;
            if (m_size == 0)
            {
                if (m_data)
                {
                    m_alloc.Deallocate(m_data, m_capacity * sizeof(T), alignof(T));
                    m_data = nullptr;
                }
                m_capacity = 0;
                return;
            }
            // Heuristic: only shrink if wasting more than 50%
            if (m_capacity < m_size * 2)
                return;
            void* mem = m_alloc.Allocate(m_size * sizeof(T), alignof(T));
            if (!mem)
                throw std::bad_alloc();
            T* newData = static_cast<T*>(mem);
            if constexpr (Meta::TypeTraits<T>::IsBitwiseRelocatable())
            {
                std::memcpy(newData, m_data, m_size * sizeof(T));
            }
            else
            {
                UIntSize i = 0;
                try
                {
                    if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
                    {
                        for (; i < m_size; ++i)
                            ::new (&newData[i]) T(std::move(m_data[i]));
                    }
                    else
                    {
                        for (; i < m_size; ++i)
                            ::new (&newData[i]) T(m_data[i]);
                    }
                } catch (...)
                {
                    for (UIntSize j = 0; j < i; ++j)
                        newData[j].~T();
                    m_alloc.Deallocate(newData, m_size * sizeof(T), alignof(T));
                    throw;
                }
                DestroyElements(m_data, m_size);
            }
            m_alloc.Deallocate(m_data, m_capacity * sizeof(T), alignof(T));
            m_data     = newData;
            m_capacity = m_size;
        }

        //=== Observers ===//

        [[nodiscard]] UIntSize Size() const noexcept
        {
            return m_size;
        }
        [[nodiscard]] UIntSize Capacity() const noexcept
        {
            return m_capacity;
        }
        [[nodiscard]] Alloc& GetAllocator() noexcept
        {
            return m_alloc;
        }
        [[nodiscard]] const Alloc& GetAllocator() const noexcept
        {
            return m_alloc;
        }

        T& At(UIntSize idx)
        {
            if (idx >= m_size)
                throw std::out_of_range("Vector::At: index out of range");
            return m_data[idx];
        }
        const T& At(UIntSize idx) const
        {
            if (idx >= m_size)
                throw std::out_of_range("Vector::At: index out of range");
            return m_data[idx];
        }

        T& operator[](UIntSize idx)
        {
            return m_data[idx];
        }
        const T& operator[](UIntSize idx) const
        {
            return m_data[idx];
        }

        //=== Iterators & data ===//

        [[nodiscard]] T* data() noexcept
        {
            return m_data;
        }
        [[nodiscard]] const T* data() const noexcept
        {
            return m_data;
        }
        [[nodiscard]] T* begin() noexcept
        {
            return m_data;
        }
        [[nodiscard]] const T* begin() const noexcept
        {
            return m_data;
        }
        [[nodiscard]] T* end() noexcept
        {
            return m_data + m_size;
        }
        [[nodiscard]] const T* end() const noexcept
        {
            return m_data + m_size;
        }

    private:
        static constexpr bool CanStealOnMove() noexcept
        {
            return NGIN::Memory::AllocatorPropagationTraits<Alloc>::PropagateOnMoveAssignment ||
                   NGIN::Memory::AllocatorPropagationTraits<Alloc>::IsAlwaysEqual ||
                   std::equality_comparable<Alloc>;
        }

        void DestroyElements(T* data, UIntSize count) noexcept
        {
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                for (UIntSize i = 0; i < count; ++i)
                    data[i].~T();
            }
        }

        void ReleaseStorage() noexcept
        {
            DestroyElements(m_data, m_size);
            if (m_data)
                m_alloc.Deallocate(m_data, m_capacity * sizeof(T), alignof(T));
            m_data     = nullptr;
            m_size     = 0;
            m_capacity = 0;
        }

        void StealStorageFrom(Vector& other) noexcept
        {
            m_data           = other.m_data;
            m_size           = other.m_size;
            m_capacity       = other.m_capacity;
            other.m_data     = nullptr;
            other.m_size     = 0;
            other.m_capacity = 0;
        }

        [[nodiscard]] bool CanStealStorageFrom(const Vector& other) const noexcept
            requires std::equality_comparable<Alloc>
        {
            return m_alloc == other.m_alloc;
        }

        template<typename SourceType>
        static void ConstructElements(T* destination, SourceType* source, UIntSize count)
        {
            UIntSize i = 0;
            try
            {
                if constexpr (Meta::TypeTraits<T>::IsBitwiseRelocatable() &&
                              std::is_same_v<std::remove_cv_t<SourceType>, T>)
                {
                    if (count != 0)
                        std::memcpy(destination, source, count * sizeof(T));
                }
                else if constexpr (std::is_const_v<SourceType>)
                {
                    for (; i < count; ++i)
                        ::new (&destination[i]) T(source[i]);
                }
                else if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
                {
                    for (; i < count; ++i)
                        ::new (&destination[i]) T(std::move(source[i]));
                }
                else
                {
                    for (; i < count; ++i)
                        ::new (&destination[i]) T(source[i]);
                }
            } catch (...)
            {
                if constexpr (!Meta::TypeTraits<T>::IsBitwiseRelocatable())
                {
                    for (UIntSize j = 0; j < i; ++j)
                        destination[j].~T();
                }
                throw;
            }
        }

        T* AllocateStorage(UIntSize capacity)
        {
            void* mem = m_alloc.Allocate(capacity * sizeof(T), alignof(T));
            if (!mem)
                throw std::bad_alloc();
            return static_cast<T*>(mem);
        }

        void AssignFromCopy(const Vector& other)
        {
            if constexpr (NGIN::Memory::AllocatorPropagationTraits<Alloc>::PropagateOnCopyAssignment)
            {
                ReleaseStorage();
                m_alloc = other.m_alloc;
                if (other.m_size == 0)
                    return;

                m_data = AllocateStorage(other.m_size);
                try
                {
                    ConstructElements(m_data, other.m_data, other.m_size);
                    m_size     = other.m_size;
                    m_capacity = other.m_size;
                } catch (...)
                {
                    m_alloc.Deallocate(m_data, other.m_size * sizeof(T), alignof(T));
                    m_data = nullptr;
                    throw;
                }
                return;
            }

            if (other.m_size > m_capacity)
            {
                T* newData = AllocateStorage(other.m_size);
                try
                {
                    ConstructElements(newData, other.m_data, other.m_size);
                } catch (...)
                {
                    m_alloc.Deallocate(newData, other.m_size * sizeof(T), alignof(T));
                    throw;
                }

                ReleaseStorage();
                m_data     = newData;
                m_size     = other.m_size;
                m_capacity = other.m_size;
                return;
            }

            UIntSize i = 0;
            for (; i < m_size && i < other.m_size; ++i)
                m_data[i] = other.m_data[i];
            for (; i < other.m_size; ++i)
                ::new (&m_data[i]) T(other.m_data[i]);

            if (m_size > other.m_size)
                DestroyElements(m_data + other.m_size, m_size - other.m_size);

            m_size = other.m_size;
        }

        void AssignFromMoved(Vector& other)
        {
            if (other.m_size > m_capacity)
            {
                T* newData = AllocateStorage(other.m_size);
                try
                {
                    ConstructElements(newData, other.m_data, other.m_size);
                } catch (...)
                {
                    m_alloc.Deallocate(newData, other.m_size * sizeof(T), alignof(T));
                    throw;
                }

                ReleaseStorage();
                m_data     = newData;
                m_size     = other.m_size;
                m_capacity = other.m_size;
            }
            else
            {
                UIntSize i = 0;
                for (; i < m_size && i < other.m_size; ++i)
                    m_data[i] = std::move(other.m_data[i]);
                for (; i < other.m_size; ++i)
                    ::new (&m_data[i]) T(std::move(other.m_data[i]));
                if (m_size > other.m_size)
                    DestroyElements(m_data + other.m_size, m_size - other.m_size);
                m_size = other.m_size;
            }

            other.Clear();
        }

        void EnsureCapacityForOne()
        {
            if (m_size < m_capacity)
                return;
            // 1.5x growth (capacity + capacity/2 + 1 to ensure progress) with overflow guard
            std::size_t next = m_capacity ? m_capacity + (m_capacity >> 1) + 1 : 1;
            if (next < m_capacity)// overflow
                next = m_capacity + 1;
            Reserve(next);
        }
        [[no_unique_address]] Alloc m_alloc {};
        T*                          m_data {nullptr};
        UIntSize                    m_size {0};
        UIntSize                    m_capacity {0};
    };
}// namespace NGIN::Containers
