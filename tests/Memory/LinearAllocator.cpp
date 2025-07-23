/// @file LinearAllocatorTest.cpp
/// @brief Tests for NGIN::Memory::LinearAllocator using boost::ut
///
/// @details
/// This file contains a series of tests to verify the correctness of the
/// LinearAllocator class in various scenarios, including construction with
/// capacity, construction from an existing block, move semantics, allocation
/// success/failure, usage tracking, reset behavior, and pointer ownership checks.
/// We use boost::ut for our unit testing framework.

#include <boost/ut.hpp>
#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Memory/Mallocator.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

using namespace boost::ut;

namespace
{
    /// @brief Helper function to create a MemoryBlock with a raw array on the stack.
    /// @details Useful for testing borrowed memory scenarios.
    ///
    /// @tparam Size The size of the array in bytes.
    /// @return A MemoryBlock pointing to the array's beginning.
    template<std::size_t Size>
    NGIN::Memory::MemoryBlock CreateStackBlock()
    {
        static std::array<std::uint8_t, Size> buffer {};
        return {static_cast<void*>(buffer.data()), buffer.size()};
    }

}// end anonymous namespace

/// @brief Test suite for NGIN::Memory::LinearAllocator
suite<"NGIN::Memory::LinearAllocator"> linearAllocatorTests = [] {
    //------------------------------------------------------------------------------
    /// @brief Verify that constructing with a capacity leads to correct capacity.
    "ConstructWithCapacity"_test = [] {
        constexpr std::size_t capacity = 1024;
        NGIN::Memory::LinearAllocator allocator(capacity);

        expect(allocator.GetCapacity() == capacity);
        expect(allocator.GetUsedSize() == 0_ul);
    };

    //------------------------------------------------------------------------------
    /// @brief Verify that constructing from an existing block borrows that block without allocating.
    "ConstructFromMemoryBlock"_test = [] {
        constexpr std::size_t blockSize = 256;
        auto block                      = CreateStackBlock<blockSize>();
        NGIN::Memory::LinearAllocator allocator(block);

        // Should not allocate new memory, capacity is the borrowed block size.
        expect(allocator.GetCapacity() == blockSize);
        expect(allocator.GetUsedSize() == 0_ul);
    };

    //------------------------------------------------------------------------------
    /// @brief Verify move-construction transfers the state from one allocator to another.
    "MoveConstructor"_test = [] {
        constexpr std::size_t capacity = 128;
        NGIN::Memory::LinearAllocator source(capacity);

        // Perform some allocations
        constexpr std::size_t smallAllocSize = 32;
        auto block1                          = source.Allocate(smallAllocSize);
        expect(block1.ptr != nullptr);
        expect(source.GetUsedSize() == smallAllocSize);

        // Move-construct
        NGIN::Memory::LinearAllocator moved(std::move(source));

        // The source should be "emptied".
        expect(source.GetCapacity() == 0_ul);
        expect(source.GetUsedSize() == 0_ul);

        // The moved allocator should have the original capacity and usage.
        expect(moved.GetCapacity() == capacity);
        expect(moved.GetUsedSize() == smallAllocSize);
        expect(moved.Owns(block1.ptr) == true);
    };

    //------------------------------------------------------------------------------
    /// @brief Verify move-assignment transfers the state from one allocator to another.
    "MoveAssignment"_test = [] {
        constexpr std::size_t capacitySrc = 64;
        NGIN::Memory::LinearAllocator src(capacitySrc);

        // Perform some allocations
        constexpr std::size_t smallAllocSize = 16;
        auto block1                          = src.Allocate(smallAllocSize);
        expect(block1.ptr != nullptr);
        expect(src.GetUsedSize() == smallAllocSize);

        constexpr std::size_t capacityDest = 32;
        NGIN::Memory::LinearAllocator dest(capacityDest);

        // Move-assign
        dest = std::move(src);

        // The source should be "emptied".
        expect(src.GetCapacity() == 0_ul);
        expect(src.GetUsedSize() == 0_ul);

        // The dest should have the original source capacity and usage.
        expect(dest.GetCapacity() == capacitySrc);
        expect(dest.GetUsedSize() == smallAllocSize);
        expect(dest.Owns(block1.ptr) == true);
    };

    //------------------------------------------------------------------------------
    /// @brief Verify that Allocate returns valid blocks until capacity is exceeded.
    "AllocateWithinCapacity"_test = [] {
        constexpr std::size_t capacity = 128;
        NGIN::Memory::LinearAllocator allocator(capacity);

        constexpr std::size_t chunk1Size = 64;
        constexpr std::size_t chunk2Size = 32;

        auto block1 = allocator.Allocate(chunk1Size);
        expect(block1.ptr != nullptr);
        expect(block1.size == chunk1Size);

        auto block2 = allocator.Allocate(chunk2Size);
        expect(block2.ptr != nullptr);
        expect(block2.size == chunk2Size);

        // Total used so far should be 64 + 32 = 96
        expect(allocator.GetUsedSize() == (chunk1Size + chunk2Size));
        // We still have capacity for 128 - 96 = 32
        expect(allocator.GetCapacity() - allocator.GetUsedSize() == 32_ul);
    };

    //------------------------------------------------------------------------------
    /// @brief Verify that allocation exceeding capacity returns a null block.
    "AllocateExceedingCapacity"_test = [] {
        constexpr std::size_t capacity = 64;
        NGIN::Memory::LinearAllocator allocator(capacity);

        // Entire capacity is 64; allocate 64 => should succeed
        auto block1 = allocator.Allocate(64);
        expect(block1.ptr != nullptr);
        expect(block1.size == 64_ul);

        // Another allocate => out of capacity => should fail
        auto block2 = allocator.Allocate(1);
        expect(block2.ptr == nullptr);
        expect(block2.size == 0_ul);
    };

    //------------------------------------------------------------------------------
    /// @brief Verify alignment parameter causes correct alignment in returned pointers.
    "AlignmentCheck"_test = [] {
        constexpr std::size_t capacity = 256;
        NGIN::Memory::LinearAllocator allocator(capacity);

        // Allocate with alignment=16
        constexpr std::size_t blockSize = 10;
        constexpr std::size_t alignment = 16;
        auto block                      = allocator.Allocate(blockSize, alignment);

        std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(block.ptr);
        expect((addr % alignment) == 0_ul);// address should be multiple of alignment
        expect(block.size == blockSize);
    };

    //------------------------------------------------------------------------------
    /// @brief Verify Reset reclaims all used space, so the next Allocate starts over.
    "ResetClearsAllocations"_test = [] {
        constexpr std::size_t capacity = 128;
        NGIN::Memory::LinearAllocator allocator(capacity);

        auto block1 = allocator.Allocate(64);
        expect(block1.ptr != nullptr);
        expect(allocator.GetUsedSize() == 64_ul);

        allocator.Reset();
        expect(allocator.GetUsedSize() == 0_ul);

        // Should now succeed again for 64
        auto block2 = allocator.Allocate(64);
        expect(block2.ptr != nullptr);
        expect(allocator.GetUsedSize() == 64_ul);
    };

    //------------------------------------------------------------------------------
    /// @brief Verify that Owns correctly reports whether a pointer is within the allocator's range.
    "OwnsPointer"_test = [] {
        constexpr std::size_t capacity = 128;
        NGIN::Memory::LinearAllocator allocator(capacity);
        auto block1 = allocator.Allocate(64);

        // This should be owned by the allocator
        expect(allocator.Owns(block1.ptr) == true);

        // Something outside the range should not be owned
        std::string dummy = "Hello";
        expect(allocator.Owns(dummy.data()) == false);
    };

    //------------------------------------------------------------------------------
    /// @brief Verify that used size tracks the total allocations without partial deallocations.
    "UsageAccumulation"_test = [] {
        constexpr std::size_t capacity = 64;
        NGIN::Memory::LinearAllocator allocator(capacity);

        // Allocate blocks of 25 bytes each => 4 allocations => total 100
        for (int i = 0; i < 4; i++)
        {
            auto block = allocator.Allocate(16);
            expect(block.ptr != nullptr);
        }
        // Should be fully used
        expect(allocator.GetUsedSize() == capacity);

        // Next allocation should fail
        auto blockFail = allocator.Allocate(1);
        expect(blockFail.ptr == nullptr);
        expect(blockFail.size == 0_ul);
    };

    //------------------------------------------------------------------------------
    /// @brief Verify that Deallocate does nothing and doesn't affect usage.
    "DeallocateNoEffect"_test = [] {
        constexpr std::size_t capacity = 64;
        NGIN::Memory::LinearAllocator allocator(capacity);

        auto block = allocator.Allocate(32);
        expect(block.ptr != nullptr);
        expect(allocator.GetUsedSize() == 32_ul);

        // Call Deallocate
        allocator.Deallocate(block.ptr);
        // Should not reset or decrease usage
        expect(allocator.GetUsedSize() == 32_ul);
    };
};
