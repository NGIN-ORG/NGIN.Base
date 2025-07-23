/// @file IAllocator.hpp
/// @brief Declaration of the IAllocator interface and helper functions for object creation.
///
/// @details
/// Provides a runtime-polymorphic interface for memory allocators, as well as
/// helper functions for creating and deleting single objects or arrays with those allocators.

#pragma once

#include <cstddef>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <NGIN/Primitives.hpp>

namespace NGIN::Memory
{
    class MemoryBlock
    {
    public:
        using PtrType = void*;

        PtrType ptr = nullptr;
        UInt64 size = 0;

    public:
        MemoryBlock() = default;
        MemoryBlock(PtrType ptr, UInt64 size)
            : ptr(ptr), size(size)
        {
        }

        char operator[](Int64 index) const
        {
            if (index < 0 || index >= size)
                throw std::out_of_range("MemoryBlock index out of range");
            return *(static_cast<char*>(ptr) + index);
        }

        char& operator[](Int64 index)
        {
            if (index < 0 || index >= size)
                throw std::out_of_range("MemoryBlock index out of range");
            return *(static_cast<char*>(ptr) + index);
        }

        template<typename T>
        T* As() const
        {
            return static_cast<T*>(ptr);
        }

        operator void*() const
        {
            return ptr;
        }
    };

    /// @class IAllocator
    /// @brief Abstract interface for allocators providing aligned allocation and deallocation.
    ///
    /// @details
    /// `IAllocator` defines a pure virtual interface for memory allocators.
    /// Classes inheriting from `IAllocator` must implement:
    /// - Allocate(size, alignment)
    /// - Deallocate(ptr)
    /// - Reset()
    /// - Owns(ptr)
    class IAllocator
    {
    public:
        /// @brief Virtual destructor for proper cleanup of derived allocators.
        virtual ~IAllocator() = default;

        /// @brief Allocates a block of memory of the given size and alignment.
        /// @param size The size of the block in bytes.
        /// @param alignment The required alignment (must be a power of two).
        /// @return Pointer to the allocated memory.
        virtual MemoryBlock Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) = 0;

        /// @brief Deallocates a block of memory previously allocated by `Allocate`.
        /// @param ptr Pointer to the memory block to deallocate.
        virtual void Deallocate(void* ptr) noexcept = 0;

        /// @brief Resets the allocator to its initial state (if applicable).
        virtual void Reset() noexcept = 0;

        /// @brief Checks if this allocator owns the given pointer (if applicable).
        /// @param ptr The pointer to check.
        /// @return `true` if owned, `false` otherwise.
        virtual bool Owns(const void* ptr) const noexcept = 0;

        /// @brief Returns the total capacity of the allocator in bytes.
        virtual std::size_t GetCapacity() const noexcept = 0;

        /// @brief Returns how many bytes are currently used.
        virtual std::size_t GetUsedSize() const noexcept = 0;
    };

    ////////////////////////////////////////////////////////////////////////////////
    // Helper Functions: New / Delete
    ////////////////////////////////////////////////////////////////////////////////

    /// @brief Constructs a single object of type T using the provided allocator.
    /// @tparam T The type of the object to construct.
    /// @tparam Args Parameter pack for T's constructor.
    /// @param allocator A reference to an IAllocator instance.
    /// @param args Arguments forwarded to T's constructor.
    /// @return Pointer to the newly constructed object.
    template<typename T, typename... Args>
    T* New(IAllocator& allocator, Args&&... args)
    {
        /// @brief Allocate memory for one T object with proper alignment.
        MemoryBlock memBlock = allocator.Allocate(sizeof(T), alignof(T));
        /// @brief Construct T in-place using placement-new.
        if (!memBlock.ptr)
            return nullptr;
        return new (memBlock.ptr) T(std::forward<Args>(args)...);
    }


    /// @brief Destroys an object of type T and deallocates its memory using the provided allocator.
    /// @tparam T The type of the object to destroy.
    /// @param allocator A reference to an IAllocator.
    /// @param ptr Pointer to the object to destroy and deallocate.
    template<typename T>
    void Delete(IAllocator& allocator, T* ptr)
    {
        if (!ptr)
            return;

        /// @brief Call the destructor explicitly.
        ptr->~T();
        /// @brief Return memory to the allocator.
        // Assuming you can retrieve the MemoryBlock from ptr if needed
        allocator.Deallocate(ptr);
    }


    ////////////////////////////////////////////////////////////////////////////////
    // Helper Functions: NewArray / DeleteArray
    ////////////////////////////////////////////////////////////////////////////////

    /// @struct ArrayHeader
    /// @brief Stores metadata for array allocations, such as the element count.
    struct ArrayHeader
    {
        /// @brief The number of elements in the array.
        std::size_t count;
    };

    /// @brief Constructs an array of T using the provided allocator.
    /// @tparam T The type of each element in the array.
    /// @param allocator A reference to an IAllocator instance.
    /// @param count The number of elements to allocate and construct.
    /// @return Pointer to the first element in the newly constructed array.
    template<typename T>
    T* NewArray(IAllocator& allocator, std::size_t count)
    {
        if (count == 0)
        {
            return nullptr;
        }

        /// @brief Calculate total size: header + count*T
        const std::size_t totalSize = sizeof(ArrayHeader) + (count * sizeof(T));

        /// @brief Allocate memory, aligned for T.
        void* rawMem = allocator.Allocate(totalSize, alignof(T));
        if (!rawMem)
        {
            throw std::bad_alloc {};
        }

        /// @brief Place the header at the start.
        auto header   = static_cast<ArrayHeader*>(rawMem);
        header->count = count;

        /// @brief The actual array starts right after the header.
        T* arrayPtr = reinterpret_cast<T*>(header + 1);

        /// @brief Construct each element via placement-new.
        for (std::size_t i = 0; i < count; ++i)
        {
            new (&arrayPtr[i]) T();
        }

        return arrayPtr;
    }

    /// @brief Destroys an array of T and deallocates its memory using the provided allocator.
    /// @tparam T The type of each element in the array.
    /// @param allocator A reference to an IAllocator instance.
    /// @param ptr Pointer to the first element in the array to destroy.
    template<typename T>
    void DeleteArray(IAllocator& allocator, T* ptr)
    {
        if (!ptr)
        {
            return;
        }

        /// @brief Recover the header by stepping back one header size.
        auto header       = reinterpret_cast<ArrayHeader*>(ptr) - 1;
        std::size_t count = header->count;

        /// @brief Call the destructor on each element.
        for (std::size_t i = count; i > 0; --i)
        {
            ptr[i - 1].~T();
        }

        /// @brief Return the entire block (header + array) to the allocator.
        allocator.Deallocate(header);
    }

}// namespace NGIN::Memory
