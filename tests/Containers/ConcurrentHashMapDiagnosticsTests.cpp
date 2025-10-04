#include <catch2/catch_all.hpp>
#include <NGIN/Containers/ConcurrentHashMap.hpp>

using NGIN::Containers::ConcurrentHashMap;

TEST_CASE("ConcurrentHashMap diagnostics counters basic", "[Containers][ConcurrentHashMap][Diagnostics]")
{
    ConcurrentHashMap<int, int> map(16);
    map.ResetDiagnostics();

    // Insert new key
    REQUIRE(map.Insert(1, 10));
    auto d1 = map.GetDiagnostics();
    REQUIRE(d1.insertCalls >= 1);
    REQUIRE(d1.insertSuccessNew >= 1);
    REQUIRE(d1.insertSuccessUpdate == 0);

    // Update existing key
    REQUIRE_FALSE(map.Insert(1, 20));// same key, should be update path
    auto d2 = map.GetDiagnostics();
    REQUIRE(d2.insertSuccessUpdate >= 1);
    REQUIRE(d2.insertSuccessNew == d1.insertSuccessNew);// no additional new success

    // Probe step stats should be non-zero for at least one path (not strictly guaranteed, so soft check)
    REQUIRE(d2.insertProbeSteps >= d1.insertProbeSteps);
}
