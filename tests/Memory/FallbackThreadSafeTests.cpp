#include <NGIN/Memory/FallbackAllocator.hpp>
#include <NGIN/Memory/ThreadSafeAllocator.hpp>
#include <NGIN/Memory/BumpArena.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Memory/TrackingAllocator.hpp>
#include <NGIN/Memory/AllocationHelpers.hpp>
#include <boost/ut.hpp>
#include <thread>
#include <vector>

using namespace boost::ut;

struct DummySmallAllocator
{
    std::byte storage[256] {};
    std::size_t used {0};
    void* Allocate(std::size_t n, std::size_t a) noexcept
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
    void Deallocate(void*, std::size_t, std::size_t) noexcept {}
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

suite<"NGIN::Memory::FallbackAndThreadSafe"> fallbackAndThreadSafe = [] {
    "FallbackAllocator basic"_test = [] {
        DummySmallAllocator small;
        NGIN::Memory::SystemAllocator sys;
        NGIN::Memory::FallbackAllocator fb {small, sys};
        // First allocate many small blocks until small exhausted
        std::vector<void*> primaryPtrs;
        for (int i = 0; i < 32; ++i)
        {
            if (void* p = fb.Allocate(8, alignof(std::max_align_t)))
                primaryPtrs.push_back(p);
        }
        // Large allocation should fall back to system
        void* large = fb.Allocate(1024, alignof(std::max_align_t));
        expect(large != nullptr);
        // Deallocate (ownership dispatch)
        for (auto p: primaryPtrs)
            fb.Deallocate(p, 8, alignof(std::max_align_t));
        fb.Deallocate(large, 1024, alignof(std::max_align_t));
    };

    "ThreadSafeConcurrency"_test = [] {
        using Arena = NGIN::Memory::BumpArena<>;
        Arena arena {8 * 1024};
        NGIN::Memory::ThreadSafeAllocator<Arena> ts {std::move(arena)};
        constexpr int threads = 4;
        constexpr int iters   = 500;
        std::vector<std::thread> ths;
        ths.reserve(threads);
        for (int t = 0; t < threads; ++t)
        {
            ths.emplace_back([&] {
                for (int i = 0; i < iters; ++i)
                {
                    void* p = ts.Allocate(16, alignof(std::max_align_t));
                    (void) p;
                }
            });
        }
        for (auto& th: ths)
            th.join();
        // Remaining should be <= capacity
        expect(ts.InnerAllocator().Used() <= ts.InnerAllocator().MaxSize());
    };

    "TrackingDecorator"_test = [] {
        NGIN::Memory::Tracking<NGIN::Memory::SystemAllocator> track {NGIN::Memory::SystemAllocator {}};
        void* p1 = track.Allocate(64, alignof(std::max_align_t));
        void* p2 = track.Allocate(32, alignof(std::max_align_t));
        expect(track.GetStats().currentBytes == 96_u);
        track.Deallocate(p1, 64, alignof(std::max_align_t));
        expect(track.GetStats().currentBytes == 32_u);
        track.Deallocate(p2, 32, alignof(std::max_align_t));
        expect(track.GetStats().currentBytes == 0_u);
    };
};
