/// @file FallbackAllocatorTests.cpp
/// @brief Tests for FallbackAllocator behavior.

#include <boost/ut.hpp>
#include <vector>

#include <NGIN/Memory/FallbackAllocator.hpp>
#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>

using namespace boost::ut;

struct DummySmallAllocator
{
    std::byte   storage[256] {};
    std::size_t used {0};
    void*       Allocate(std::size_t n, std::size_t a) noexcept
    {
        if (!n)
            return nullptr;
        if (a == 0)
            a = 1;
        auto base    = reinterpret_cast<std::uintptr_t>(storage) + used;
        auto aligned = (base + (a - 1)) & ~(std::uintptr_t(a) - 1);
        auto padding = aligned - base;
        if (padding + n > (sizeof(storage) - used))
            return nullptr;
        used += padding + n;
        return reinterpret_cast<void*>(aligned);
    }
    void        Deallocate(void*, std::size_t, std::size_t) noexcept {}
    std::size_t MaxSize() const noexcept
    {
        return sizeof(storage);
    }
    std::size_t Remaining() const noexcept
    {
        return sizeof(storage) - used;
    }
    bool Owns(const void* p) const noexcept
    {
        auto b = reinterpret_cast<const std::byte*>(p);
        return b >= storage && b < storage + sizeof(storage);
    }
};

suite<"NGIN::Memory::FallbackAllocator"> fallbackAllocatorSuite = [] {
    "PrimaryThenSecondary"_test = [] {
        DummySmallAllocator             small;
        NGIN::Memory::SystemAllocator   sys;
        NGIN::Memory::FallbackAllocator fb {small, sys};
        // Exhaust small then allocate large in secondary.
        std::vector<void*> primaryPtrs;
        for (int i = 0; i < 32; ++i)
        {
            if (void* p = fb.Allocate(8, alignof(std::max_align_t)))
                primaryPtrs.push_back(p);
        }
        void* large = fb.Allocate(1024, alignof(std::max_align_t));
        expect(large != nullptr);
        for (auto p: primaryPtrs)
            fb.Deallocate(p, 8, alignof(std::max_align_t));
        fb.Deallocate(large, 1024, alignof(std::max_align_t));
    };

    "MixedOwnershipDeallocate"_test = [] {
        using Arena = NGIN::Memory::LinearAllocator<>;
        Arena                           primary {128};
        NGIN::Memory::SystemAllocator   sys;
        NGIN::Memory::FallbackAllocator fb {std::move(primary), sys};
        void*                           a = fb.Allocate(64, 8); // likely from primary
        void*                           b = fb.Allocate(256, 8);// must come from secondary
        expect(a != nullptr && b != nullptr);
        fb.Deallocate(a, 64, 8);
        fb.Deallocate(b, 256, 8);
    };
};
