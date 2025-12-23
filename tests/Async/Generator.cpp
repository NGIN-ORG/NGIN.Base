#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

#include <NGIN/Async/Generator.hpp>

namespace
{
    NGIN::Async::Generator<int> Range(int count)
    {
        for (int i = 0; i < count; ++i)
        {
            co_yield i;
        }
    }

    NGIN::Async::Generator<int> YieldThenThrow()
    {
        co_yield 1;
        throw std::runtime_error("boom");
    }
}// namespace

TEST_CASE("Generator yields a sequence of values")
{
    std::vector<int> values;
    for (const auto v: Range(5))
    {
        values.push_back(v);
    }

    REQUIRE(values.size() == 5);
    REQUIRE(values[0] == 0);
    REQUIRE(values[1] == 1);
    REQUIRE(values[2] == 2);
    REQUIRE(values[3] == 3);
    REQUIRE(values[4] == 4);
}

TEST_CASE("Generator propagates exceptions on resume")
{
    auto gen = YieldThenThrow();

    auto it = gen.begin();
    REQUIRE(it != std::default_sentinel);
    REQUIRE(*it == 1);

    REQUIRE_THROWS_AS(++it, std::runtime_error);
}

