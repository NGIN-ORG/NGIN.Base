/// @file SmartPtrTests.cpp
/// @brief Tests for Scoped, Shared, and Ticket smart pointers with allocator support.

#include "../Support/FailureInjection.hpp"
#include <NGIN/Memory/AllocatorRef.hpp>
#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Memory/TrackingAllocator.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>

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

    struct PolyBase
    {
        static inline int destructed = 0;
        virtual ~PolyBase() { ++destructed; }
        [[nodiscard]] virtual int Value() const noexcept = 0;
    };

    struct PolyDerived final : PolyBase
    {
        static inline int destructed = 0;

        explicit PolyDerived(const int valueIn) noexcept
            : value(valueIn)
        {
        }

        ~PolyDerived() override { ++destructed; }

        [[nodiscard]] int Value() const noexcept override
        {
            return value;
        }

        int value {0};
    };

    struct ThrowingProbe final
    {
        explicit ThrowingProbe(NGIN::Tests::FailureCountdown& failures)
        {
            failures.Hit();
        }
    };

    struct MoveOnlyAllocatorState final
    {
        std::size_t allocations {0};
        std::size_t deallocations {0};
        std::size_t owningAllocatorDestructions {0};
    };

    class MoveOnlyAllocator final
    {
    public:
        explicit MoveOnlyAllocator(std::shared_ptr<MoveOnlyAllocatorState> state) noexcept
            : m_state(std::move(state))
        {
        }

        MoveOnlyAllocator(const MoveOnlyAllocator&)            = delete;
        MoveOnlyAllocator& operator=(const MoveOnlyAllocator&) = delete;

        MoveOnlyAllocator(MoveOnlyAllocator&& other) noexcept
            : m_state(std::move(other.m_state))
        {
        }

        MoveOnlyAllocator& operator=(MoveOnlyAllocator&& other) noexcept
        {
            if (this != &other)
                m_state = std::move(other.m_state);
            return *this;
        }

        ~MoveOnlyAllocator()
        {
            if (m_state)
                ++m_state->owningAllocatorDestructions;
        }

        [[nodiscard]] void* Allocate(const std::size_t size, const std::size_t alignment) noexcept
        {
            void* const pointer = m_system.Allocate(size, alignment);
            if (pointer)
                ++m_state->allocations;
            return pointer;
        }

        void Deallocate(void* pointer, const std::size_t size, const std::size_t alignment) noexcept
        {
            if (pointer)
                ++m_state->deallocations;
            m_system.Deallocate(pointer, size, alignment);
        }

    private:
        std::shared_ptr<MoveOnlyAllocatorState> m_state;
        NGIN::Memory::SystemAllocator           m_system;
    };

    struct ThrowingOwner final
    {
        explicit ThrowingOwner(NGIN::Tests::FailureCountdown& failures) noexcept
            : m_failures(&failures)
        {
        }

        ThrowingOwner(const ThrowingOwner&) = delete;
        ThrowingOwner(ThrowingOwner&& other)
            : m_failures(other.m_failures)
        {
            m_failures->Hit();
        }

        ~ThrowingOwner() noexcept = default;

        NGIN::Tests::FailureCountdown* m_failures;
    };
}// namespace

TEST_CASE("Scoped pointers manage lifetime", "[Memory][SmartPointers]")
{
    using Tracked = NGIN::Memory::TrackingAllocator<NGIN::Memory::SystemAllocator>;

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
    using Tracked = NGIN::Memory::TrackingAllocator<NGIN::Memory::SystemAllocator>;

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
    using Tracked = NGIN::Memory::TrackingAllocator<NGIN::Memory::SystemAllocator>;

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
    using Tracked = NGIN::Memory::TrackingAllocator<NGIN::Memory::SystemAllocator>;

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

TEST_CASE("MakeSharedAs constructs derived as base and destroys virtually", "[Memory][SmartPointers]")
{
    PolyBase::destructed    = 0;
    PolyDerived::destructed = 0;

    {
        auto base = NGIN::Memory::MakeSharedAs<PolyBase, PolyDerived>(123);
        REQUIRE(base);
        CHECK(base->Value() == 123);

        auto copy = base;
        CHECK(base.UseCount() == 2U);
        copy.Reset();
        CHECK(base.UseCount() == 1U);
    }

    CHECK(PolyDerived::destructed == 1);
    CHECK(PolyBase::destructed == 1);
}

TEST_CASE("MakeSharedAs supports allocator overload", "[Memory][SmartPointers]")
{
    using Tracked = NGIN::Memory::TrackingAllocator<NGIN::Memory::SystemAllocator>;

    PolyBase::destructed    = 0;
    PolyDerived::destructed = 0;

    Tracked           tracking {NGIN::Memory::SystemAllocator {}};
    auto              allocatorRef = NGIN::Memory::AllocatorRef(tracking);
    const std::size_t baseline     = tracking.GetStats().currentBytes;

    {
        auto base = NGIN::Memory::MakeSharedAs<PolyBase, PolyDerived>(allocatorRef, 77);
        REQUIRE(base);
        CHECK(base->Value() == 77);
        CHECK(tracking.GetStats().currentCount == 1U);
    }

    CHECK(PolyDerived::destructed == 1);
    CHECK(PolyBase::destructed == 1);
    CHECK(tracking.GetStats().currentBytes == baseline);
    CHECK(tracking.GetStats().currentCount == 0U);
}

TEST_CASE("MakeSharedAlias retains an external owner", "[Memory][SmartPointers]")
{
    auto                 owner    = std::make_shared<Probe>(91);
    auto*                object   = owner.get();
    std::weak_ptr<Probe> lifetime = owner;

    auto alias = NGIN::Memory::MakeSharedAlias(object, std::move(owner));
    REQUIRE(alias);
    CHECK(alias.Get() == object);
    CHECK(alias->value == 91);
    CHECK_FALSE(lifetime.expired());

    auto copy = alias;
    alias.Reset();
    CHECK_FALSE(lifetime.expired());
    copy.Reset();
    CHECK(lifetime.expired());
}

TEST_CASE("MakeShared releases its allocation when object construction throws", "[Memory][SmartPointers]")
{
    using Allocator                         = NGIN::Tests::FailureAllocator<>;
    std::shared_ptr<Allocator::State> state = std::make_shared<Allocator::State>();
    Allocator                         allocator(state);
    NGIN::Tests::FailureCountdown     failures;
    failures.Arm(0);

    CHECK_THROWS_AS(NGIN::Memory::MakeShared<ThrowingProbe>(allocator, failures), std::runtime_error);
    CHECK(state->allocations == 1U);
    CHECK(state->deallocations == 1U);
}

TEST_CASE("MakeSharedAlias releases its control block when owner construction throws", "[Memory][SmartPointers]")
{
    using Allocator                         = NGIN::Tests::FailureAllocator<>;
    std::shared_ptr<Allocator::State> state = std::make_shared<Allocator::State>();
    Allocator                         allocator(state);
    NGIN::Tests::FailureCountdown     failures;
    ThrowingOwner                     owner(failures);
    int                               object = 42;
    failures.Arm(0);

    CHECK_THROWS_AS(
            NGIN::Memory::MakeSharedAlias<int>(allocator, &object, std::move(owner)),
            std::runtime_error);
    CHECK(state->allocations == 1U);
    CHECK(state->deallocations == 1U);
}

TEST_CASE("Shared final release supports a stateful move-only allocator", "[Memory][SmartPointers]")
{
    std::shared_ptr<MoveOnlyAllocatorState> state = std::make_shared<MoveOnlyAllocatorState>();
    {
        MoveOnlyAllocator                              allocator(state);
        NGIN::Memory::Shared<Probe, MoveOnlyAllocator> shared =
                NGIN::Memory::MakeShared<Probe>(std::move(allocator), 55);
        REQUIRE(shared);
        CHECK(shared->value == 55);
        CHECK(state->allocations == 1U);

        NGIN::Memory::Ticket<Probe, MoveOnlyAllocator> ticket = NGIN::Memory::MakeTicket(shared);
        shared.Reset();
        CHECK(ticket.Expired());
        CHECK(state->deallocations == 0U);
        ticket.Reset();
    }

    CHECK(state->allocations == 1U);
    CHECK(state->deallocations == 1U);
    CHECK(state->owningAllocatorDestructions == 1U);
}
