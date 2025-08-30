/// @file LinearAllocator.hpp
/// @brief Declaration of the LinearAllocator class, an arena-style allocator.
///
/// @details
/// A LinearAllocator manages a single contiguous block of memory in a linear fashion.
/// Allocation is fast and sequential, but no per-allocation deallocation is supported.
/// You can only free all allocations at once by calling `Reset()`.

#pragma once

#include <NGIN/Memory/Mallocator.hpp>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace NGIN::Memory
{
    /// @brief An arena-style allocator which allocates from a contiguous memory block in a linear fashion.
    /// @details
    ///  - Fast allocation by bumping a pointer.
    ///  - No per-allocation deallocation.
    ///  - All allocations can be freed at once with Reset().
    class LinearAllocator : public Mallocator
    {
    public:
        /// @brief Constructor that disallows default creation. A memory block or capacity must be specified.
        LinearAllocator() = delete;

        /// @brief Construct from an existing memory block (unowned).
        /// @param block A pre-allocated memory block to use.
        explicit LinearAllocator(MemoryBlock block);

        /// @brief Construct by allocating a new block of the given capacity. (Owned)
        /// @param capacity Size in bytes of the block to allocate.
        explicit LinearAllocator(std::size_t capacity = 1024);

        /// @brief Disallow copy-construction.
        LinearAllocator(const LinearAllocator&) = delete;
        /// @brief Disallow copy-assignment.
        LinearAllocator& operator=(const LinearAllocator&) = delete;

        /// @brief Allow move-construction.
        LinearAllocator(LinearAllocator&& other) noexcept;
        /// @brief Allow move-assignment.
        LinearAllocator& operator=(LinearAllocator&& other) noexcept;

        /// @brief Destructor.
        /// @note Does not free memory if constructed with a borrowed MemoryBlock.
        ///       If allocated internally, it will free that memory.
        virtual ~LinearAllocator();

        /// @brief Allocates a block of memory of the given size and alignment.
        /// @param size The size of the block in bytes.
        /// @param alignment The required alignment (must be a power of two).
        /// @return Pointer to the allocated memory.
        MemoryBlock Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) override;

        /// @brief LinearAllocator does not support deallocation of individual blocks.
        /// @param ptr The pointer to deallocate (ignored).
        void Deallocate(void* ptr) noexcept override {}

        /// @brief Resets the allocator to the beginning of its block.
        /// @details All previously allocated blocks are invalidated.
        void Reset() noexcept override;

        /// @brief Checks if this allocator owns the given pointer.
        /// @param ptr The pointer to check.
        /// @return True if within the allocator's memory range, false otherwise.
        bool Owns(const void* ptr) const noexcept override;

        /// @brief Returns the total number of bytes allocated so far (beyond the base pointer).
        /// @return The number of bytes used.
        std::size_t GetUsedSize() const noexcept override;

        /// @brief Returns the total capacity of the managed memory block.
        /// @return The capacity in bytes.
        std::size_t GetCapacity() const noexcept override;

    private:
        /// @brief The pointer to the beginning of the memory block.
        void* m_basePtr = nullptr;

        /// @brief The pointer to the current allocation position.
        void* m_currentPtr = nullptr;

        /// @brief The total capacity (in bytes) of the memory block.
        std::size_t m_capacity = 0;
    };

    /// @brief Construct using an existing, user-managed MemoryBlock.
    /// @param block The pre-allocated memory block to use.
    inline LinearAllocator::LinearAllocator(MemoryBlock block)
        : m_basePtr(block.ptr), m_currentPtr(block.ptr), m_capacity(block.size)
    {
        // No new allocation here, we just borrow the memory.
    }

    /// @brief Construct by allocating a new block of the given capacity.
    /// @param capacity The size in bytes of the block to allocate.
    inline LinearAllocator::LinearAllocator(std::size_t capacity)
        : m_basePtr(nullptr), m_currentPtr(nullptr), m_capacity(capacity)
    {
        // Allocate using Mallocator, store base pointer, current pointer starts there
        MemoryBlock block = Mallocator::Allocate(capacity);
        m_basePtr         = block.ptr;
        m_currentPtr      = block.ptr;
    }

    /// @brief Move-constructor.
    /// @param other The LinearAllocator to move from.
    inline LinearAllocator::LinearAllocator(LinearAllocator&& other) noexcept
        : m_basePtr(std::exchange(other.m_basePtr, nullptr)), m_currentPtr(std::exchange(other.m_currentPtr, nullptr)), m_capacity(std::exchange(other.m_capacity, 0))
    {
    }

    /// @brief Move-assignment operator.
    /// @param other The LinearAllocator to move from.
    /// @return Reference to this object.
    inline LinearAllocator& LinearAllocator::operator=(LinearAllocator&& other) noexcept
    {
        if (this != &other)
        {
            m_basePtr    = std::exchange(other.m_basePtr, nullptr);
            m_currentPtr = std::exchange(other.m_currentPtr, nullptr);
            m_capacity   = std::exchange(other.m_capacity, 0);
        }
        return *this;
    }

    /// @brief Destructor.
    inline LinearAllocator::~LinearAllocator()
    {
    }

    /// @brief Allocate a block of memory from the linear region.
    /// @param size The size of the block in bytes.
    /// @param alignment The required alignment (power of two).
    /// @return A MemoryBlock pointing to the allocated region.
    inline MemoryBlock LinearAllocator::Allocate(std::size_t size, std::size_t alignment)
    {
        if (!m_basePtr)
            return {nullptr, 0};

        // Bump-pointer alignment
        const std::uintptr_t currentAddr = reinterpret_cast<std::uintptr_t>(m_currentPtr);
        const std::uintptr_t alignedAddr = (currentAddr + alignment - 1) & ~(alignment - 1);
        const std::size_t padding        = alignedAddr - currentAddr;
        const std::size_t bytesNeeded    = padding + size;

        const std::uintptr_t baseAddr = reinterpret_cast<std::uintptr_t>(m_basePtr);
        const std::size_t usedSoFar   = currentAddr - baseAddr;
        const std::size_t totalNeeded = usedSoFar + bytesNeeded;

        if (totalNeeded > m_capacity)
            return {nullptr, 0};


        void* userPtr = reinterpret_cast<void*>(alignedAddr);
        m_currentPtr  = reinterpret_cast<void*>(alignedAddr + size);

        return {userPtr, size};
    }

    /// @brief Resets the allocator back to the base pointer.
    inline void LinearAllocator::Reset() noexcept
    {
        m_currentPtr = m_basePtr;
    }

    /// @brief Check if the pointer is within the range of [basePtr, basePtr + capacity).
    /// @param ptr The pointer to check.
    /// @return True if owned by this allocator, false otherwise.
    inline bool LinearAllocator::Owns(const void* ptr) const noexcept
    {
        if (!m_basePtr || !ptr)
        {
            return false;
        }

        std::uintptr_t baseAddr = reinterpret_cast<std::uintptr_t>(m_basePtr);
        std::uintptr_t check    = reinterpret_cast<std::uintptr_t>(ptr);
        return (check >= baseAddr) && (check < (baseAddr + m_capacity));
    }

    /// @brief Get the total number of bytes used so far.
    /// @return The used size in bytes.
    inline std::size_t LinearAllocator::GetUsedSize() const noexcept
    {
        const std::uintptr_t baseAddr    = reinterpret_cast<std::uintptr_t>(m_basePtr);
        const std::uintptr_t currentAddr = reinterpret_cast<std::uintptr_t>(m_currentPtr);
        return static_cast<std::size_t>(currentAddr - baseAddr);
    }

    /// @brief Get the total capacity of this allocator.
    /// @return The capacity in bytes.
    inline std::size_t LinearAllocator::GetCapacity() const noexcept
    {
        return m_capacity;
    }

}// namespace NGIN::Memory
