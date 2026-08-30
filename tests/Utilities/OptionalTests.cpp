/// @file OptionalTests.cpp
/// @brief Contract checks for the NGIN alias of `std::optional`.

#include <NGIN/Utilities/Optional.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <type_traits>
#include <utility>

namespace
{
    struct MoveOnly final
    {
        explicit MoveOnly(const int input) noexcept
            : value(input)
        {
        }

        MoveOnly(const MoveOnly&)                = delete;
        MoveOnly& operator=(const MoveOnly&)     = delete;
        MoveOnly(MoveOnly&&) noexcept            = default;
        MoveOnly& operator=(MoveOnly&&) noexcept = default;

        int value {0};
    };
}// namespace

static_assert(std::is_same_v<NGIN::Utilities::Optional<int>, std::optional<int>>);

TEST_CASE("Optional alias exposes standard empty and engaged states", "[Utilities][Optional]")
{
    NGIN::Utilities::Optional<int> value;
    CHECK_FALSE(value.has_value());

    value.emplace(123);
    REQUIRE(value.has_value());
    CHECK(value.value() == 123);
    CHECK(value.value_or(7) == 123);

    value.reset();
    CHECK_FALSE(value.has_value());
    CHECK(value.value_or(7) == 7);
}

TEST_CASE("Optional alias supports standard swap and move-only extraction", "[Utilities][Optional]")
{
    NGIN::Utilities::Optional<MoveOnly> left(std::in_place, 11);
    NGIN::Utilities::Optional<MoveOnly> right;

    left.swap(right);
    CHECK_FALSE(left.has_value());
    REQUIRE(right.has_value());

    MoveOnly extracted = std::move(right).value();
    CHECK(extracted.value == 11);
}
