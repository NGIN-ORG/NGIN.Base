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

    void Deallocate(void*, std::size_t, std::size_t) noexcept {}

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

TEST_CASE("FallbackAllocator uses primary allocator until exhausted", "[Memory][FallbackAllocator]")
{
    DummySmallAllocator                   primary;
    NGIN::Memory::SystemAllocator         system;
    NGIN::Memory::TaggedFallbackAllocator allocator {primary, system};

    void* primaryBlock   = allocator.Allocate(192, alignof(std::max_align_t));
    void* secondaryBlock = allocator.Allocate(192, alignof(std::max_align_t));
    REQUIRE(primaryBlock != nullptr);
    REQUIRE(secondaryBlock != nullptr);

    allocator.Deallocate(primaryBlock, 192, alignof(std::max_align_t));
    allocator.Deallocate(secondaryBlock, 192, alignof(std::max_align_t));
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

    const bool withinCapacity = allocator.WithInner([](const Arena& inner) {
        return inner.Used() <= inner.MaxSize();
    });
    CHECK(withinCapacity);
}

TEST_CASE("Tracking allocator reports usage", "[Memory][TrackingAllocator]")
{
    NGIN::Memory::TrackingAllocator<NGIN::Memory::SystemAllocator> tracking {NGIN::Memory::SystemAllocator {}};
    void*                                                          first  = tracking.Allocate(64, alignof(std::max_align_t));
    void*                                                          second = tracking.Allocate(32, alignof(std::max_align_t));

    CHECK(tracking.GetStats().currentBytes == 96U);

    tracking.Deallocate(first, 64, alignof(std::max_align_t));
    CHECK(tracking.GetStats().currentBytes == 32U);

    tracking.Deallocate(second, 32, alignof(std::max_align_t));
    CHECK(tracking.GetStats().currentBytes == 0U);
}
