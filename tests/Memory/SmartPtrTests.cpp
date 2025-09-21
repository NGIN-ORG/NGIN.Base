/// @file SmartPtrTests.cpp
/// @brief Tests for Scoped, Shared, and Ticket smart pointers with allocator support.

#include <NGIN/Memory/AllocatorRef.hpp>
#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Memory/TrackingAllocator.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    struct Probe
    {
        static inline int constructed = 0;
        static inline int destructed  = 0;

        int value {0};

        explicit Probe(int v)
            : value(v) { ++constructed; }
        Probe(const Probe& other)
            : value(other.value) { ++constructed; }
        Probe(Probe&& other) noexcept
            : value(other.value) { ++constructed; }
        ~Probe() { ++destructed; }
    };
}// namespace

TEST_CASE("Scoped pointers manage lifetime", "[Memory][SmartPointers]")
{
    using Tracked = NGIN::Memory::Tracking<NGIN::Memory::SystemAllocator>;

    Probe::constructed = 0;
    Probe::destructed  = 0;

    {
        Tracked allocator {NGIN::Memory::SystemAllocator {}};
        auto    scoped = NGIN::Memory::MakeScoped<Probe>(allocator, 42);
        REQUIRE(scoped);
        CHECK(scoped->value == 42);
        CHECK(Probe::constructed == 1);

        const auto stats = scoped.Allocator().GetStats();
        CHECK(stats.currentCount == 1U);
        CHECK(stats.currentBytes >= sizeof(Probe));
    }

    CHECK(Probe::destructed == 1);
}

TEST_CASE("Scoped pointers support move and release", "[Memory][SmartPointers]")
{
    using Tracked = NGIN::Memory::Tracking<NGIN::Memory::SystemAllocator>;

    Probe::constructed = 0;
    Probe::destructed  = 0;

    Tracked allocator {NGIN::Memory::SystemAllocator {}};
    auto    scoped = NGIN::Memory::MakeScoped<Probe>(allocator, 5);
    REQUIRE(scoped);

    auto moved = std::move(scoped);
    CHECK_FALSE(scoped);
    REQUIRE(moved);
    CHECK(moved->value == 5);

    Probe* raw = moved.Release();
    CHECK_FALSE(moved);
    NGIN::Memory::DeallocateObject<Tracked, Probe>(allocator, raw);

    CHECK(Probe::constructed == 1);
    CHECK(Probe::destructed == 1);
}

TEST_CASE("Shared and ticket pointers manage reference counts", "[Memory][SmartPointers]")
{
    using Tracked = NGIN::Memory::Tracking<NGIN::Memory::SystemAllocator>;

    Probe::constructed = 0;
    Probe::destructed  = 0;

    Tracked           tracking {NGIN::Memory::SystemAllocator {}};
    auto              allocatorRef = NGIN::Memory::AllocatorRef(tracking);
    const std::size_t baseline     = tracking.GetStats().currentBytes;

    {
        auto shared = NGIN::Memory::MakeShared<Probe>(allocatorRef, 7);
        REQUIRE(shared);
        CHECK(shared.UseCount() == 1U);
        CHECK(shared->value == 7);
        CHECK(tracking.GetStats().currentCount == 1U);

        {
            auto sharedCopy = shared;
            CHECK(shared.UseCount() == 2U);

            auto ticket = NGIN::Memory::MakeTicket(shared);
            CHECK_FALSE(ticket.Expired());
            auto locked = ticket.Lock();
            REQUIRE(locked);
            CHECK(locked.UseCount() == 3U);
        }

        CHECK(shared.UseCount() == 1U);

        auto ticket = NGIN::Memory::MakeTicket(shared);
        CHECK_FALSE(ticket.Expired());

        shared.Reset();
        CHECK(shared.UseCount() == 0U);
        CHECK(ticket.Expired());
        CHECK(tracking.GetStats().currentBytes >= baseline);

        ticket.Reset();
    }

    CHECK(Probe::constructed == 1);
    CHECK(Probe::destructed == 1);
    CHECK(tracking.GetStats().currentBytes == baseline);
    CHECK(tracking.GetStats().currentCount == 0U);
}

TEST_CASE("Tickets handle edge cases", "[Memory][SmartPointers]")
{
    using Tracked = NGIN::Memory::Tracking<NGIN::Memory::SystemAllocator>;

    Probe::constructed = 0;
    Probe::destructed  = 0;

    Tracked tracking {NGIN::Memory::SystemAllocator {}};
    auto    allocatorRef = NGIN::Memory::AllocatorRef(tracking);

    NGIN::Memory::Ticket<Probe, decltype(allocatorRef)> emptyTicket;
    CHECK(emptyTicket.Expired());
    CHECK_FALSE(emptyTicket.Lock());

    auto shared = NGIN::Memory::MakeShared<Probe>(allocatorRef, 1);
    auto ticket = NGIN::Memory::MakeTicket(shared);
    CHECK_FALSE(ticket.Expired());

    shared.Reset();
    CHECK(ticket.Expired());
    CHECK_FALSE(ticket.Lock());
    ticket.Reset();

    CHECK(Probe::constructed == 1);
    CHECK(Probe::destructed == 1);
    CHECK(tracking.GetStats().currentCount == 0U);
}
