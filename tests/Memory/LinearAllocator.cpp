/// @file LinearAllocatorTest.cpp
/// @brief Tests for NGIN::Memory::LinearAllocator using Catch2
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
/// The tests rely on Catch2 for a lightweight, header-only unit test framework.

#include <catch2/catch_test_macros.hpp>

#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

namespace nm = NGIN::Memory;

TEST_CASE("NGIN::Memory::LinearAllocator", "[Memory][LinearAllocator]")
{
    // -------------------------------------------------------------------------
    // Construction + basic properties
    // -------------------------------------------------------------------------
    SECTION("ConstructWithCapacityAndDefaults")
    {
        constexpr std::size_t kCapacity = 1024;
        nm::LinearAllocator<> arena {kCapacity};// upstream = SystemAllocator by default

        CHECK(arena.MaxSize() == kCapacity);
        CHECK(arena.Used() == 0UL);
        CHECK(arena.Remaining() == kCapacity);
    }

    SECTION("ConstructZeroCapacity_YieldsEmptyArena")
    {
        // SystemAllocator::Allocate(0, align) returns nullptr, so arena becomes empty
        nm::LinearAllocator<> arena {0};

        CHECK(arena.MaxSize() == 0UL);
        CHECK(arena.Used() == 0UL);
        CHECK(arena.Remaining() == 0UL);

        void* p = arena.Allocate(1, 8);
        CHECK(p == nullptr);
    };

    // -------------------------------------------------------------------------
    // Allocate / AllocateEx
    // -------------------------------------------------------------------------
    SECTION("Allocate_BasicAndRemainingTracking")
    {
        constexpr std::size_t kCapacity = 256;
        nm::LinearAllocator<> arena {kCapacity};

        void* a = arena.Allocate(64, 8);
        CHECK(a != nullptr);
        CHECK(arena.Used() == 64UL);
        CHECK(arena.Remaining() == (kCapacity - 64));

        void* b = arena.Allocate(32, 8);
        CHECK(b != nullptr);
        CHECK(arena.Used() == 96UL);
        CHECK(arena.Remaining() == (kCapacity - 96));

        // Exhaust the remainder exactly
        void* c = arena.Allocate(kCapacity - 96, 8);
        CHECK(c != nullptr);
        CHECK(arena.Used() == kCapacity);
        CHECK(arena.Remaining() == 0UL);

        // One more should fail
        void* d = arena.Allocate(1, 8);
        CHECK(d == nullptr);
    }

    SECTION("AllocateEx_ReturnsMemoryBlockWithMetadata")
    {
        constexpr std::size_t kCapacity = 128;
        nm::LinearAllocator<> arena {kCapacity};

        constexpr std::size_t kRequest = 24;
        constexpr std::size_t kAlign   = 32;

        nm::MemoryBlock blk = arena.AllocateEx(kRequest, kAlign);
        CHECK(bool(blk));
        CHECK(blk.SizeInBytes == kRequest);
        CHECK(blk.AlignmentInBytes >= kAlign);// may be normalized up

        auto addr = reinterpret_cast<std::uintptr_t>(blk.ptr);
        CHECK((addr % blk.AlignmentInBytes) == 0UL);
        CHECK(arena.Used() == kRequest);
    };

    // -------------------------------------------------------------------------
    // Alignment behavior
    // -------------------------------------------------------------------------
    SECTION("Alignment_NormalizationToPowerOfTwo_AndAtLeastMaxAlignT")
    {
        constexpr std::size_t kCapacity = 256;
        nm::LinearAllocator<> arena {kCapacity};

        // Request an odd, non-power-of-two alignment; allocator will normalize it.
        constexpr std::size_t requested = 18;
        void*                 p         = arena.Allocate(8, requested);
        CHECK(p != nullptr);

        // The actual alignment is at least alignof(max_align_t) and power-of-two.
        auto                  addr     = reinterpret_cast<std::uintptr_t>(p);
        constexpr std::size_t minAlign = alignof(std::max_align_t);
        CHECK((addr % minAlign) == 0UL);
    }

    SECTION("Alignment_ExactPowerOfTwoIsRespected")
    {
        constexpr std::size_t kCapacity = 256;
        nm::LinearAllocator<> arena {kCapacity};

        for (std::size_t align: {std::size_t(8), std::size_t(16), std::size_t(32), std::size_t(64)})
        {
            void* p = arena.Allocate(8, align);
            CHECK(p != nullptr);
            auto addr = reinterpret_cast<std::uintptr_t>(p);
            CHECK((addr % align) == 0UL);
        }
    };

    // -------------------------------------------------------------------------
    // Reset / Mark / Rollback
    // -------------------------------------------------------------------------
    SECTION("Reset_ReclaimsAll")
    {
        constexpr std::size_t kCapacity = 128;
        nm::LinearAllocator<> arena {kCapacity};

        void* p1 = arena.Allocate(40, 8);
        CHECK(p1 != nullptr);
        CHECK(arena.Used() == 40UL);

        arena.Reset();
        CHECK(arena.Used() == 0UL);
        CHECK(arena.Remaining() == kCapacity);

        // Allocate again after reset
        void* p2 = arena.Allocate(64, 16);
        CHECK(p2 != nullptr);
        CHECK(arena.Used() == 64UL);
        CHECK(arena.Remaining() == (kCapacity - 64));
    }

    SECTION("MarkAndRollback_MoveBumpPointerBack")
    {
        constexpr std::size_t kCapacity = 256;
        nm::LinearAllocator<> arena {kCapacity};

        void* a = arena.Allocate(32, 8);
        CHECK(a != nullptr);
        auto mark = arena.Mark();

        void* b = arena.Allocate(64, 16);
        CHECK(b != nullptr);
        CHECK(arena.Used() == 96UL);

        arena.Rollback(mark);
        // After rollback, Used() should be back to 32
        CHECK(arena.Used() == 32UL);

        // Reallocate the same 64 bytes again and it should still fit
        void* c = arena.Allocate(64, 16);
        CHECK(c != nullptr);
        CHECK(arena.Used() == 96UL);
    };

    // -------------------------------------------------------------------------
    // Move semantics
    // -------------------------------------------------------------------------
    SECTION("MoveConstructor_TransfersSlabOwnership")
    {
        constexpr std::size_t kCapacity = 128;
        nm::LinearAllocator<> src {kCapacity};

        void* p = src.Allocate(32, 8);
        CHECK(p != nullptr);
        CHECK(src.Used() == 32UL);

        nm::LinearAllocator<> dst {std::move(src)};

        // Source becomes empty
        CHECK(src.MaxSize() == 0UL);
        CHECK(src.Used() == 0UL);

        // Destination has prior state
        CHECK(dst.MaxSize() == kCapacity);
        CHECK(dst.Used() == 32UL);
        CHECK(dst.Owns(p));
    }

    SECTION("MoveAssignment_TransfersSlabOwnership")
    {
        constexpr std::size_t kSrcCap = 96;
        nm::LinearAllocator<> src {kSrcCap};
        void*                 p = src.Allocate(48, 8);
        CHECK(p != nullptr);
        CHECK(src.Used() == 48UL);

        constexpr std::size_t kDstCap = 64;
        nm::LinearAllocator<> dst {kDstCap};

        dst = std::move(src);

        CHECK(src.MaxSize() == 0UL);
        CHECK(src.Used() == 0UL);

        CHECK(dst.MaxSize() == kSrcCap);
        CHECK(dst.Used() == 48UL);
        CHECK(dst.Owns(p));
    };

    // -------------------------------------------------------------------------
    // Owns and Deallocate semantics
    // -------------------------------------------------------------------------
    SECTION("Owns_ReturnsTrueForPointersInsideSlab")
    {
        constexpr std::size_t kCapacity = 128;
        nm::LinearAllocator<> arena {kCapacity};

        void* p = arena.Allocate(16, 8);
        CHECK(p != nullptr);
        CHECK(arena.Owns(p));

        std::string external = "not in arena";
        CHECK_FALSE(arena.Owns(external.data()));
    }

    SECTION("Deallocate_IsNoOp")
    {
        constexpr std::size_t kCapacity = 128;
        nm::LinearAllocator<> arena {kCapacity};

        void* p = arena.Allocate(32, 16);
        CHECK(p != nullptr);
        auto usedBefore = arena.Used();

        // Deallocate does nothing (API still accepts size and alignment)
        arena.Deallocate(p, 32, 16);

        CHECK(arena.Used() == usedBefore);
        CHECK(arena.Remaining() == (kCapacity - usedBefore));
    };

    // -------------------------------------------------------------------------
    // AllocateEx + alignment normalization checks combined
    // -------------------------------------------------------------------------
    SECTION("AllocateEx_NormalizesAlignmentAndReportsIt")
    {
        constexpr std::size_t kCapacity = 512;
        nm::LinearAllocator<> arena {kCapacity};

        // Request a non power-of-two alignment (e.g., 18) -> will normalize
        nm::MemoryBlock blk = arena.AllocateEx(40, 18);
        CHECK(bool(blk));

        // Reported alignment should be power-of-two and at least alignof(max_align_t)
        const std::size_t reported = blk.AlignmentInBytes;
        auto              is_pow2  = (reported & (reported - 1)) == 0;
        CHECK(is_pow2);
        CHECK(reported >= alignof(std::max_align_t));

        auto addr = reinterpret_cast<std::uintptr_t>(blk.ptr);
        CHECK((addr % reported) == 0UL);
    }
}
