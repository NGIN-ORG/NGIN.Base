/// @file ConcurrentHashMapTest.cpp
/// @brief Tests for NGIN::Containers::ConcurrentHashMap using boost::ut.
/// @details
/// Baseline (single-thread) validation for phases 1-3: insertion, update, rvalue paths,
/// tombstone (remove + reinsert), TryGet / GetOptional semantics, approximate collision
/// chain coverage, resize growth, idempotent removal, and clear behavior. Concurrent
/// multi-thread stress & cooperative online resize will be added in later phases.

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
        map.Insert("key", 20);// update
        expect(map.Size() == 1_ul);
        expect(map.Get("key") == 20_i);
    };

    "InsertRvalue"_test = [] {
        NGIN::Containers::ConcurrentHashMap<std::string, std::string> map;
        std::string                                                   val = "value";
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
        // Absent key
        expect(map.Contains(42) == false);
        // Single insert
        map.Insert(42, 99);
        expect(map.Contains(42) == true);
        expect(map.Contains(99) == false);// different value used as key
        // Additional key lifecycle
        expect(map.Contains(100) == false);
        map.Insert(100, 1);
        expect(map.Contains(100) == true);
        map.Remove(100);
        expect(map.Contains(100) == false);// tombstoned
        map.Insert(100, 2);                // reinsert after tombstone
        expect(map.Contains(100) == true);
        // Update existing key value should not change Contains result
        map.Insert(42, 1234);
        expect(map.Contains(42) == true);
        // Remove original key and ensure Contains reflects tombstone
        map.Remove(42);
        expect(map.Contains(42) == false);
        // Reinsertion after removal
        map.Insert(42, 777);
        expect(map.Contains(42) == true);
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
        map.Remove(999);// Should not throw
        expect(map.Size() == 1_ul);
    };

    "InsertManyElementsAndResize"_test = [] {
        NGIN::Containers::ConcurrentHashMap<int, int> map(8);
        const int                                     num = 500;// large enough to trigger several resizes (load factor > 0.75)
        for (int i = 0; i < num; ++i)
        {
            map.Insert(i, i * 2);
        }
        expect(map.Size() == static_cast<std::size_t>(num));
        // spot checks
        expect(map.Get(0) == 0_i);
        expect(map.Get(123) == 246_i);
        expect(map.Get(num - 1) == ((num - 1) * 2));
    };

    "TryGetAndOptional"_test = [] {
        NGIN::Containers::ConcurrentHashMap<int, int> map;
        map.Insert(7, 70);
        int out = 0;
        expect(map.TryGet(7, out) == true);
        expect(out == 70_i);
        expect(map.TryGet(99, out) == false);
        auto opt = map.GetOptional(7);
        expect(bool(opt) == true);
        expect(*opt == 70_i);
        expect(!map.GetOptional(88));
    };

    "TombstoneReinsert"_test = [] {
        NGIN::Containers::ConcurrentHashMap<int, int> map;
        map.Insert(5, 500);
        map.Remove(5);
        expect(map.Contains(5) == false);
        auto szAfterRemove = map.Size();
        map.Remove(5);// idempotent remove should not change size
        expect(map.Size() == szAfterRemove);
        map.Insert(5, 600);
        expect(map.Get(5) == 600_i);
        expect(eq(map.Size(), szAfterRemove + 1));
    };

    "CollisionChainApprox"_test = [] {
        NGIN::Containers::ConcurrentHashMap<std::string, int> map(4);
        // Create keys likely to collide by varying suffix; reliance on std::hash distribution.
        for (int i = 0; i < 64; ++i)
        {
            map.Insert("k_" + std::to_string(i * 16), i);
        }
        expect(map.Size() == 64_ul);
        expect(map.Contains("k_0") == true);
        expect(map.Get("k_0") == 0_i);
    };
};
