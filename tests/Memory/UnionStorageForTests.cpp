/// @file UnionStorageForTests.cpp
/// @brief Tests for NGIN::Memory::UnionStorageFor.

#include <NGIN/Memory/UnionStorageFor.hpp>

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
    static_assert(!std::is_trivially_copyable_v<NonTrivial>);
}

TEST_CASE("UnionStorageFor reports max size/alignment", "[Memory][UnionStorageFor]")
{
    using Storage = NGIN::Memory::UnionStorageFor<int, double>;

    static_assert(Storage::Size() == (sizeof(double) > sizeof(int) ? sizeof(double) : sizeof(int)));
    static_assert(Storage::Alignment() == (alignof(double) > alignof(int) ? alignof(double) : alignof(int)));

    static_assert(sizeof(Storage) >= Storage::Size());
    static_assert(alignof(Storage) >= Storage::Alignment());
}

TEST_CASE("UnionStorageFor is trivially copyable when all Ts are trivially copyable", "[Memory][UnionStorageFor]")
{
    using Storage = NGIN::Memory::UnionStorageFor<int, double, TrivialPod>;

    static_assert(std::is_trivially_copyable_v<Storage>);
    static_assert(std::is_trivially_move_constructible_v<Storage>);
    static_assert(std::is_trivially_copy_constructible_v<Storage>);
    static_assert(std::is_trivially_destructible_v<Storage>);

    static_assert(std::is_copy_constructible_v<Storage>);
    static_assert(std::is_move_constructible_v<Storage>);
    static_assert(std::is_copy_assignable_v<Storage>);
    static_assert(std::is_move_assignable_v<Storage>);
}

TEST_CASE("UnionStorageFor is not copyable/movable when any T is not trivially copyable", "[Memory][UnionStorageFor]")
{
    using Storage = NGIN::Memory::UnionStorageFor<int, NonTrivial>;

    static_assert(!std::is_copy_constructible_v<Storage>);
    static_assert(!std::is_move_constructible_v<Storage>);
    static_assert(!std::is_copy_assignable_v<Storage>);
    static_assert(!std::is_move_assignable_v<Storage>);
}

TEST_CASE("UnionStorageFor Construct/Ref/Destroy drives lifetime", "[Memory][UnionStorageFor]")
{
    NonTrivial::s_destructCount = 0;

    NGIN::Memory::UnionStorageFor<NonTrivial, int> storage;

    storage.Construct<NonTrivial>();
    storage.Ref<NonTrivial>().value = 42;
    CHECK(storage.Ref<NonTrivial>().value == 42);

    storage.Destroy<NonTrivial>();
    CHECK(NonTrivial::s_destructCount == 1);

    storage.Construct<int>(7);
    CHECK(storage.Ref<int>() == 7);

    storage.Destroy<int>();
    CHECK(NonTrivial::s_destructCount == 1);
}
