/// @file ConcurrentHashMapTest.cpp
/// @brief Tests for NGIN::Containers::ConcurrentHashMap using Catch2.

#include <NGIN/Containers/ConcurrentHashMap.hpp>
#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>

using NGIN::Containers::ConcurrentHashMap;

TEST_CASE("ConcurrentHashMap default construction", "[Containers][ConcurrentHashMap]")
{
    ConcurrentHashMap<int, int> map;
    CHECK(map.Size() == 0U);
}

TEST_CASE("ConcurrentHashMap insert and get", "[Containers][ConcurrentHashMap]")
{
    ConcurrentHashMap<std::string, int> map;
    map.Insert("one", 1);
    map.Insert("two", 2);
    CHECK(map.Size() == 2U);
    CHECK(map.Get("one") == 1);
    CHECK(map.Get("two") == 2);
}

TEST_CASE("ConcurrentHashMap updates values", "[Containers][ConcurrentHashMap]")
{
    ConcurrentHashMap<std::string, int> map;
    map.Insert("key", 10);
    map.Insert("key", 20);
    CHECK(map.Size() == 1U);
    CHECK(map.Get("key") == 20);
}

TEST_CASE("ConcurrentHashMap handles rvalues", "[Containers][ConcurrentHashMap]")
{
    ConcurrentHashMap<std::string, std::string> map;
    std::string                                 value = "value";
    map.Insert("key", std::move(value));
    CHECK(map.Get("key") == "value");
}

TEST_CASE("ConcurrentHashMap removes keys", "[Containers][ConcurrentHashMap]")
{
    ConcurrentHashMap<int, int> map;
    map.Insert(1, 100);
    map.Insert(2, 200);
    map.Remove(1);

    CHECK(map.Size() == 1U);
    CHECK_THROWS_AS(map.Get(1), std::out_of_range);
    CHECK(map.Get(2) == 200);
}

TEST_CASE("ConcurrentHashMap contains lifecycle", "[Containers][ConcurrentHashMap]")
{
    ConcurrentHashMap<int, int> map;
    CHECK_FALSE(map.Contains(42));

    map.Insert(42, 99);
    CHECK(map.Contains(42));
    CHECK_FALSE(map.Contains(99));

    CHECK_FALSE(map.Contains(100));
    map.Insert(100, 1);
    CHECK(map.Contains(100));
    map.Remove(100);
    CHECK_FALSE(map.Contains(100));
    map.Insert(100, 2);
    CHECK(map.Contains(100));

    map.Insert(42, 1234);
    CHECK(map.Contains(42));
    map.Remove(42);
    CHECK_FALSE(map.Contains(42));
    map.Insert(42, 777);
    CHECK(map.Contains(42));
}

TEST_CASE("ConcurrentHashMap clear", "[Containers][ConcurrentHashMap]")
{
    ConcurrentHashMap<int, int> map;
    map.Insert(1, 1);
    map.Insert(2, 2);
    map.Clear();
    CHECK(map.Size() == 0U);
}

TEST_CASE("ConcurrentHashMap Get throws when missing", "[Containers][ConcurrentHashMap]")
{
    ConcurrentHashMap<int, int> map;
    CHECK_THROWS_AS(map.Get(999), std::out_of_range);
}

TEST_CASE("ConcurrentHashMap ignores missing removals", "[Containers][ConcurrentHashMap]")
{
    ConcurrentHashMap<int, int> map;
    map.Insert(1, 1);
    map.Remove(999);
    CHECK(map.Size() == 1U);
}

TEST_CASE("ConcurrentHashMap resizes as it grows", "[Containers][ConcurrentHashMap]")
{
    ConcurrentHashMap<int, int> map(8);
    constexpr int               count = 500;
    for (int i = 0; i < count; ++i)
    {
        map.Insert(i, i * 2);
    }

    CHECK(map.Size() == static_cast<std::size_t>(count));
    CHECK(map.Get(0) == 0);
    CHECK(map.Get(123) == 246);
    CHECK(map.Get(count - 1) == (count - 1) * 2);
}

TEST_CASE("ConcurrentHashMap TryGet and optional", "[Containers][ConcurrentHashMap]")
{
    ConcurrentHashMap<int, int> map;
    map.Insert(7, 70);
    int value = 0;
    CHECK(map.TryGet(7, value));
    CHECK(value == 70);
    CHECK_FALSE(map.TryGet(99, value));

    auto optional = map.GetOptional(7);
    REQUIRE(optional.has_value());
    CHECK(*optional == 70);
    CHECK_FALSE(map.GetOptional(88).has_value());
}

TEST_CASE("ConcurrentHashMap handles tombstones", "[Containers][ConcurrentHashMap]")
{
    ConcurrentHashMap<int, int> map;
    map.Insert(5, 500);
    map.Remove(5);
    CHECK_FALSE(map.Contains(5));
    const auto sizeAfterRemove = map.Size();
    map.Remove(5);
    CHECK(map.Size() == sizeAfterRemove);
    map.Insert(5, 600);
    CHECK(map.Get(5) == 600);
    CHECK(map.Size() == sizeAfterRemove + 1);
}

TEST_CASE("ConcurrentHashMap handles collision chains", "[Containers][ConcurrentHashMap]")
{
    ConcurrentHashMap<std::string, int> map(4);
    for (int i = 0; i < 64; ++i)
    {
        map.Insert("k_" + std::to_string(i * 16), i);
    }

    CHECK(map.Size() == 64U);
    CHECK(map.Contains("k_0"));
    CHECK(map.Get("k_0") == 0);
}
