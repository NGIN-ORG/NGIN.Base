/// @file StorageForTests.cpp
/// @brief Tests for NGIN::Memory::StorageFor.

#include <NGIN/Memory/StorageFor.hpp>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace
{
    struct TrivialPod
    {
        int a {0};
        int b {0};
    };

    struct NonTrivial
    {
        inline static int s_destructCount = 0;

        NonTrivial() = default;
        NonTrivial(const NonTrivial&) = default;
        NonTrivial(NonTrivial&&) = default;
        NonTrivial& operator=(const NonTrivial&) = default;
        NonTrivial& operator=(NonTrivial&&) = default;

        ~NonTrivial() { ++s_destructCount; }

        int value {0};
    };

    static_assert(std::is_trivially_copyable_v<TrivialPod>);
    static_assert(!std::is_trivially_destructible_v<NonTrivial>);
}

TEST_CASE("StorageFor is trivially copyable when T is trivially copyable", "[Memory][StorageFor]")
{
    using Storage = NGIN::Memory::StorageFor<TrivialPod>;

    static_assert(std::is_trivially_copyable_v<Storage>);
    static_assert(std::is_trivially_move_constructible_v<Storage>);
    static_assert(std::is_trivially_copy_constructible_v<Storage>);
    static_assert(std::is_trivially_destructible_v<Storage>);

    static_assert(std::is_copy_constructible_v<Storage>);
    static_assert(std::is_move_constructible_v<Storage>);
    static_assert(std::is_copy_assignable_v<Storage>);
    static_assert(std::is_move_assignable_v<Storage>);
}

TEST_CASE("StorageFor is not copyable/movable when T is not trivially copyable", "[Memory][StorageFor]")
{
    using Storage = NGIN::Memory::StorageFor<NonTrivial>;

    static_assert(!std::is_copy_constructible_v<Storage>);
    static_assert(!std::is_move_constructible_v<Storage>);
    static_assert(!std::is_copy_assignable_v<Storage>);
    static_assert(!std::is_move_assignable_v<Storage>);
}

TEST_CASE("StorageFor Construct/Ref/Destroy drives lifetime", "[Memory][StorageFor]")
{
    NonTrivial::s_destructCount = 0;

    NGIN::Memory::StorageFor<NonTrivial> storage;
    storage.Construct();
    storage.Ref().value = 42;

    CHECK(storage.Ref().value == 42);

    storage.Destroy();
    CHECK(NonTrivial::s_destructCount == 1);
}
