/// @file ConcurrentHashMapTest.cpp
/// @brief Tests for NGIN::Containers::ConcurrentHashMap using boost::ut
/// @details
/// This file contains a series of tests to verify the correctness of the
/// ConcurrentHashMap container in various scenarios, including insertion, removal,
/// access, updating, and capacity management.
/// We use boost::ut for our unit testing framework.

#include <NGIN/Containers/ConcurrentHashMap.hpp>
#include <boost/ut.hpp>
#include <string>
#include <stdexcept>

using namespace boost::ut;

suite<"NGIN::Containers::ConcurrentHashMap"> concurrent_hashmap_tests = [] {
    "DefaultConstruction"_test = [] {
        NGIN::Containers::ConcurrentHashMap<int, int> map;
        expect(map.Size() == 0_ul);
    };

    "InsertAndGet"_test = [] {
        NGIN::Containers::ConcurrentHashMap<std::string, int> map;
        map.Insert("one", 1);
        map.Insert("two", 2);
        expect(map.Size() == 2_ul);
        expect(map.Get("one") == 1_i);
        expect(map.Get("two") == 2_i);
    };

    "InsertUpdateValue"_test = [] {
        NGIN::Containers::ConcurrentHashMap<std::string, int> map;
        map.Insert("key", 10);
        map.Insert("key", 20); // update
        expect(map.Size() == 1_ul);
        expect(map.Get("key") == 20_i);
    };

    "InsertRvalue"_test = [] {
        NGIN::Containers::ConcurrentHashMap<std::string, std::string> map;
        std::string val = "value";
        map.Insert("key", std::move(val));
        expect(map.Get("key") == "value");
    };

    "RemoveKey"_test = [] {
        NGIN::Containers::ConcurrentHashMap<int, int> map;
        map.Insert(1, 100);
        map.Insert(2, 200);
        map.Remove(1);
        expect(map.Size() == 1_ul);
        expect(throws<std::out_of_range>([&] { map.Get(1); }));
        expect(map.Get(2) == 200_i);
    };

    "ContainsKey"_test = [] {
        NGIN::Containers::ConcurrentHashMap<int, int> map;
        map.Insert(42, 99);
        expect(map.Contains(42) == true);
        expect(map.Contains(99) == false);
    };

    "ClearResetsSize"_test = [] {
        NGIN::Containers::ConcurrentHashMap<int, int> map;
        map.Insert(1, 1);
        map.Insert(2, 2);
        map.Clear();
        expect(map.Size() == 0_ul);
    };

    "GetThrowsIfNotFound"_test = [] {
        NGIN::Containers::ConcurrentHashMap<int, int> map;
        expect(throws<std::out_of_range>([&] { map.Get(999); }));
    };

    "RemoveNonExistentKeyDoesNotThrow"_test = [] {
        NGIN::Containers::ConcurrentHashMap<int, int> map;
        map.Insert(1, 1);
        map.Remove(999); // Should not throw
        expect(map.Size() == 1_ul);
    };

    "InsertManyElements"_test = [] {
        NGIN::Containers::ConcurrentHashMap<int, int> map;
        const int num = 100;
        for (int i = 0; i < num; ++i) {
            map.Insert(i, i);
        }
        expect(map.Size() == static_cast<std::size_t>(num));
        expect(map.Get(0) == 0_i);
        expect(map.Get(50) == 50_i);
        expect(map.Get(99) == 99_i);
    };
};
