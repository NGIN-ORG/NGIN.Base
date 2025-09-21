/// @file HashMapTest.cpp
/// @brief Tests for NGIN::Containers::FlatHashMap using Catch2.

#include <NGIN/Containers/HashMap.hpp>
#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>

using NGIN::Containers::FlatHashMap;

TEST_CASE("FlatHashMap default construction", "[Containers][FlatHashMap]")
{
    FlatHashMap<int, int> map;
    CHECK(map.Size() == 0U);
    CHECK(map.Capacity() >= 16U);
}

TEST_CASE("FlatHashMap insert and get", "[Containers][FlatHashMap]")
{
    FlatHashMap<std::string, int> map;
    map.Insert("one", 1);
    map.Insert("two", 2);
    CHECK(map.Size() == 2U);
    CHECK(map.Get("one") == 1);
    CHECK(map.Get("two") == 2);
}

TEST_CASE("FlatHashMap insert updates existing values", "[Containers][FlatHashMap]")
{
    FlatHashMap<std::string, int> map;
    map.Insert("key", 10);
    map.Insert("key", 20);
    CHECK(map.Size() == 1U);
    CHECK(map.Get("key") == 20);
}

TEST_CASE("FlatHashMap accepts rvalue values", "[Containers][FlatHashMap]")
{
    FlatHashMap<std::string, std::string> map;
    std::string                           value = "value";
    map.Insert("key", std::move(value));
    CHECK(map.Get("key") == "value");
}

TEST_CASE("FlatHashMap removes keys", "[Containers][FlatHashMap]")
{
    FlatHashMap<int, int> map;
    map.Insert(1, 100);
    map.Insert(2, 200);
    map.Remove(1);

    CHECK(map.Size() == 1U);
    CHECK_THROWS_AS(map.Get(1), std::out_of_range);
    CHECK(map.Get(2) == 200);
}

TEST_CASE("FlatHashMap contains check", "[Containers][FlatHashMap]")
{
    FlatHashMap<int, int> map;
    map.Insert(42, 99);
    CHECK(map.Contains(42));
    CHECK_FALSE(map.Contains(99));
}

TEST_CASE("FlatHashMap clear preserves capacity", "[Containers][FlatHashMap]")
{
    FlatHashMap<int, int> map;
    map.Insert(1, 1);
    map.Insert(2, 2);
    const auto capacity = map.Capacity();
    map.Clear();
    CHECK(map.Size() == 0U);
    CHECK(map.Capacity() == capacity);
}

TEST_CASE("FlatHashMap operator[] inserts and updates", "[Containers][FlatHashMap]")
{
    FlatHashMap<std::string, int> map;
    map["foo"] = 123;
    CHECK(map["foo"] == 123);
    map["foo"] = 456;
    CHECK(map["foo"] == 456);
}

TEST_CASE("FlatHashMap Get throws when missing", "[Containers][FlatHashMap]")
{
    FlatHashMap<int, int> map;
    CHECK_THROWS_AS(map.Get(999), std::out_of_range);
}

TEST_CASE("FlatHashMap grows capacity automatically", "[Containers][FlatHashMap]")
{
    FlatHashMap<int, int> map;
    const auto            initialCapacity = map.Capacity();
    for (int i = 0; i < static_cast<int>(initialCapacity * 2); ++i)
    {
        map.Insert(i, i * 10);
    }

    CHECK(map.Size() == initialCapacity * 2);
    CHECK(map.Capacity() >= initialCapacity * 2);
    CHECK(map.Get(0) == 0);
    CHECK(map.Get(static_cast<int>(initialCapacity * 2 - 1)) == static_cast<int>((initialCapacity * 2 - 1) * 10));
}

TEST_CASE("FlatHashMap ignore removals of missing keys", "[Containers][FlatHashMap]")
{
    FlatHashMap<int, int> map;
    map.Insert(1, 1);
    map.Remove(999);
    CHECK(map.Size() == 1U);
}

TEST_CASE("FlatHashMap handles bulk insertions", "[Containers][FlatHashMap]")
{
    FlatHashMap<int, int> map;
    constexpr int         count = 1000;
    for (int i = 0; i < count; ++i)
    {
        map.Insert(i, i);
    }

    CHECK(map.Size() == static_cast<std::size_t>(count));
    CHECK(map.Get(0) == 0);
    CHECK(map.Get(500) == 500);
    CHECK(map.Get(999) == 999);
}
