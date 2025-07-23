/// @file Vector.hpp
/// @brief Declaration and inline implementation of the Vector container class.
/// @details
/// A dynamically resizable array-like container that uses an externally-owned
/// allocator and stores elements contiguously.  If you default-construct,
/// it assumes `Allocator::Instance()` exists; otherwise you must supply a
/// reference to an allocator yourself.
#pragma once

#include <NGIN/Memory/Mallocator.hpp>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace NGIN::Containers
{
    /// @tparam T         Element type.
    /// @tparam Allocator Must implement IAllocator and provide
    ///                   `static Allocator& Instance()`.
    template<typename T, typename Allocator = NGIN::Memory::IAllocator>
    class Vector
    {
    public:
        /// @brief Default constructor: uses Allocator::Instance().
        /// @note Fails to compile if `Allocator::Instance()` doesn't exist.
        Vector(std::size_t initialCapacity = 0)
            requires requires { Allocator::Instance(); }
            : m_allocator(Allocator::Instance()), m_data(nullptr), m_size(0), m_capacity(0)
        {
            Reserve(initialCapacity);
        }


        /// @brief Default constructor: uses Mallocator::Instance() if Allocator is IAllocator.
        Vector(std::size_t initialCapacity = 0)
            requires(std::is_same_v<Allocator, NGIN::Memory::IAllocator>)
            : m_allocator(NGIN::Memory::Mallocator::Instance()), m_data(nullptr), m_size(0), m_capacity(0)
        {
            Reserve(initialCapacity);
        }

        /// @brief Construct with an explicit external allocator.
        Vector(Allocator& allocator, std::size_t initialCapacity = 0)
            : m_allocator(allocator), m_data(nullptr), m_size(0), m_capacity(0)
        {
            Reserve(initialCapacity);
        }

        /// @brief Initializer-list constructor (uses global Instance).
        Vector(std::initializer_list<T> list)
            requires requires { Allocator::Instance(); }
            : Vector(Allocator::Instance(), list.size())
        {
            for (auto& elem: list)
            {
                new (m_data + m_size) T(elem);
                ++m_size;
            }
        }

        /// @brief Copy constructor: copies elements but keeps the same allocator.
        Vector(const Vector& other)
            : m_allocator(other.m_allocator), m_data(nullptr), m_size(0), m_capacity(0)
        {
            Reserve(other.m_capacity);
            for (std::size_t i = 0; i < other.m_size; ++i)
            {
                new (m_data + i) T(other.m_data[i]);
                ++m_size;
            }
        }

        /// @brief Move constructor: steals storage (uses same allocator).
        Vector(Vector&& other) noexcept
            : m_allocator(other.m_allocator), m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity)
        {
            other.m_data     = nullptr;
            other.m_size     = 0;
            other.m_capacity = 0;
        }

        /// @brief Destructor: destroys elements and deallocates memory.
        ~Vector()
        {
            Destroy();
            if (m_data)
                m_allocator.Deallocate(m_data);
        }

        /// @brief Copy-assign: deep-copy elements into this container.
        Vector& operator=(const Vector& other)
        {
            if (this != &other)
            {
                Destroy();
                if (m_data)
                    m_allocator.Deallocate(m_data);

                m_data     = nullptr;
                m_capacity = 0;
                Reserve(other.m_capacity);

                for (std::size_t i = 0; i < other.m_size; ++i)
                    new (m_data + i) T(other.m_data[i]);
                m_size = other.m_size;
            }
            return *this;
        }

        /// @brief Move-assign: deep-move elements into this container.
        Vector& operator=(Vector&& other) noexcept
        {
            if (this != &other)
            {
                Destroy();
                if (m_data)
                    m_allocator.Deallocate(m_data);

                m_data     = other.m_data;
                m_size     = other.m_size;
                m_capacity = other.m_capacity;

                other.m_data     = nullptr;
                other.m_size     = 0;
                other.m_capacity = 0;
            }
            return *this;
        }

        //=== Element modifiers ===//

        /// @brief Push by copy.
        void PushBack(const T& value)
        {
            EnsureCapacityForOne();
            new (m_data + m_size) T(value);
            ++m_size;
        }

        /// @brief Push by move.
        void PushBack(T&& value)
        {
            EnsureCapacityForOne();
            new (m_data + m_size) T(std::move(value));
            ++m_size;
        }

        /// @brief In-place construct at the end.
        template<typename... Args>
        void EmplaceBack(Args&&... args)
        {
            EnsureCapacityForOne();
            new (m_data + m_size) T(std::forward<Args>(args)...);
            ++m_size;
        }

        /// @brief Insert by copy at index (shifts elements right).
        void PushAt(std::size_t index, const T& value)
        {
            if (index > m_size)
                throw std::out_of_range("Vector::PushAt: index out of range");
            EnsureCapacityForOne();
            for (std::size_t i = m_size; i > index; --i)
            {
                new (m_data + i) T(std::move(m_data[i - 1]));
                m_data[i - 1].~T();
            }
            new (m_data + index) T(value);
            ++m_size;
        }

        /// @brief Insert by move at index (shifts elements right).
        void PushAt(std::size_t index, T&& value)
        {
            if (index > m_size)
                throw std::out_of_range("Vector::PushAt: index out of range");
            EnsureCapacityForOne();
            for (std::size_t i = m_size; i > index; --i)
            {
                new (m_data + i) T(std::move(m_data[i - 1]));
                m_data[i - 1].~T();
            }
            new (m_data + index) T(std::move(value));
            ++m_size;
        }

        /// @brief In-place insert at index (shifts elements right).
        template<typename... Args>
        void EmplaceAt(std::size_t index, Args&&... args)
        {
            if (index > m_size)
                throw std::out_of_range("Vector::EmplaceAt: index out of range");
            EnsureCapacityForOne();
            for (std::size_t i = m_size; i > index; --i)
            {
                new (m_data + i) T(std::move(m_data[i - 1]));
                m_data[i - 1].~T();
            }
            new (m_data + index) T(std::forward<Args>(args)...);
            ++m_size;
        }

        /// @brief Pop the last element.
        void PopBack()
        {
            if (m_size == 0)
                throw std::out_of_range("Vector::PopBack: vector is empty");
            m_data[m_size - 1].~T();
            --m_size;
        }

        /// @brief Erase at index (shifts down).
        void Erase(std::size_t index)
        {
            if (index >= m_size)
                throw std::out_of_range("Vector::Erase: index out of range");
            m_data[index].~T();
            for (std::size_t i = index; i + 1 < m_size; ++i)
            {
                new (m_data + i) T(std::move(m_data[i + 1]));
                m_data[i + 1].~T();
            }
            --m_size;
        }

        /// @brief Remove all elements (capacity remains).
        void Clear()
        {
            Destroy();
            m_size = 0;
        }

        //=== Capacity management ===//

        /// @brief Ensure at least `newCapacity` slots.
        void Reserve(std::size_t newCapacity)
        {
            if (newCapacity <= m_capacity)
                return;

            auto block = m_allocator.Allocate(newCapacity * sizeof(T), alignof(T));
            T* newData = reinterpret_cast<T*>(block.ptr);
            if (!newData)
                throw std::bad_alloc();

            // Move-construct old elements into new buffer
            std::size_t i = 0;
            try
            {
                for (; i < m_size; ++i)
                    new (newData + i) T(std::move(m_data[i]));
            } catch (...)
            {
                for (std::size_t j = 0; j < i; ++j)
                    newData[j].~T();
                m_allocator.Deallocate(newData);
                throw;
            }

            // Destroy old and free
            DestroyElements();
            if (m_data)
                m_allocator.Deallocate(m_data);

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
                    m_allocator.Deallocate(m_data);
                    m_data = nullptr;
                }
                m_capacity = 0;
                return;
            }

            auto block = m_allocator.Allocate(m_size * sizeof(T), alignof(T));
            T* newData = reinterpret_cast<T*>(block.ptr);
            for (std::size_t i = 0; i < m_size; ++i)
            {
                new (newData + i) T(std::move(m_data[i]));
                m_data[i].~T();
            }
            m_allocator.Deallocate(m_data);

            m_data     = newData;
            m_capacity = m_size;
        }

        //=== Observers ===//

        [[nodiscard]] std::size_t Size() const noexcept
        {
            return m_size;
        }
        [[nodiscard]] std::size_t Capacity() const noexcept
        {
            return m_capacity;
        }

        T& At(std::size_t idx)
        {
            if (idx >= m_size)
                throw std::out_of_range("Vector::At: index out of range");
            return m_data[idx];
        }
        const T& At(std::size_t idx) const
        {
            if (idx >= m_size)
                throw std::out_of_range("Vector::At: index out of range");
            return m_data[idx];
        }

        T& operator[](std::size_t idx)
        {
            return m_data[idx];
        }
        const T& operator[](std::size_t idx) const
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
        inline void DestroyElements() noexcept
        {
            for (std::size_t i = 0; i < m_size; ++i)
                m_data[i].~T();
        }

        /// Destroy all elements (does _not_ deallocate).
        inline void Destroy()
        {
            DestroyElements();
            m_size = 0;
        }

        /// Grow by one slot if needed.
        inline void EnsureCapacityForOne()
        {
            if (m_size >= m_capacity)
                Reserve(m_capacity ? m_capacity * 2 : 1);
        }

        Allocator& m_allocator;
        T* m_data;
        std::size_t m_size;
        std::size_t m_capacity;
    };

}// namespace NGIN::Containers
