/// @file ExpectedTests.cpp
/// @brief Contract checks for the NGIN aliases of C++23 expected vocabulary types.

#include <NGIN/Utilities/Expected.hpp>

#include <catch2/catch_test_macros.hpp>

#include <expected>
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

static_assert(std::is_same_v<NGIN::Utilities::Expected<int, int>, std::expected<int, int>>);
static_assert(std::is_same_v<NGIN::Utilities::Unexpected<int>, std::unexpected<int>>);

TEST_CASE("Expected alias exposes standard value and error access", "[Utilities][Expected]")
{
    NGIN::Utilities::Expected<int, int> value(std::in_place, 42);
    NGIN::Utilities::Expected<int, int> error(std::unexpected(7));

    REQUIRE(value.has_value());
    CHECK(value.value() == 42);
    REQUIRE_FALSE(error.has_value());
    CHECK(error.error() == 7);
    CHECK(value.value_or(9) == 42);
    CHECK(error.value_or(9) == 9);
}

TEST_CASE("Expected alias supports move-only value and error payloads", "[Utilities][Expected]")
{
    NGIN::Utilities::Expected<MoveOnly, int> value(std::in_place, 11);
    MoveOnly                                 extractedValue = std::move(value).value();
    CHECK(extractedValue.value == 11);

    NGIN::Utilities::Expected<int, MoveOnly> error(std::unexpect, 19);
    MoveOnly                                 extractedError = std::move(error).error();
    CHECK(extractedError.value == 19);
}

TEST_CASE("Expected void alias uses the standard success representation", "[Utilities][Expected]")
{
    NGIN::Utilities::Expected<void, int> success;
    NGIN::Utilities::Expected<void, int> failure(std::unexpected(5));

    CHECK(success.has_value());
    REQUIRE_FALSE(failure.has_value());
    CHECK(failure.error() == 5);
}
