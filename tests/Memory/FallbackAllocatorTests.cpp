/// @file FallbackAllocatorTests.cpp
/// @brief Tests for FallbackAllocator behavior using Catch2.

#include <NGIN/Memory/FallbackAllocator.hpp>
#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

struct DummySmallAllocator
{
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
        auto base    = reinterpret_cast<std::uintptr_t>(storage) + used;
        auto aligned = (base + (alignment - 1)) & ~(std::uintptr_t(alignment) - 1);
        auto padding = aligned - base;
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

    bool Owns(const void* pointer) const noexcept
    {
        auto bytes = reinterpret_cast<const std::byte*>(pointer);
        return bytes >= storage && bytes < storage + sizeof(storage);
    }
};

TEST_CASE("FallbackAllocator uses primary before secondary", "[Memory][FallbackAllocator]")
{
    DummySmallAllocator             primary;
    NGIN::Memory::SystemAllocator   secondary;
    NGIN::Memory::FallbackAllocator allocator {primary, secondary};

    std::vector<void*> primaryBlocks;
    for (int i = 0; i < 32; ++i)
    {
        if (void* block = allocator.Allocate(8, alignof(std::max_align_t)))
        {
            primaryBlocks.push_back(block);
        }
    }

    void* large = allocator.Allocate(1024, alignof(std::max_align_t));
    REQUIRE(large != nullptr);

    for (auto* block: primaryBlocks)
    {
        allocator.Deallocate(block, 8, alignof(std::max_align_t));
    }
    allocator.Deallocate(large, 1024, alignof(std::max_align_t));
}

TEST_CASE("FallbackAllocator routes deallocation correctly", "[Memory][FallbackAllocator]")
{
    using Arena = NGIN::Memory::LinearAllocator<>;
    Arena                           primary {128};
    NGIN::Memory::SystemAllocator   secondary;
    NGIN::Memory::FallbackAllocator allocator {std::move(primary), secondary};

    void* small = allocator.Allocate(64, 8);
    void* large = allocator.Allocate(256, 8);

    REQUIRE(small != nullptr);
    REQUIRE(large != nullptr);

    allocator.Deallocate(small, 64, 8);
    allocator.Deallocate(large, 256, 8);
}
