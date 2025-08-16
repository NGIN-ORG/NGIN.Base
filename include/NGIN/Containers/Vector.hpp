/// @file Vector.hpp
/// @brief Declaration and inline implementation of the Vector container class.
/// @details
/// A dynamically resizable array-like container that uses an externally-owned
/// allocator and stores elements contiguously.  If you default-construct,
/// it assumes `Allocator::Instance()` exists; otherwise you must supply a
/// reference to an allocator yourself.
#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <cstddef>
#include <initializer_list>
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

        Vector() noexcept = default;
        explicit Vector(std::size_t initialCapacity, Alloc alloc = Alloc {}) : alloc_(std::move(alloc))
        {
            if (initialCapacity)
                Reserve(initialCapacity);
        }
        Vector(std::initializer_list<T> init, Alloc alloc = Alloc {}) : alloc_(std::move(alloc))
        {
            Reserve(init.size());
            for (auto& v: init)
                ::new (&data_[size_++]) T(v);
        }
        Vector(const Vector& other) : alloc_(other.alloc_)
        {
            Reserve(other.size_);
            for (std::size_t i = 0; i < other.size_; ++i)
                ::new (&data_[i]) T(other.data_[i]);
            size_ = other.size_;
        }
        Vector& operator=(const Vector& other)
        {
            if (this != &other)
            {
                Clear();
                if (data_)
                    alloc_.Deallocate(data_, capacity_ * sizeof(T), alignof(T));
                data_     = nullptr;
                capacity_ = size_ = 0;
                Reserve(other.size_);
                for (std::size_t i = 0; i < other.size_; ++i)
                    ::new (&data_[i]) T(other.data_[i]);
                size_ = other.size_;
            }
            return *this;
        }
        Vector(Vector&& other) noexcept
            : alloc_(std::move(other.alloc_)), data_(other.data_), size_(other.size_), capacity_(other.capacity_)
        {
            other.data_ = nullptr;
            other.size_ = other.capacity_ = 0;
        }
        Vector& operator=(Vector&& other) noexcept
        {
            if (this != &other)
            {
                Clear();
                if (data_)
                    alloc_.Deallocate(data_, capacity_ * sizeof(T), alignof(T));
                alloc_      = std::move(other.alloc_);
                data_       = other.data_;
                size_       = other.size_;
                capacity_   = other.capacity_;
                other.data_ = nullptr;
                other.size_ = other.capacity_ = 0;
            }
            return *this;
        }
        ~Vector()
        {
            Clear();
            if (data_)
                alloc_.Deallocate(data_, capacity_ * sizeof(T), alignof(T));
        }

        //=== Element modifiers ===//

        /// @brief Push by copy.
        void PushBack(const T& value)
        {
            EnsureCapacityForOne();
            ::new (&data_[size_++]) T(value);
        }

        /// @brief Push by move.
        void PushBack(T&& value)
        {
            EnsureCapacityForOne();
            ::new (&data_[size_++]) T(std::move(value));
        }

        /// @brief In-place construct at the end.
        template<typename... Args>
        void EmplaceBack(Args&&... args)
        {
            EnsureCapacityForOne();
            ::new (&data_[size_++]) T(std::forward<Args>(args)...);
        }

        /// @brief Insert by copy at index (shifts elements right).
        void PushAt(UIntSize index, const T& value)
        {
            if (index > size_)
                throw std::out_of_range("Vector::PushAt: index out of range");
            EnsureCapacityForOne();
            for (UIntSize i = size_; i > index; --i)
            {
                ::new (&data_[i]) T(std::move(data_[i - 1]));
                data_[i - 1].~T();
            }
            ::new (&data_[index]) T(value);
            ++size_;
        }

        /// @brief Insert by move at index (shifts elements right).
        void PushAt(UIntSize index, T&& value)
        {
            if (index > size_)
                throw std::out_of_range("Vector::PushAt: index out of range");
            EnsureCapacityForOne();
            for (UIntSize i = size_; i > index; --i)
            {
                ::new (&data_[i]) T(std::move(data_[i - 1]));
                data_[i - 1].~T();
            }
            ::new (&data_[index]) T(std::move(value));
            ++size_;
        }

        /// @brief In-place insert at index (shifts elements right).
        template<typename... Args>
        void EmplaceAt(UIntSize index, Args&&... args)
        {
            if (index > size_)
                throw std::out_of_range("Vector::EmplaceAt: index out of range");
            EnsureCapacityForOne();
            for (UIntSize i = size_; i > index; --i)
            {
                ::new (&data_[i]) T(std::move(data_[i - 1]));
                data_[i - 1].~T();
            }
            ::new (&data_[index]) T(std::forward<Args>(args)...);
            ++size_;
        }

        /// @brief Pop the last element.
        void PopBack()
        {
            if (size_ == 0)
                throw std::out_of_range("Vector::PopBack: vector is empty");
            data_[size_ - 1].~T();
            --size_;
        }

        /// @brief Erase at index (shifts down).
        void Erase(UIntSize index)
        {
            if (index >= size_)
                throw std::out_of_range("Vector::Erase: index out of range");
            data_[index].~T();
            for (UIntSize i = index; i + 1 < size_; ++i)
            {
                ::new (&data_[i]) T(std::move(data_[i + 1]));
                data_[i + 1].~T();
            }
            --size_;
        }

        /// @brief Remove all elements (capacity remains).
        void Clear() noexcept
        {
            for (std::size_t i = 0; i < size_; ++i)
                data_[i].~T();
            size_ = 0;
        }

        //=== Capacity management ===//

        /// @brief Ensure at least `newCapacity` slots.
        void Reserve(UIntSize newCapacity)
        {
            if (newCapacity <= capacity_)
                return;
            void* mem = alloc_.Allocate(newCapacity * sizeof(T), alignof(T));
            if (!mem)
                throw std::bad_alloc();
            T* newData = static_cast<T*>(mem);
            UIntSize i = 0;
            try
            {
                for (; i < size_; ++i)
                    ::new (&newData[i]) T(std::move(data_[i]));
            } catch (...)
            {
                for (UIntSize j = 0; j < i; ++j)
                    newData[j].~T();
                alloc_.Deallocate(newData, newCapacity * sizeof(T), alignof(T));
                throw;
            }
            for (UIntSize j = 0; j < size_; ++j)
                data_[j].~T();
            if (data_)
                alloc_.Deallocate(data_, capacity_ * sizeof(T), alignof(T));
            data_     = newData;
            capacity_ = newCapacity;
        }

        /// @brief Shrink capacity to match size.
        void ShrinkToFit()
        {
            if (size_ == capacity_)
                return;
            if (size_ == 0)
            {
                if (data_)
                {
                    alloc_.Deallocate(data_, capacity_ * sizeof(T), alignof(T));
                    data_ = nullptr;
                }
                capacity_ = 0;
                return;
            }
            void* mem = alloc_.Allocate(size_ * sizeof(T), alignof(T));
            if (!mem)
                throw std::bad_alloc();
            T* newData = static_cast<T*>(mem);
            for (UIntSize i = 0; i < size_; ++i)
            {
                ::new (&newData[i]) T(std::move(data_[i]));
                data_[i].~T();
            }
            alloc_.Deallocate(data_, capacity_ * sizeof(T), alignof(T));
            data_     = newData;
            capacity_ = size_;
        }

        //=== Observers ===//

        [[nodiscard]] UIntSize Size() const noexcept
        {
            return size_;
        }
        [[nodiscard]] UIntSize Capacity() const noexcept
        {
            return capacity_;
        }

        T& At(UIntSize idx)
        {
            if (idx >= size_)
                throw std::out_of_range("Vector::At: index out of range");
            return data_[idx];
        }
        const T& At(UIntSize idx) const
        {
            if (idx >= size_)
                throw std::out_of_range("Vector::At: index out of range");
            return data_[idx];
        }

        T& operator[](UIntSize idx)
        {
            return data_[idx];
        }
        const T& operator[](UIntSize idx) const
        {
            return data_[idx];
        }

        //=== Iterators & data ===//

        [[nodiscard]] T* data() noexcept
        {
            return data_;
        }
        [[nodiscard]] const T* data() const noexcept
        {
            return data_;
        }
        [[nodiscard]] T* begin() noexcept
        {
            return data_;
        }
        [[nodiscard]] const T* begin() const noexcept
        {
            return data_;
        }
        [[nodiscard]] T* end() noexcept
        {
            return data_ + size_;
        }
        [[nodiscard]] const T* end() const noexcept
        {
            return data_ + size_;
        }

    private:
        void EnsureCapacityForOne()
        {
            if (size_ >= capacity_)
                Reserve(capacity_ ? capacity_ * 2 : 1);
        }
        Alloc alloc_ {};
        T* data_ {nullptr};
        UIntSize size_ {0};
        UIntSize capacity_ {0};
    };

    // Vector2 removed; Vector now uses allocator concept directly.

}// namespace NGIN::Containers
