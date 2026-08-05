/// @file PolyAllocatorRef.cpp
/// @brief Tests for the non-owning type-erased allocator reference.

#include <NGIN/Memory/PolyAllocatorRef.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct CountingAllocator
    {
        NGIN::Memory::SystemAllocator system {};
        std::size_t                   allocations {0};
        std::size_t                   deallocations {0};

        [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment) noexcept
        {
            ++allocations;
            return system.Allocate(size, alignment);
        }

        void Deallocate(void* memory, std::size_t size, std::size_t alignment) noexcept
        {
            ++deallocations;
            system.Deallocate(memory, size, alignment);
        }
    };
}// namespace

TEST_CASE("PolyAllocatorRef direct copies preserve the referenced allocator",
          "[Memory][PolyAllocatorRef]")
{
    CountingAllocator              allocator;
    NGIN::Memory::PolyAllocatorRef original {allocator};
    NGIN::Memory::PolyAllocatorRef copy {original};

    original = {};

    void* memory = copy.Allocate(64, alignof(std::max_align_t));
    REQUIRE(memory != nullptr);
    CHECK(allocator.allocations == 1);

    copy.Deallocate(memory, 64, alignof(std::max_align_t));
    CHECK(allocator.deallocations == 1);
}

TEST_CASE("empty PolyAllocatorRef rejects allocations", "[Memory][PolyAllocatorRef]")
{
    NGIN::Memory::PolyAllocatorRef allocator;

    CHECK_FALSE(allocator.HasValue());
    CHECK(allocator.Allocate(64, alignof(std::max_align_t)) == nullptr);
}
