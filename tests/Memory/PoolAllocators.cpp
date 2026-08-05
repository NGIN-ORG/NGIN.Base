#include <catch2/catch_test_macros.hpp>

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Memory/AllocationHelpers.hpp>
#include <NGIN/Memory/AllocatorRef.hpp>
#include <NGIN/Memory/DebugAllocator.hpp>
#include <NGIN/Memory/FixedBlockAllocator.hpp>
#include <NGIN/Memory/ObjectPool.hpp>
#include <NGIN/Memory/SegregatedPoolAllocator.hpp>
#include <NGIN/Memory/ThreadSafeAllocator.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    struct ThrowingObject
    {
        explicit ThrowingObject(bool fail)
        {
            if (fail)
                throw std::runtime_error("construction failed");
        }
    };

    struct FailingAllocator
    {
        void* Allocate(std::size_t, std::size_t) noexcept { return nullptr; }
        void  Deallocate(void*, std::size_t, std::size_t) noexcept {}
    };
}// namespace

TEST_CASE("FixedBlockAllocator exhausts and reuses aligned blocks", "[Memory][PoolAllocator]")
{
    using Allocator = NGIN::Memory::FixedBlockAllocator<48, 3, 64>;
    STATIC_REQUIRE(NGIN::Memory::AllocatorConcept<Allocator>);

    Allocator            allocator;
    std::array<void*, 3> blocks {};
    for (auto& block: blocks)
    {
        block = allocator.Allocate(48, 64);
        REQUIRE(block != nullptr);
        CHECK(reinterpret_cast<std::uintptr_t>(block) % 64 == 0);
    }
    CHECK(allocator.AvailableBlocks() == 0);
    CHECK(allocator.Allocate(1, 1) == nullptr);
    CHECK(allocator.Allocate(49, 1) == nullptr);

    allocator.Deallocate(blocks[1], 48, 64);
    CHECK(allocator.AvailableBlocks() == 1);
    CHECK(allocator.Allocate(16, 16) == blocks[1]);

    int outside = 0;
    allocator.Deallocate(&outside, sizeof(outside), alignof(int));
    CHECK(allocator.InvalidDeallocations() == 1);
    allocator.Deallocate(blocks[0], 48, 64);
    allocator.Deallocate(blocks[0], 48, 64);
    CHECK(allocator.InvalidDeallocations() == 2);
}

TEST_CASE("Pool allocator construction handles upstream exhaustion", "[Memory][PoolAllocator]")
{
    NGIN::Memory::FixedBlockAllocator<32, 4, alignof(void*), FailingAllocator> fixed;
    CHECK(fixed.Capacity() == 4);
    CHECK(fixed.AvailableBlocks() == 0);
    CHECK(fixed.Allocate(16, alignof(void*)) == nullptr);

    NGIN::Memory::SegregatedPoolAllocator<4, FailingAllocator> segregated;
    CHECK(segregated.Remaining() == 0);
    CHECK(segregated.Allocate(16, alignof(void*)) == nullptr);
}

TEST_CASE("SegregatedPoolAllocator selects and composes size classes", "[Memory][PoolAllocator]")
{
    using Allocator = NGIN::Memory::SegregatedPoolAllocator<2>;
    STATIC_REQUIRE(NGIN::Memory::AllocatorConcept<Allocator>);

    Allocator allocator;
    auto      first  = allocator.AllocateEx(12, alignof(std::max_align_t));
    auto      second = allocator.AllocateEx(12, alignof(std::max_align_t));
    auto      spill  = allocator.AllocateEx(12, alignof(std::max_align_t));
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(spill);
    CHECK(first.SizeInBytes == 16);
    CHECK(spill.SizeInBytes == 32);
    CHECK(allocator.Allocate(513, alignof(std::max_align_t)) == nullptr);
    CHECK(allocator.Allocate(8, 64) == nullptr);

    allocator.Deallocate(first.ptr, first.SizeInBytes, first.AlignmentInBytes);
    CHECK(allocator.Allocate(16, alignof(std::max_align_t)) == first.ptr);
}

TEST_CASE("Pool allocators compose with allocator-aware containers", "[Memory][PoolAllocator]")
{
    NGIN::Memory::FixedBlockAllocator<256, 2, alignof(std::max_align_t)> pool;
    NGIN::Memory::AllocatorRef                                           reference(pool);
    {
        NGIN::Containers::Vector<int, decltype(reference)> values(16, reference);
        for (int value = 0; value < 16; ++value)
            values.PushBack(value);
        CHECK(values.Size() == 16);
        CHECK(values[15] == 15);
    }
    CHECK(pool.AvailableBlocks() == 2);
}

TEST_CASE("DebugAllocator detects canary corruption and invalid frees", "[Memory][DebugAllocator]")
{
    using Inner = NGIN::Memory::FixedBlockAllocator<256, 4, 64>;
    NGIN::Memory::DebugAllocator<Inner> allocator(Inner {});
    STATIC_REQUIRE(NGIN::Memory::AllocatorConcept<decltype(allocator)>);

    auto* bytes = static_cast<std::byte*>(allocator.Allocate(32, 16));
    REQUIRE(bytes != nullptr);
    CHECK(bytes[0] == std::byte {0xCD});
    bytes[32] = std::byte {0};
    allocator.Deallocate(bytes, 32, 16);
    CHECK(allocator.GetStats().corruptedAllocations == 1);
    CHECK(allocator.GetStats().liveAllocations == 0);

    int outside = 0;
    allocator.Deallocate(&outside, sizeof(outside), alignof(int));
    CHECK(allocator.GetStats().invalidDeallocations == 1);
}

TEST_CASE("Reallocate preserves data and retains the original on failure", "[Memory][AllocationHelpers]")
{
    NGIN::Memory::FixedBlockAllocator<16, 2, alignof(std::max_align_t)> allocator;
    auto*                                                               original = static_cast<std::byte*>(allocator.Allocate(8, alignof(std::max_align_t)));
    REQUIRE(original != nullptr);
    for (std::size_t index = 0; index < 8; ++index)
        original[index] = static_cast<std::byte>(index);

    auto* grown = static_cast<std::byte*>(
            NGIN::Memory::Reallocate(allocator, original, 8, 16, alignof(std::max_align_t)));
    REQUIRE(grown != nullptr);
    for (std::size_t index = 0; index < 8; ++index)
        CHECK(grown[index] == static_cast<std::byte>(index));

    CHECK(NGIN::Memory::Reallocate(allocator, grown, 16, 32, alignof(std::max_align_t)) == nullptr);
    CHECK(allocator.Owns(grown));
    allocator.Deallocate(grown, 16, alignof(std::max_align_t));
}

TEST_CASE("ObjectPool rolls back failed construction", "[Memory][ObjectPool]")
{
    NGIN::Memory::ObjectPool<ThrowingObject, 1> pool;
    CHECK_THROWS_AS(pool.Create(true), std::runtime_error);
    CHECK(pool.Available() == 1);
    auto* object = pool.Create(false);
    REQUIRE(object != nullptr);
    CHECK(pool.Available() == 0);
    pool.Destroy(object);
    CHECK(pool.Available() == 1);
}

TEST_CASE("Segregated pools support explicit thread-safe composition", "[Memory][PoolAllocator]")
{
    using Pool = NGIN::Memory::SegregatedPoolAllocator<64>;
    NGIN::Memory::ThreadSafeAllocator<Pool> allocator;
    STATIC_REQUIRE(NGIN::Memory::AllocatorConcept<decltype(allocator)>);

    std::atomic<int>         completed {0};
    std::vector<std::thread> workers;
    for (int threadIndex = 0; threadIndex < 4; ++threadIndex)
    {
        workers.emplace_back([&]() {
            for (int iteration = 0; iteration < 500; ++iteration)
            {
                void* pointer = nullptr;
                while ((pointer = allocator.Allocate(48, alignof(std::max_align_t))) == nullptr)
                    std::this_thread::yield();
                allocator.Deallocate(pointer, 48, alignof(std::max_align_t));
            }
            completed.fetch_add(1, std::memory_order_relaxed);
        });
    }
    for (auto& worker: workers)
        worker.join();
    CHECK(completed.load(std::memory_order_relaxed) == 4);
}
