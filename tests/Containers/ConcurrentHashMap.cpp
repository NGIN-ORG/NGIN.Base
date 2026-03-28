/// @file ConcurrentHashMapTest.cpp
/// @brief Functional tests for the rebuilt ConcurrentHashMap scaffold.

#include <NGIN/Containers/ConcurrentHashMap.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace
{
    template<NGIN::Containers::ReclamationPolicy Policy>
    using IntMap = NGIN::Containers::ConcurrentHashMap<int, int, std::hash<int>, std::equal_to<int>, NGIN::Memory::SystemAllocator, Policy, 8>;

    template<NGIN::Containers::ReclamationPolicy Policy>
    void RunBasicLifecycle()
    {
        IntMap<Policy> map(16);

        CHECK(map.Empty());
        CHECK(map.Size() == 0U);
        CHECK(map.Capacity() >= 16U);

        CHECK(map.Insert(1, 10));
        CHECK(map.Insert(2, 20));
        CHECK_FALSE(map.Insert(1, 30));
        CHECK(map.Size() == 2U);
        CHECK(map.Get(1) == 30);
        CHECK(map.Get(2) == 20);

        int value = 0;
        CHECK(map.TryGet(1, value));
        CHECK(value == 30);
        CHECK_FALSE(map.TryGet(99, value));

        REQUIRE(map.GetOptional(2).has_value());
        CHECK(*map.GetOptional(2) == 20);
        CHECK_FALSE(map.GetOptional(77).has_value());

        CHECK(map.Contains(1));
        CHECK_FALSE(map.Contains(7));

        CHECK(map.Remove(1));
        CHECK_FALSE(map.Remove(1));
        CHECK_FALSE(map.Contains(1));
        CHECK(map.Size() == 1U);

        map.Clear();
        CHECK(map.Empty());
        CHECK(map.Size() == 0U);
    }
}// namespace

TEST_CASE("ConcurrentHashMap basic lifecycle works for all scaffold policies", "[Containers][ConcurrentHashMap]")
{
    SECTION("ManualQuiesce")
    {
        RunBasicLifecycle<NGIN::Containers::ReclamationPolicy::ManualQuiesce>();
    }

    SECTION("LocalEpoch")
    {
        RunBasicLifecycle<NGIN::Containers::ReclamationPolicy::LocalEpoch>();
    }

    SECTION("HazardPointers")
    {
        RunBasicLifecycle<NGIN::Containers::ReclamationPolicy::HazardPointers>();
    }
}

TEST_CASE("ConcurrentHashMap upsert merges existing values", "[Containers][ConcurrentHashMap]")
{
    IntMap<NGIN::Containers::ReclamationPolicy::LocalEpoch> map(8);

    CHECK(map.Upsert(7, 10, [](int& current, int fresh) {
        current += fresh;
    }));

    CHECK_FALSE(map.Upsert(7, 5, [](int& current, int fresh) {
        current += fresh;
    }));

    CHECK(map.Get(7) == 15);
}

TEST_CASE("ConcurrentHashMap reserve grows shard tables", "[Containers][ConcurrentHashMap]")
{
    IntMap<NGIN::Containers::ReclamationPolicy::LocalEpoch> map(8);
    const auto                                              initialCapacity = map.Capacity();

    map.Reserve(512);

    CHECK(map.Capacity() >= 512U);
    CHECK(map.Capacity() >= initialCapacity);
}

TEST_CASE("ConcurrentHashMap insert-or-assign updates existing values", "[Containers][ConcurrentHashMap]")
{
    IntMap<NGIN::Containers::ReclamationPolicy::LocalEpoch> map(8);

    CHECK(map.Insert(11, 100));
    CHECK_FALSE(map.InsertOrAssign(11, 250));
    CHECK(map.Get(11) == 250);

    CHECK(map.InsertOrAssign(12, 400));
    CHECK(map.Get(12) == 400);
}

TEST_CASE("ConcurrentHashMap foreach enumerates inserted values", "[Containers][ConcurrentHashMap]")
{
    IntMap<NGIN::Containers::ReclamationPolicy::LocalEpoch> map(32);
    REQUIRE(map.Insert(1, 100));
    REQUIRE(map.Insert(2, 200));
    REQUIRE(map.Insert(3, 300));

    std::vector<std::pair<int, int>> items;
    map.ForEach([&](const int& key, const int& value) {
        items.emplace_back(key, value);
    });

    REQUIRE(items.size() == 3U);
    std::sort(items.begin(), items.end());
    CHECK(items[0] == std::pair<int, int> {1, 100});
    CHECK(items[1] == std::pair<int, int> {2, 200});
    CHECK(items[2] == std::pair<int, int> {3, 300});
}

TEST_CASE("ConcurrentHashMap snapshot foreach matches foreach", "[Containers][ConcurrentHashMap]")
{
    IntMap<NGIN::Containers::ReclamationPolicy::LocalEpoch> map(32);
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(map.Insert(i, i * 10));
    }

    std::vector<std::pair<int, int>> directItems;
    std::vector<std::pair<int, int>> snapshotItems;

    map.ForEach([&](const int& key, const int& value) {
        directItems.emplace_back(key, value);
    });
    map.SnapshotForEach([&](const int& key, const int& value) {
        snapshotItems.emplace_back(key, value);
    });

    std::sort(directItems.begin(), directItems.end());
    std::sort(snapshotItems.begin(), snapshotItems.end());
    CHECK(snapshotItems == directItems);
}

TEST_CASE("ConcurrentHashMap supports string keys and values", "[Containers][ConcurrentHashMap]")
{
    using StringMap = NGIN::Containers::ConcurrentHashMap<std::string, std::string>;

    StringMap map(16);
    CHECK(map.Insert("alpha", "one"));
    CHECK_FALSE(map.Insert("alpha", "two"));
    CHECK(map.Get("alpha") == "two");
    CHECK(map.InsertOrAssign("beta", "three"));
    CHECK(map.Get("beta") == "three");
}
