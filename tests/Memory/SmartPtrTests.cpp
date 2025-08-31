/// @file SmartPtrTests.cpp
/// @brief Tests for Scoped, Shared, and Ticket smart pointers with allocator support.

#include <boost/ut.hpp>

#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Memory/TrackingAllocator.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Memory/AllocatorRef.hpp>

using namespace boost::ut;

namespace
{
    struct Probe
    {
        static inline int constructed = 0;
        static inline int destructed  = 0;

        int value {0};
        explicit Probe(int v) : value(v) { ++constructed; }
        Probe(const Probe& p) : value(p.value) { ++constructed; }
        Probe(Probe&& p) noexcept : value(p.value) { ++constructed; }
        ~Probe() { ++destructed; }
    };
}// namespace

suite<"NGIN::Memory::SmartPointers"> smartPtrSuite = [] {
    "ScopedBasic"_test = [] {
        using Tracked      = NGIN::Memory::Tracking<NGIN::Memory::SystemAllocator>;
        Probe::constructed = Probe::destructed = 0;
        {
            Tracked alloc {NGIN::Memory::SystemAllocator {}};
            auto    scoped = NGIN::Memory::MakeScoped<Probe>(alloc, 42);
            expect(!!scoped);
            expect(scoped->value == 42_i);
            expect(Probe::constructed == 1_i);
            // Query stats from the allocator stored inside the Scoped instance
            expect(scoped.Allocator().GetStats().currentCount == 1_u);
            expect(scoped.Allocator().GetStats().currentBytes >= sizeof(Probe)) << "size counted";
        }
        // Scoped destroyed
        expect(Probe::destructed == 1_i);
    };

    "ScopedMoveAndRelease"_test = [] {
        using Tracked      = NGIN::Memory::Tracking<NGIN::Memory::SystemAllocator>;
        Probe::constructed = Probe::destructed = 0;

        Tracked alloc {NGIN::Memory::SystemAllocator {}};
        auto    s1 = NGIN::Memory::MakeScoped<Probe>(alloc, 5);
        expect(!!s1);

        // Move should transfer ownership and keep allocator alive
        auto s2 = std::move(s1);
        expect(!s1);
        expect(!!s2);
        expect(s2->value == 5_i);

        // Release returns the raw pointer; we must clean it up manually
        Probe* raw = s2.Release();
        expect(!s2);
        // Destruct + deallocate via the same tracking allocator
        NGIN::Memory::DeallocateObject<Tracked, Probe>(alloc, raw);

        expect(Probe::constructed == 1_i);
        expect(Probe::destructed == 1_i);
    };

    "SharedAndTicketLifecycle"_test = [] {
        using Tracked      = NGIN::Memory::Tracking<NGIN::Memory::SystemAllocator>;
        Probe::constructed = Probe::destructed = 0;
        Tracked     tracking {NGIN::Memory::SystemAllocator {}};
        auto        allocRef = NGIN::Memory::AllocatorRef(tracking);
        std::size_t baseline = tracking.GetStats().currentBytes;

        {
            auto sp = NGIN::Memory::MakeShared<Probe>(allocRef, 7);
            expect(!!sp);
            expect(sp.UseCount() == 1_u);
            expect(sp->value == 7_i);
            // One allocation for control+object
            expect(tracking.GetStats().currentCount == 1_u);

            // Copy increases strong count
            {
                auto sp2 = sp;
                expect(sp.UseCount() == 2_u);

                // Make a weak ticket
                auto wk = NGIN::Memory::MakeTicket(sp);
                expect(!wk.Expired());
                auto locked = wk.Lock();
                expect(!!locked);
                expect(locked.UseCount() == 3_u);
            }

            // sp2 and locked destroyed, expect one owner left
            expect(sp.UseCount() == 1_u);

            // Keep a weak ticket alive after last shared
            auto wk2 = NGIN::Memory::MakeTicket(sp);
            expect(!wk2.Expired());

            // After resetting the last shared, object gets destroyed but block remains due to weak
            sp.Reset();
            expect(sp.UseCount() == 0_u);
            expect(wk2.Expired());

            // Memory for control+object should still be accounted until weak drops
            expect(tracking.GetStats().currentBytes >= baseline);

            // Drop weak; block should be released
            wk2.Reset();
        }

        expect(Probe::constructed == 1_i);
        expect(Probe::destructed == 1_i);
        // All memory released
        expect(tracking.GetStats().currentBytes == baseline);
        expect(tracking.GetStats().currentCount == 0_u);
    };

    "WeakLockEdgeCases"_test = [] {
        using Tracked      = NGIN::Memory::Tracking<NGIN::Memory::SystemAllocator>;
        Probe::constructed = Probe::destructed = 0;
        Tracked tracking {NGIN::Memory::SystemAllocator {}};
        auto    ref = NGIN::Memory::AllocatorRef(tracking);

        // Empty Ticket
        NGIN::Memory::Ticket<Probe, decltype(ref)> emptyTicket;
        expect(emptyTicket.Expired());
        auto l0 = emptyTicket.Lock();
        expect(!l0);

        // Create shared then immediately reset
        auto sp = NGIN::Memory::MakeShared<Probe>(ref, 1);
        auto wk = NGIN::Memory::MakeTicket(sp);
        expect(!wk.Expired());
        sp.Reset();
        expect(wk.Expired());
        auto l1 = wk.Lock();
        expect(!l1);
        wk.Reset();

        expect(Probe::constructed == 1_i);
        expect(Probe::destructed == 1_i);
        expect(tracking.GetStats().currentCount == 0_u);
    };
};
