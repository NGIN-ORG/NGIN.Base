/// @file AllocatorContractTests.cpp
/// @brief Tests for allocation failure, checked arithmetic, and tri-state ownership contracts.

#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/FallbackAllocator.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Memory/detail/CheckedArithmetic.hpp>

#include "../Support/FailureInjection.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <limits>
#include <memory>

namespace
{
    struct ThrowingAllocateSignature
    {
        void* Allocate(std::size_t, std::size_t)
        {
            return nullptr;
        }

        void Deallocate(void*, std::size_t, std::size_t) noexcept {}
    };

    static_assert(!NGIN::Memory::AllocatorConcept<ThrowingAllocateSignature>);
    static_assert(NGIN::Memory::AllocatorConcept<NGIN::Tests::FailureAllocator<>>);
    static_assert(NGIN::Memory::AllocatorReportsPreciseOwnership<NGIN::Tests::FailureAllocator<>>);
    static_assert(!NGIN::Memory::AllocatorReportsPreciseOwnership<NGIN::Memory::SystemAllocator>);
}// namespace

TEST_CASE("checked allocator arithmetic rejects overflow", "[Memory][AllocatorContract]")
{
    std::size_t result = 17;
    CHECK_FALSE(NGIN::Memory::detail::CheckedAdd(
            (std::numeric_limits<std::size_t>::max)(), 1, result));
    CHECK(result == 17);

    CHECK_FALSE(NGIN::Memory::detail::CheckedMultiply(
            (std::numeric_limits<std::size_t>::max)(), 2, result));
    CHECK(result == 17);

    CHECK_FALSE(NGIN::Memory::detail::TryNormalizeAlignment(
            (std::numeric_limits<std::size_t>::max)(), alignof(std::max_align_t), result));
    CHECK(result == 17);
}

TEST_CASE("system allocator reports unknown pointer ownership", "[Memory][AllocatorContract]")
{
    NGIN::Memory::SystemAllocator allocator;
    int                           external = 0;

    CHECK(allocator.OwnershipOf(&external) == NGIN::Memory::Ownership::Unknown);
}

TEST_CASE("failure allocator injects exhaustion without throwing", "[Memory][AllocatorContract]")
{
    using Allocator                           = NGIN::Tests::FailureAllocator<>;
    std::shared_ptr<Allocator::State> state   = std::make_shared<Allocator::State>();
    state->successfulAllocationsBeforeFailure = 1;
    Allocator allocator {state};

    void* first  = allocator.Allocate(32, alignof(std::max_align_t));
    void* second = allocator.Allocate(32, alignof(std::max_align_t));

    REQUIRE(first != nullptr);
    CHECK(second == nullptr);
    CHECK(allocator.OwnershipOf(first) == NGIN::Memory::Ownership::Owns);

    allocator.Deallocate(first, 32, alignof(std::max_align_t));
    CHECK(allocator.OwnershipOf(first) == NGIN::Memory::Ownership::DoesNotOwn);
    CHECK(state->allocations == 1);
    CHECK(state->deallocations == 1);
}

TEST_CASE("fallback allocator routes only from definitive ownership", "[Memory][AllocatorContract]")
{
    using Allocator                                  = NGIN::Tests::FailureAllocator<>;
    std::shared_ptr<Allocator::State> primaryState   = std::make_shared<Allocator::State>();
    std::shared_ptr<Allocator::State> secondaryState = std::make_shared<Allocator::State>();
    primaryState->successfulAllocationsBeforeFailure = 1;

    NGIN::Memory::FallbackAllocator<Allocator, Allocator> allocator {
            Allocator {primaryState},
            Allocator {secondaryState}};

    void* primaryPointer   = allocator.Allocate(16, alignof(std::max_align_t));
    void* secondaryPointer = allocator.Allocate(16, alignof(std::max_align_t));

    REQUIRE(primaryPointer != nullptr);
    REQUIRE(secondaryPointer != nullptr);
    CHECK(primaryState->allocations == 1);
    CHECK(secondaryState->allocations == 1);

    allocator.Deallocate(primaryPointer, 16, alignof(std::max_align_t));
    allocator.Deallocate(secondaryPointer, 16, alignof(std::max_align_t));
    CHECK(primaryState->deallocations == 1);
    CHECK(secondaryState->deallocations == 1);
}
