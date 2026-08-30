/// @file Vector.hpp
/// @brief Declaration and inline implementation of the Vector container class.
/// @details
/// A dynamically resizable array-like container that stores its allocator and
/// elements contiguously.
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Memory/detail/CheckedArithmetic.hpp>
#include <NGIN/Memory/detail/ConstructionGuard.hpp>
#include <NGIN/Meta/TypeTraits.hpp>
#include <NGIN/Primitives.hpp>
#include <algorithm>
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
    /// @brief Contiguous dynamically sized sequence backed by an NGIN allocator.
    /// @tparam T Element type.
    /// @tparam Alloc Value-stored allocator satisfying `AllocatorConcept`.
    template<class T, NGIN::Memory::AllocatorConcept Alloc = NGIN::Memory::SystemAllocator>
    class Vector
    {
    public:
        /// @brief Element type stored by the vector.
        using Value = T;

        /// @brief Allocator type stored by the vector.
        using AllocType = Alloc;

        /// @brief Constructs an empty vector with a default-constructed allocator.
        Vector() noexcept(std::is_nothrow_default_constructible_v<Alloc>) = default;

        /// @brief Constructs an empty vector with reserved storage.
        /// @param initialCapacity Number of element slots to allocate.
        /// @param alloc Allocator to store in the vector.
        explicit Vector(std::size_t initialCapacity, Alloc alloc = Alloc {}) : m_alloc(std::move(alloc))
        {
            if (initialCapacity)
                Reserve(initialCapacity);
        }
        /// @brief Constructs a vector by copying an initializer list.
        /// @param init Elements to copy.
        /// @param alloc Allocator to store in the vector.
        Vector(std::initializer_list<T> init, Alloc alloc = Alloc {}) : m_alloc(std::move(alloc))
        {
            InitializeFromCopy(init.begin(), init.size());
        }

        /// @brief Copy-constructs the elements and allocator from another vector.
        /// @param other Vector to copy.
        Vector(const Vector& other) : m_alloc(other.m_alloc)
        {
            InitializeFromCopy(other.m_data, other.m_size);
        }
        /// @brief Replaces the contents with a copy of another vector.
        /// @param other Vector to copy.
        /// @return This vector.
        Vector& operator=(const Vector& other)
        {
            if (this != &other)
            {
                AssignFromCopy(other);
            }
            return *this;
        }
        /// @brief Move-constructs by taking ownership of another vector's storage.
        /// @param other Vector whose storage is transferred; it is left empty.
        Vector(Vector&& other) noexcept(std::is_nothrow_move_constructible_v<Alloc>)
            : m_alloc(std::move(other.m_alloc)), m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity)
        {
            other.m_data = nullptr;
            other.m_size = other.m_capacity = 0;
        }
        /// @brief Replaces the contents by moving from another vector.
        /// @details Storage is transferred when allocator propagation permits it; otherwise elements are moved.
        /// @param other Vector to consume; it is left empty.
        /// @return This vector.
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
        /// @brief Destroys all elements and releases allocated storage.
        ~Vector()
        {
            ReleaseStorage();
        }

        //=== Element modifiers ===//

        /// @brief Push by copy.
        T& PushBack(const T& value)
        {
            if (m_size == m_capacity)
            {
                T staged(value);
                Reserve(NextCapacity(MinimumCapacityForOne()));
                std::construct_at(m_data + m_size, std::move(staged));
            }
            else
            {
                std::construct_at(m_data + m_size, value);
            }
            ++m_size;
            return m_data[m_size - 1];
        }

        /// @brief Push by move.
        T& PushBack(T&& value)
        {
            if (m_size == m_capacity)
            {
                T staged(std::move(value));
                Reserve(NextCapacity(MinimumCapacityForOne()));
                std::construct_at(m_data + m_size, std::move(staged));
            }
            else
            {
                std::construct_at(m_data + m_size, std::move(value));
            }
            ++m_size;
            return m_data[m_size - 1];
        }

        /// @brief In-place construct at the end.
        template<typename... Args>
        T& EmplaceBack(Args&&... args)
        {
            if (m_size == m_capacity)
            {
                T staged(std::forward<Args>(args)...);
                Reserve(NextCapacity(MinimumCapacityForOne()));
                std::construct_at(m_data + m_size, std::move(staged));
            }
            else
            {
                std::construct_at(m_data + m_size, std::forward<Args>(args)...);
            }
            ++m_size;
            return m_data[m_size - 1];
        }

        /// @brief Insert by copy at index (shifts elements right).
        void PushAt(UIntSize index, const T& value)
        {
            if (index > m_size)
                throw std::out_of_range("Vector::PushAt: index out of range");
            T staged(value);
            InsertPrepared(index, std::move(staged));
        }

        /// @brief Insert by move at index (shifts elements right).
        void PushAt(UIntSize index, T&& value)
        {
            if (index > m_size)
                throw std::out_of_range("Vector::PushAt: index out of range");
            T staged(std::move(value));
            InsertPrepared(index, std::move(staged));
        }

        /// @brief In-place insert at index (shifts elements right).
        template<typename... Args>
        void EmplaceAt(UIntSize index, Args&&... args)
        {
            if (index > m_size)
                throw std::out_of_range("Vector::EmplaceAt: index out of range");
            T staged(std::forward<Args>(args)...);
            InsertPrepared(index, std::move(staged));
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
            const std::size_t                            allocationBytes = StorageBytes(newCapacity);
            T*                                           newData         = AllocateStorage(newCapacity);
            NGIN::Memory::detail::AllocationGuard<Alloc> allocationGuard {
                    m_alloc, newData, allocationBytes, alignof(T)};
            NGIN::Memory::detail::ConstructionGuard<T> constructionGuard {newData};

            if constexpr (Meta::TypeTraits<T>::IsBitwiseRelocatable())
            {
                if (m_size)
                    std::memcpy(newData, m_data, m_size * sizeof(T));
            }
            else
            {
                for (UIntSize index = 0; index < m_size; ++index)
                {
                    ConstructRelocated(newData + index, m_data[index]);
                    constructionGuard.Increment();
                }
            }

            if constexpr (!Meta::TypeTraits<T>::IsBitwiseRelocatable())
                DestroyElements(m_data, m_size);
            if (m_data)
                m_alloc.Deallocate(m_data, m_capacity * sizeof(T), alignof(T));

            m_data     = newData;
            m_capacity = newCapacity;
            constructionGuard.Release();
            allocationGuard.Release();
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
            if ((m_capacity - m_size) < m_size)
                return;
            const std::size_t                            allocationBytes = StorageBytes(m_size);
            T*                                           newData         = AllocateStorage(m_size);
            NGIN::Memory::detail::AllocationGuard<Alloc> allocationGuard {
                    m_alloc, newData, allocationBytes, alignof(T)};
            NGIN::Memory::detail::ConstructionGuard<T> constructionGuard {newData};

            if constexpr (Meta::TypeTraits<T>::IsBitwiseRelocatable())
            {
                std::memcpy(newData, m_data, m_size * sizeof(T));
            }
            else
            {
                for (UIntSize index = 0; index < m_size; ++index)
                {
                    ConstructRelocated(newData + index, m_data[index]);
                    constructionGuard.Increment();
                }
                DestroyElements(m_data, m_size);
            }

            m_alloc.Deallocate(m_data, m_capacity * sizeof(T), alignof(T));
            m_data     = newData;
            m_capacity = m_size;
            constructionGuard.Release();
            allocationGuard.Release();
        }

        //=== Observers ===//

        /// @brief Returns the number of constructed elements.
        [[nodiscard]] UIntSize Size() const noexcept
        {
            return m_size;
        }
        /// @brief Returns the number of elements that fit without reallocating.
        [[nodiscard]] UIntSize Capacity() const noexcept
        {
            return m_capacity;
        }
        /// @brief Returns the allocator stored by the vector.
        [[nodiscard]] Alloc& GetAllocator() noexcept
        {
            return m_alloc;
        }
        /// @brief Returns the allocator stored by the vector.
        [[nodiscard]] const Alloc& GetAllocator() const noexcept
        {
            return m_alloc;
        }

        /// @brief Returns the element at an index with bounds checking.
        /// @param idx Zero-based element index.
        /// @throws std::out_of_range If `idx` is not less than `Size()`.
        T& At(UIntSize idx)
        {
            if (idx >= m_size)
                throw std::out_of_range("Vector::At: index out of range");
            return m_data[idx];
        }
        /// @brief Returns the element at an index with bounds checking.
        /// @param idx Zero-based element index.
        /// @throws std::out_of_range If `idx` is not less than `Size()`.
        const T& At(UIntSize idx) const
        {
            if (idx >= m_size)
                throw std::out_of_range("Vector::At: index out of range");
            return m_data[idx];
        }

        /// @brief Returns the element at an index without bounds checking.
        /// @param idx Zero-based element index that must be less than `Size()`.
        T& operator[](UIntSize idx)
        {
            return m_data[idx];
        }
        /// @brief Returns the element at an index without bounds checking.
        /// @param idx Zero-based element index that must be less than `Size()`.
        const T& operator[](UIntSize idx) const
        {
            return m_data[idx];
        }

        //=== Iterators & data ===//

        /// @brief Returns a pointer to the contiguous element storage.
        [[nodiscard]] T* data() noexcept
        {
            return m_data;
        }
        /// @brief Returns a pointer to the contiguous element storage.
        [[nodiscard]] const T* data() const noexcept
        {
            return m_data;
        }
        /// @brief Returns an iterator to the first element.
        [[nodiscard]] T* begin() noexcept
        {
            return m_data;
        }
        /// @brief Returns an iterator to the first element.
        [[nodiscard]] const T* begin() const noexcept
        {
            return m_data;
        }
        /// @brief Returns an iterator one past the final element.
        [[nodiscard]] T* end() noexcept
        {
            return m_data ? m_data + m_size : nullptr;
        }
        /// @brief Returns an iterator one past the final element.
        [[nodiscard]] const T* end() const noexcept
        {
            return m_data ? m_data + m_size : nullptr;
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
            NGIN::Memory::detail::ConstructionGuard<T> constructionGuard {destination};
            if constexpr (Meta::TypeTraits<T>::IsBitwiseRelocatable() &&
                          std::is_same_v<std::remove_cv_t<SourceType>, T>)
            {
                if (count != 0)
                    std::memcpy(destination, source, count * sizeof(T));
            }
            else
            {
                for (UIntSize index = 0; index < count; ++index)
                {
                    if constexpr (std::is_const_v<SourceType>)
                        std::construct_at(destination + index, source[index]);
                    else
                        ConstructRelocated(destination + index, source[index]);
                    constructionGuard.Increment();
                }
            }
            constructionGuard.Release();
        }

        [[nodiscard]] static std::size_t StorageBytes(const UIntSize capacity)
        {
            std::size_t bytes = 0;
            if (!NGIN::Memory::detail::CheckedMultiply(capacity, sizeof(T), bytes))
                throw std::length_error("Vector storage size overflow");
            return bytes;
        }

        T* AllocateStorage(UIntSize capacity)
        {
            const std::size_t bytes = StorageBytes(capacity);
            void*             mem   = m_alloc.Allocate(bytes, alignof(T));
            if (!mem)
                throw std::bad_alloc();
            return static_cast<T*>(mem);
        }

        void InitializeFromCopy(const T* source, const UIntSize count)
        {
            if (count == 0)
                return;

            const std::size_t                            allocationBytes = StorageBytes(count);
            T*                                           newData         = AllocateStorage(count);
            NGIN::Memory::detail::AllocationGuard<Alloc> allocationGuard {
                    m_alloc, newData, allocationBytes, alignof(T)};
            ConstructElements(newData, source, count);

            m_data     = newData;
            m_size     = count;
            m_capacity = count;
            allocationGuard.Release();
        }

        static void ConstructRelocated(T* destination, T& source)
        {
            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
                std::construct_at(destination, std::move(source));
            else
                std::construct_at(destination, source);
        }

        [[nodiscard]] UIntSize NextCapacity(const UIntSize minimum) const
        {
            const UIntSize maximum = (std::numeric_limits<std::size_t>::max)() / sizeof(T);
            if (minimum > maximum)
                throw std::length_error("Vector capacity overflow");

            if (m_capacity == 0)
                return minimum > 1 ? minimum : 1;

            std::size_t candidate = 0;
            if (!NGIN::Memory::detail::CheckedAdd(m_capacity, m_capacity >> 1, candidate) ||
                !NGIN::Memory::detail::CheckedAdd(candidate, 1, candidate))
            {
                candidate = maximum;
            }
            if (candidate < minimum)
                candidate = minimum;
            if (candidate > maximum)
                candidate = maximum;
            return candidate;
        }

        [[nodiscard]] UIntSize MinimumCapacityForOne() const
        {
            const UIntSize maximum = (std::numeric_limits<std::size_t>::max)() / sizeof(T);
            if (m_size == maximum)
                throw std::length_error("Vector capacity overflow");
            return m_size + 1;
        }

        void InsertPrepared(const UIntSize index, T&& staged)
        {
            if (index == m_size)
            {
                if (m_size == m_capacity)
                    Reserve(NextCapacity(MinimumCapacityForOne()));
                std::construct_at(m_data + m_size, std::move(staged));
                ++m_size;
                return;
            }

            if (m_size < m_capacity)
            {
                if constexpr (Meta::TypeTraits<T>::IsBitwiseRelocatable() &&
                              std::is_nothrow_move_constructible_v<T>)
                {
                    const UIntSize tailCount = m_size - index;
                    std::memmove(
                            static_cast<void*>(m_data + index + 1),
                            static_cast<void*>(m_data + index),
                            tailCount * sizeof(T));
                    std::construct_at(m_data + index, std::move(staged));
                    ++m_size;
                    return;
                }
                else if constexpr (std::is_nothrow_move_constructible_v<T> &&
                                   std::is_nothrow_move_assignable_v<T>)
                {
                    std::construct_at(m_data + m_size, std::move(m_data[m_size - 1]));
                    for (UIntSize position = m_size - 1; position > index; --position)
                        m_data[position] = std::move(m_data[position - 1]);
                    m_data[index] = std::move(staged);
                    ++m_size;
                    return;
                }
            }

            const UIntSize newCapacity =
                    m_size == m_capacity ? NextCapacity(MinimumCapacityForOne()) : m_capacity;
            const std::size_t                            allocationBytes = StorageBytes(newCapacity);
            T*                                           newData         = AllocateStorage(newCapacity);
            NGIN::Memory::detail::AllocationGuard<Alloc> allocationGuard {
                    m_alloc, newData, allocationBytes, alignof(T)};

            if constexpr (Meta::TypeTraits<T>::IsBitwiseRelocatable())
            {
                NGIN::Memory::detail::ObjectConstructionGuard<T> insertedGuard {newData + index};
                std::construct_at(newData + index, std::move(staged));
                insertedGuard.MarkConstructed();
                if (index > 0)
                    std::memcpy(newData, m_data, index * sizeof(T));
                const UIntSize suffixCount = m_size - index;
                if (suffixCount > 0)
                    std::memcpy(newData + index + 1, m_data + index, suffixCount * sizeof(T));
                insertedGuard.Release();
            }
            else
            {
                NGIN::Memory::detail::ConstructionGuard<T> constructionGuard {newData};
                for (UIntSize sourceIndex = 0; sourceIndex < index; ++sourceIndex)
                {
                    ConstructRelocated(newData + sourceIndex, m_data[sourceIndex]);
                    constructionGuard.Increment();
                }
                std::construct_at(newData + index, std::move(staged));
                constructionGuard.Increment();
                for (UIntSize sourceIndex = index; sourceIndex < m_size; ++sourceIndex)
                {
                    ConstructRelocated(newData + sourceIndex + 1, m_data[sourceIndex]);
                    constructionGuard.Increment();
                }
                constructionGuard.Release();
                DestroyElements(m_data, m_size);
            }

            if (m_data)
                m_alloc.Deallocate(m_data, m_capacity * sizeof(T), alignof(T));
            m_data     = newData;
            m_capacity = newCapacity;
            ++m_size;
            allocationGuard.Release();
        }

        void AssignFromCopy(const Vector& other)
        {
            if constexpr (NGIN::Memory::AllocatorPropagationTraits<Alloc>::PropagateOnCopyAssignment)
            {
                ReleaseStorage();
                m_alloc = other.m_alloc;
                InitializeFromCopy(other.m_data, other.m_size);
                return;
            }

            if (other.m_size > m_capacity)
            {
                T*                                           newData = AllocateStorage(other.m_size);
                NGIN::Memory::detail::AllocationGuard<Alloc> allocationGuard {
                        m_alloc, newData, StorageBytes(other.m_size), alignof(T)};
                ConstructElements(newData, other.m_data, other.m_size);

                ReleaseStorage();
                m_data     = newData;
                m_size     = other.m_size;
                m_capacity = other.m_size;
                allocationGuard.Release();
                return;
            }

            const UIntSize originalSize = m_size;
            const UIntSize commonSize   = (std::min) (m_size, other.m_size);
            UIntSize       index        = 0;
            for (; index < commonSize; ++index)
                m_data[index] = other.m_data[index];
            for (; index < other.m_size; ++index)
            {
                std::construct_at(m_data + index, other.m_data[index]);
                ++m_size;
            }

            if (originalSize > other.m_size)
                DestroyElements(m_data + other.m_size, originalSize - other.m_size);

            m_size = other.m_size;
        }

        void AssignFromMoved(Vector& other)
        {
            if (other.m_size > m_capacity)
            {
                T*                                           newData = AllocateStorage(other.m_size);
                NGIN::Memory::detail::AllocationGuard<Alloc> allocationGuard {
                        m_alloc, newData, StorageBytes(other.m_size), alignof(T)};
                ConstructElements(newData, other.m_data, other.m_size);

                ReleaseStorage();
                m_data     = newData;
                m_size     = other.m_size;
                m_capacity = other.m_size;
                allocationGuard.Release();
            }
            else
            {
                const UIntSize originalSize = m_size;
                const UIntSize commonSize   = (std::min) (m_size, other.m_size);
                UIntSize       index        = 0;
                for (; index < commonSize; ++index)
                    m_data[index] = std::move(other.m_data[index]);
                for (; index < other.m_size; ++index)
                {
                    std::construct_at(m_data + index, std::move(other.m_data[index]));
                    ++m_size;
                }
                if (originalSize > other.m_size)
                    DestroyElements(m_data + other.m_size, originalSize - other.m_size);
                m_size = other.m_size;
            }

            other.Clear();
        }

        NGIN_NO_UNIQUE_ADDRESS Alloc m_alloc {};
        T*                           m_data {nullptr};
        UIntSize                     m_size {0};
        UIntSize                     m_capacity {0};
    };
}// namespace NGIN::Containers
