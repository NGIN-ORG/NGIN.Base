/// @file ConcurrentHashMapCoverage.cpp
/// @brief Coverage / integrity tests for ConcurrentHashMap focused on key preservation across migrations.

#include <NGIN/Containers/ConcurrentHashMap.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

using NGIN::Containers::ConcurrentHashMap;

namespace
{
	struct KV
	{
		std::uint64_t key {0};
		std::uint64_t value {0};
	};
}// namespace

TEST_CASE("ConcurrentHashMap preserves all inserted keys under concurrent growth",
		  "[Containers][ConcurrentHashMap][Coverage]")
{
	constexpr std::size_t threadCount      = 8;
	constexpr std::size_t insertsPerThread = 5000; // 40k total
	constexpr std::size_t totalKeys        = threadCount * insertsPerThread;

	ConcurrentHashMap<std::uint64_t, std::uint64_t> map(64);

	std::atomic<bool> start {false};
	std::vector<std::thread> writers;
	writers.reserve(threadCount);

	for (std::size_t t = 0; t < threadCount; ++t)
	{
		writers.emplace_back([t, &map, &start]() {
			while (!start.load(std::memory_order_acquire))
			{
				std::this_thread::yield();
			}
			const std::uint64_t base = static_cast<std::uint64_t>(t * insertsPerThread);
			for (std::size_t i = 0; i < insertsPerThread; ++i)
			{
				const std::uint64_t k = base + i;
				// Value equals key * 3 to cross-check retrieval correctness.
				map.Insert(k, k * 3ULL);
			}
		});
	}

	// Concurrent reader performing Contains / TryGet probes during growth.
	std::atomic<bool> stopReader {false};
	std::thread       reader([&]() {
		std::mt19937_64                       rng(123456);
		std::uniform_int_distribution<size_t> dist(0, totalKeys - 1);
		std::uint64_t                         sink = 0;
		while (!start.load(std::memory_order_acquire))
		{
			std::this_thread::yield();
		}
		while (!stopReader.load(std::memory_order_acquire))
		{
			const auto sampleKey = static_cast<std::uint64_t>(dist(rng));
			if (map.Contains(sampleKey))
			{
				// TryGet should succeed and produce sampleKey*3 once inserted; ignore failures pre-insert.
				if (map.TryGet(sampleKey, sink))
				{
					CHECK(sink == sampleKey * 3ULL);
				}
			}
			std::this_thread::yield();
		}
	});

	start.store(true, std::memory_order_release);

	for (auto& w: writers)
	{
		w.join();
	}
	stopReader.store(true, std::memory_order_release);
	reader.join();

	// Ensure any in-flight migrations finalize before validation.
	map.Quiesce();

	REQUIRE(map.Size() == totalKeys);

	// Verify every key is present and mapped to expected value.
	std::size_t missingCount = 0;
	for (std::uint64_t k = 0; k < totalKeys; ++k)
	{
		if (!map.Contains(k))
		{
			++missingCount;
			INFO("Missing key=" << k << " size=" << map.Size() << " totalKeys=" << totalKeys);
			REQUIRE(false); // fail immediately to surface diagnostic
		}
		const auto v = map.Get(k);
		if (v != k * 3ULL)
		{
			INFO("Mismatched value key=" << k << " value=" << v << " expected=" << (k * 3ULL));
			REQUIRE(false);
		}
	}
	REQUIRE(missingCount == 0);
}

TEST_CASE("ConcurrentHashMap maintains key integrity across repeated reserve cycles",
		  "[Containers][ConcurrentHashMap][Coverage]")
{
	ConcurrentHashMap<std::uint64_t, std::uint64_t> map(8);
	constexpr std::size_t rounds  = 64;
	constexpr std::size_t stride  = 256;
	constexpr std::size_t repeats = 4;

	for (std::size_t r = 0; r < rounds; ++r)
	{
		map.Reserve((r + 1) * stride);
		for (std::size_t s = 0; s < stride; ++s)
		{
			const std::uint64_t key = r * stride + s;
			map.Insert(key, key + 1ULL);
		}
	}

	map.Quiesce();
	const std::size_t expectedSize = rounds * stride;
	REQUIRE(map.Size() == expectedSize);

	// Probe a subset multiple times to stress post-finalization reads.
	for (std::size_t pass = 0; pass < repeats; ++pass)
	{
		for (std::size_t k = 0; k < expectedSize; k += 17) // step to reduce runtime
		{
			REQUIRE(map.Contains(k));
			REQUIRE(map.Get(k) == k + 1ULL);
		}
	}
}

