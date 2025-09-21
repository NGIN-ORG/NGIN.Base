#include <NGIN/Memory/AllocationHelpers.hpp>
#include <NGIN/Memory/FallbackAllocator.hpp>
#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Memory/ThreadSafeAllocator.hpp>
#include <NGIN/Memory/TrackingAllocator.hpp>
#include <catch2/catch_test_macros.hpp>
#include <thread>
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

    void Deallocate(void*, std::size_t, std::size_t) noexcept {}

    std::size_t MaxSize() const noexcept { return sizeof(storage); }
    std::size_t Remaining() const noexcept { return sizeof(storage) - used; }

    bool Owns(const void* pointer) const noexcept
    {
        auto bytes = reinterpret_cast<const std::byte*>(pointer);
        return bytes >= storage && bytes < storage + sizeof(storage);
    }
};

TEST_CASE("FallbackAllocator uses primary allocator until exhausted", "[Memory][FallbackAllocator]")
{
    DummySmallAllocator             primary;
    NGIN::Memory::SystemAllocator   system;
    NGIN::Memory::FallbackAllocator allocator {primary, system};

    std::vector<void*> primaryAllocations;
    for (int i = 0; i < 32; ++i)
    {
        if (void* block = allocator.Allocate(8, alignof(std::max_align_t)))
        {
            primaryAllocations.push_back(block);
        }
    }

    void* large = allocator.Allocate(1024, alignof(std::max_align_t));
    REQUIRE(large != nullptr);

    for (void* block: primaryAllocations)
    {
        allocator.Deallocate(block, 8, alignof(std::max_align_t));
    }
    allocator.Deallocate(large, 1024, alignof(std::max_align_t));
}

TEST_CASE("ThreadSafeAllocator supports concurrent allocations", "[Memory][ThreadSafeAllocator]")
{
    using Arena = NGIN::Memory::LinearAllocator<>;
    Arena                                    arena {8 * 1024};
    NGIN::Memory::ThreadSafeAllocator<Arena> allocator {std::move(arena)};

    constexpr int threadCount = 4;
    constexpr int iterations  = 500;

    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i)
    {
        workers.emplace_back([&] {
            for (int iteration = 0; iteration < iterations; ++iteration)
            {
                if (void* block = allocator.Allocate(16, alignof(std::max_align_t)))
                {
                    allocator.Deallocate(block, 16, alignof(std::max_align_t));
                }
            }
        });
    }

    for (auto& worker: workers)
    {
        worker.join();
    }

    CHECK(allocator.InnerAllocator().Used() <= allocator.InnerAllocator().MaxSize());
}

TEST_CASE("Tracking allocator reports usage", "[Memory][TrackingAllocator]")
{
    NGIN::Memory::Tracking<NGIN::Memory::SystemAllocator> tracking {NGIN::Memory::SystemAllocator {}};
    void*                                                 first  = tracking.Allocate(64, alignof(std::max_align_t));
    void*                                                 second = tracking.Allocate(32, alignof(std::max_align_t));

    CHECK(tracking.GetStats().currentBytes == 96U);

    tracking.Deallocate(first, 64, alignof(std::max_align_t));
    CHECK(tracking.GetStats().currentBytes == 32U);

    tracking.Deallocate(second, 32, alignof(std::max_align_t));
    CHECK(tracking.GetStats().currentBytes == 0U);
}
