/// @file FallbackAllocatorTests.cpp
/// @brief Tests for FallbackAllocator behavior using Catch2.

#include <NGIN/Memory/FallbackAllocator.hpp>
#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

struct DummySmallAllocator
{
    static constexpr bool HasPreciseOwnership = true;

    std::byte   storage[256] {};
    std::size_t used {0};

    void* Allocate(std::size_t size, std::size_t alignment) noexcept
    {
        if (size == 0)
        {
            return nullptr;
        }
        if (alignment == 0)
        {
            alignment = 1;
        }
        const std::uintptr_t base    = reinterpret_cast<std::uintptr_t>(storage) + used;
        const std::uintptr_t aligned = (base + (alignment - 1)) & ~(std::uintptr_t(alignment) - 1);
        const std::uintptr_t padding = aligned - base;
        if (padding + size > (sizeof(storage) - used))
        {
            return nullptr;
        }
        used += padding + size;
        return reinterpret_cast<void*>(aligned);
    }

    void        Deallocate(void*, std::size_t, std::size_t) noexcept {}
    std::size_t MaxSize() const noexcept { return sizeof(storage); }
    std::size_t Remaining() const noexcept { return sizeof(storage) - used; }

    NGIN::Memory::Ownership OwnershipOf(const void* pointer) const noexcept
    {
        const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(pointer);
        const std::uintptr_t begin   = reinterpret_cast<std::uintptr_t>(storage);
        const std::uintptr_t end     = begin + sizeof(storage);
        return address >= begin && address < end ? NGIN::Memory::Ownership::Owns
                                                 : NGIN::Memory::Ownership::DoesNotOwn;
    }
};

TEST_CASE("FallbackAllocator uses primary before secondary", "[Memory][FallbackAllocator]")
{
    DummySmallAllocator             primary;
    DummySmallAllocator             secondary;
    NGIN::Memory::FallbackAllocator allocator {primary, secondary};

    void* primaryBlock   = allocator.Allocate(192, alignof(std::max_align_t));
    void* secondaryBlock = allocator.Allocate(192, alignof(std::max_align_t));
    REQUIRE(primaryBlock != nullptr);
    REQUIRE(secondaryBlock != nullptr);

    allocator.Deallocate(primaryBlock, 192, alignof(std::max_align_t));
    allocator.Deallocate(secondaryBlock, 192, alignof(std::max_align_t));
}

TEST_CASE("FallbackAllocator routes deallocation correctly", "[Memory][FallbackAllocator]")
{
    using Arena = NGIN::Memory::LinearAllocator<>;
    Arena                                 primary {128};
    NGIN::Memory::SystemAllocator         secondary;
    NGIN::Memory::TaggedFallbackAllocator allocator {std::move(primary), secondary};

    void* small = allocator.Allocate(64, 8);
    void* large = allocator.Allocate(256, 8);

    REQUIRE(small != nullptr);
    REQUIRE(large != nullptr);

    allocator.Deallocate(small, 64, 8);
    allocator.Deallocate(large, 256, 8);
}
