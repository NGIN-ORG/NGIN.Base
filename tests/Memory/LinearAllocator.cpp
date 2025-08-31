/// @file LinearAllocatorTest.cpp
/// @brief Tests for NGIN::Memory::LinearAllocator using boost::ut
///
/// @details
/// This file exercises the modern LinearAllocator API:
///   - Construction with capacity and upstream
///   - Allocate / AllocateEx
///   - Alignment normalization and guarantees
///   - Exhaustion behavior
///   - Usage/remaining/capacity tracking
///   - Reset / Mark / Rollback
///   - Move construction and assignment
///   - Owns checks
///   - Deallocate no-op semantics
///
/// The tests use boost::ut for a lightweight, header-only unit test framework.

#include <boost/ut.hpp>

#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

using namespace boost::ut;

namespace nm = NGIN::Memory;

suite<"NGIN::Memory::LinearAllocator"> linear_allocator_tests = [] {
    // -------------------------------------------------------------------------
    // Construction + basic properties
    // -------------------------------------------------------------------------
    "ConstructWithCapacityAndDefaults"_test = [] {
        constexpr std::size_t kCapacity = 1024;
        nm::LinearAllocator<> arena {kCapacity};// upstream = SystemAllocator by default

        expect(arena.MaxSize() == kCapacity);
        expect(arena.Used() == 0_ul);
        expect(arena.Remaining() == kCapacity);
    };

    "ConstructZeroCapacity_YieldsEmptyArena"_test = [] {
        // SystemAllocator::Allocate(0, align) returns nullptr, so arena becomes empty
        nm::LinearAllocator<> arena {0};

        expect(arena.MaxSize() == 0_ul);
        expect(arena.Used() == 0_ul);
        expect(arena.Remaining() == 0_ul);

        void* p = arena.Allocate(1, 8);
        expect(p == nullptr);
    };

    // -------------------------------------------------------------------------
    // Allocate / AllocateEx
    // -------------------------------------------------------------------------
    "Allocate_BasicAndRemainingTracking"_test = [] {
        constexpr std::size_t kCapacity = 256;
        nm::LinearAllocator<> arena {kCapacity};

        void* a = arena.Allocate(64, 8);
        expect(a != nullptr);
        expect(arena.Used() == 64_ul);
        expect(arena.Remaining() == (kCapacity - 64));

        void* b = arena.Allocate(32, 8);
        expect(b != nullptr);
        expect(arena.Used() == 96_ul);
        expect(arena.Remaining() == (kCapacity - 96));

        // Exhaust the remainder exactly
        void* c = arena.Allocate(kCapacity - 96, 8);
        expect(c != nullptr);
        expect(arena.Used() == kCapacity);
        expect(arena.Remaining() == 0_ul);

        // One more should fail
        void* d = arena.Allocate(1, 8);
        expect(d == nullptr);
    };

    "AllocateEx_ReturnsMemoryBlockWithMetadata"_test = [] {
        constexpr std::size_t kCapacity = 128;
        nm::LinearAllocator<> arena {kCapacity};

        constexpr std::size_t kRequest = 24;
        constexpr std::size_t kAlign   = 32;

        nm::MemoryBlock blk = arena.AllocateEx(kRequest, kAlign);
        expect(bool(blk) == true);
        expect(blk.SizeInBytes == kRequest);
        expect(blk.AlignmentInBytes >= kAlign);// may be normalized up

        auto addr = reinterpret_cast<std::uintptr_t>(blk.ptr);
        expect((addr % blk.AlignmentInBytes) == 0_ul);
        expect(arena.Used() == kRequest);
    };

    // -------------------------------------------------------------------------
    // Alignment behavior
    // -------------------------------------------------------------------------
    "Alignment_NormalizationToPowerOfTwo_AndAtLeastMaxAlignT"_test = [] {
        constexpr std::size_t kCapacity = 256;
        nm::LinearAllocator<> arena {kCapacity};

        // Request an odd, non-power-of-two alignment; allocator will normalize it.
        constexpr std::size_t requested = 18;
        void*                 p         = arena.Allocate(8, requested);
        expect(p != nullptr);

        // The actual alignment is at least alignof(max_align_t) and power-of-two.
        auto                  addr     = reinterpret_cast<std::uintptr_t>(p);
        constexpr std::size_t minAlign = alignof(std::max_align_t);
        expect((addr % minAlign) == 0_ul);
    };

    "Alignment_ExactPowerOfTwoIsRespected"_test = [] {
        constexpr std::size_t kCapacity = 256;
        nm::LinearAllocator<> arena {kCapacity};

        for (std::size_t align: {std::size_t(8), std::size_t(16), std::size_t(32), std::size_t(64)})
        {
            void* p = arena.Allocate(8, align);
            expect(p != nullptr);
            auto addr = reinterpret_cast<std::uintptr_t>(p);
            expect((addr % align) == 0_ul);
        }
    };

    // -------------------------------------------------------------------------
    // Reset / Mark / Rollback
    // -------------------------------------------------------------------------
    "Reset_ReclaimsAll"_test = [] {
        constexpr std::size_t kCapacity = 128;
        nm::LinearAllocator<> arena {kCapacity};

        void* p1 = arena.Allocate(40, 8);
        expect(p1 != nullptr);
        expect(arena.Used() == 40_ul);

        arena.Reset();
        expect(arena.Used() == 0_ul);
        expect(arena.Remaining() == kCapacity);

        // Allocate again after reset
        void* p2 = arena.Allocate(64, 16);
        expect(p2 != nullptr);
        expect(arena.Used() == 64_ul);
        expect(arena.Remaining() == (kCapacity - 64));
    };

    "MarkAndRollback_MoveBumpPointerBack"_test = [] {
        constexpr std::size_t kCapacity = 256;
        nm::LinearAllocator<> arena {kCapacity};

        void* a = arena.Allocate(32, 8);
        expect(a != nullptr);
        auto mark = arena.Mark();

        void* b = arena.Allocate(64, 16);
        expect(b != nullptr);
        expect(arena.Used() == 96_ul);

        arena.Rollback(mark);
        // After rollback, Used() should be back to 32
        expect(arena.Used() == 32_ul);

        // Reallocate the same 64 bytes again and it should still fit
        void* c = arena.Allocate(64, 16);
        expect(c != nullptr);
        expect(arena.Used() == 96_ul);
    };

    // -------------------------------------------------------------------------
    // Move semantics
    // -------------------------------------------------------------------------
    "MoveConstructor_TransfersSlabOwnership"_test = [] {
        constexpr std::size_t kCapacity = 128;
        nm::LinearAllocator<> src {kCapacity};

        void* p = src.Allocate(32, 8);
        expect(p != nullptr);
        expect(src.Used() == 32_ul);

        nm::LinearAllocator<> dst {std::move(src)};

        // Source becomes empty
        expect(src.MaxSize() == 0_ul);
        expect(src.Used() == 0_ul);

        // Destination has prior state
        expect(dst.MaxSize() == kCapacity);
        expect(dst.Used() == 32_ul);
        expect(dst.Owns(p) == true);
    };

    "MoveAssignment_TransfersSlabOwnership"_test = [] {
        constexpr std::size_t kSrcCap = 96;
        nm::LinearAllocator<> src {kSrcCap};
        void*                 p = src.Allocate(48, 8);
        expect(p != nullptr);
        expect(src.Used() == 48_ul);

        constexpr std::size_t kDstCap = 64;
        nm::LinearAllocator<> dst {kDstCap};

        dst = std::move(src);

        expect(src.MaxSize() == 0_ul);
        expect(src.Used() == 0_ul);

        expect(dst.MaxSize() == kSrcCap);
        expect(dst.Used() == 48_ul);
        expect(dst.Owns(p) == true);
    };

    // -------------------------------------------------------------------------
    // Owns and Deallocate semantics
    // -------------------------------------------------------------------------
    "Owns_ReturnsTrueForPointersInsideSlab"_test = [] {
        constexpr std::size_t kCapacity = 128;
        nm::LinearAllocator<> arena {kCapacity};

        void* p = arena.Allocate(16, 8);
        expect(p != nullptr);
        expect(arena.Owns(p) == true);

        std::string external = "not in arena";
        expect(arena.Owns(external.data()) == false);
    };

    "Deallocate_IsNoOp"_test = [] {
        constexpr std::size_t kCapacity = 128;
        nm::LinearAllocator<> arena {kCapacity};

        void* p = arena.Allocate(32, 16);
        expect(p != nullptr);
        auto usedBefore = arena.Used();

        // Deallocate does nothing (API still accepts size and alignment)
        arena.Deallocate(p, 32, 16);

        expect(arena.Used() == usedBefore);
        expect(arena.Remaining() == (kCapacity - usedBefore));
    };

    // -------------------------------------------------------------------------
    // AllocateEx + alignment normalization checks combined
    // -------------------------------------------------------------------------
    "AllocateEx_NormalizesAlignmentAndReportsIt"_test = [] {
        constexpr std::size_t kCapacity = 512;
        nm::LinearAllocator<> arena {kCapacity};

        // Request a non power-of-two alignment (e.g., 18) -> will normalize
        nm::MemoryBlock blk = arena.AllocateEx(40, 18);
        expect(bool(blk) == true);

        // Reported alignment should be power-of-two and at least alignof(max_align_t)
        const std::size_t reported = blk.AlignmentInBytes;
        auto              is_pow2  = (reported & (reported - 1)) == 0;
        expect(is_pow2 == true);
        expect(reported >= alignof(std::max_align_t));

        auto addr = reinterpret_cast<std::uintptr_t>(blk.ptr);
        expect((addr % reported) == 0_ul);
    };
};
