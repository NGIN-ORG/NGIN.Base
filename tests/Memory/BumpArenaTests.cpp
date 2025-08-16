/// @file BumpArenaTests.cpp
/// @brief Tests for the new BumpArena and Tracking allocators.

#include <boost/ut.hpp>

#include <NGIN/Memory/BumpArena.hpp>
#include <NGIN/Memory/AllocationHelpers.hpp>

using namespace boost::ut;

namespace
{
    struct TestPod
    {
        int a {42};
    };
}// namespace

suite<"NGIN::Memory::BumpArena"> bumpArenaSuite = [] {
    "ConstructionAndBasicAllocate"_test = [] {
        NGIN::Memory::BumpArena arena {1024};
        expect(arena.MaxSize() == 1024_u);
        auto* p = arena.Allocate(16, alignof(std::max_align_t));
        expect(p != nullptr);
        expect(arena.Remaining() < 1024_u);
    };

    "MarkerRollback"_test = [] {
        NGIN::Memory::BumpArena arena {512};
        auto mark = arena.Mark();
        auto p1   = arena.Allocate(64, 8);
        auto p2   = arena.Allocate(64, 8);
        expect(p1 != nullptr && p2 != nullptr);
        auto usedBefore = arena.Used();
        arena.Rollback(mark);
        expect(arena.Used() == (unsigned) 0);
        expect(usedBefore > 0_u);
    };

    "AllocateObjectHelper"_test = [] {
        NGIN::Memory::BumpArena arena {128};
        auto* obj = NGIN::Memory::AllocateObject<TestPod>(arena);
        expect(obj != nullptr);
        expect(obj->a == 42_i);
        NGIN::Memory::DeallocateObject(arena, obj);
    };

    "AllocateArrayHelper"_test = [] {
        NGIN::Memory::BumpArena arena {256};
        int* arr = NGIN::Memory::AllocateArray<int>(arena, 10);
        expect(arr != nullptr);
        for (int i = 0; i < 10; ++i)
            arr[i] = i;
        for (int i = 0; i < 10; ++i)
            expect(arr[i] == i);
        NGIN::Memory::DeallocateArray(arena, arr);
    };

    // New / expanded tests -------------------------------------------------

    "ZeroSizeAllocation"_test = [] {
        NGIN::Memory::BumpArena arena {128};
        auto usedBefore = arena.Used();
        void* p         = arena.Allocate(0, 8);
        expect(p == nullptr);
        expect(arena.Used() == usedBefore);
    };

    "AlignmentGuarantee"_test = [] {
        NGIN::Memory::BumpArena arena {512};
        void* p = arena.Allocate(24, 64);
        expect(p != nullptr);
        expect((reinterpret_cast<std::uintptr_t>(p) % 64) == 0_u);

        p = arena.Allocate(32, 16);
        expect(p != nullptr);
        expect((reinterpret_cast<std::uintptr_t>(p) % 16) == 0_u);

        p = arena.Allocate(32, 32);
        expect(p != nullptr);
        expect((reinterpret_cast<std::uintptr_t>(p) % 32) == 0_u);

        p = arena.Allocate(32, 128);
        expect(p != nullptr);
        expect((reinterpret_cast<std::uintptr_t>(p) % 128) == 0_u);
    };

    "ExhaustionReturnsNull"_test = [] {
        NGIN::Memory::BumpArena arena {96};
        void* a = arena.Allocate(32, 8);
        void* b = arena.Allocate(32, 8);
        void* c = arena.Allocate(40, 8);// Should fail: only 32 left
        expect(a != nullptr && b != nullptr);
        expect(c == nullptr);
        expect(arena.Remaining() == 32_u);
    };

    "MarkRollbackPartial"_test = [] {
        NGIN::Memory::BumpArena arena {256};
        void* first  = arena.Allocate(64, 8);
        auto mark    = arena.Mark();
        void* second = arena.Allocate(32, 8);
        expect(first != nullptr && second != nullptr);
        auto usedMid = arena.Used();
        arena.Rollback(mark);
        expect(arena.Used() < usedMid);
        void* third = arena.Allocate(48, 8);
        expect(third != nullptr);
    };

    "MoveSemantics"_test = [] {
        NGIN::Memory::BumpArena original {256};
        void* p = original.Allocate(32, 8);
        expect(p != nullptr);
        std::size_t usedBeforeMove = original.Used();
        NGIN::Memory::BumpArena moved {std::move(original)};
        expect(original.MaxSize() == 0_u);
        expect(moved.Used() == usedBeforeMove);
        void* q = moved.Allocate(32, 8);
        expect(q != nullptr);
    };
};
