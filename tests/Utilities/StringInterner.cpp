#include <catch2/catch_test_macros.hpp>

#include <NGIN/Utilities/StringInterner.hpp>

#include <NGIN/Memory/SystemAllocator.hpp>

#include <array>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
    struct CountingAllocator : NGIN::Memory::SystemAllocator
    {
        mutable int allocations {0};
        mutable int deallocations {0};

        void* Allocate(std::size_t size, std::size_t alignment) noexcept
        {
            ++allocations;
            return NGIN::Memory::SystemAllocator::Allocate(size, alignment);
        }

        void Deallocate(void* ptr, std::size_t size, std::size_t alignment) noexcept
        {
            ++deallocations;
            NGIN::Memory::SystemAllocator::Deallocate(ptr, size, alignment);
        }
    };
}// namespace

TEST_CASE("StringInterner deduplicates strings")
{
    NGIN::Utilities::StringInterner<> interner;

    auto id1 = interner.InsertOrGet("alpha");
    auto id2 = interner.InsertOrGet("alpha");

    REQUIRE(id1 != NGIN::Utilities::StringInterner<>::INVALID_ID);
    REQUIRE(id1 == id2);

    auto view = interner.View(id1);
    REQUIRE(view == "alpha");

    auto secondView = interner.Intern("alpha");
    REQUIRE(secondView.data() == view.data());
    REQUIRE(interner.Size() == 1);

    auto stats = interner.GetStatistics();
    REQUIRE(stats.lookups == 3);
    REQUIRE(stats.lookupHits == 2);
    REQUIRE(stats.inserted == 1);
    REQUIRE(stats.totalBytesStored == 5);
    REQUIRE(stats.pageAllocations >= 1);
}

TEST_CASE("StringInterner TryGetId handles missing values")
{
    NGIN::Utilities::StringInterner<> interner;

    NGIN::Utilities::StringInterner<>::IdType out {};
    REQUIRE_FALSE(interner.TryGetId("beta", out));

    auto id = interner.InsertOrGet("beta");
    REQUIRE(interner.TryGetId("beta", out));
    REQUIRE(id == out);

    REQUIRE(interner.View(NGIN::Utilities::StringInterner<>::INVALID_ID).empty());

    auto stats = interner.GetStatistics();
    REQUIRE(stats.lookups == 3);
    REQUIRE(stats.lookupHits == 1);
    REQUIRE(stats.inserted == 1);
    REQUIRE(stats.totalBytesStored == 4);

    interner.ResetStatistics();
    auto reset = interner.GetStatistics();
    REQUIRE(reset.lookups == 0);
    REQUIRE(reset.lookupHits == 0);
    REQUIRE(reset.inserted == 0);
    REQUIRE(reset.totalBytesStored == 4);
}

TEST_CASE("StringInterner clears allocated pages")
{
    CountingAllocator                                  alloc;
    NGIN::Utilities::StringInterner<CountingAllocator> interner(alloc);

    auto& storedAlloc = interner.GetAllocator();

    auto large = std::string(6000, 'x');
    auto id    = interner.InsertOrGet(large);
    REQUIRE(id != NGIN::Utilities::StringInterner<CountingAllocator>::INVALID_ID);
    REQUIRE(storedAlloc.allocations >= 1);

    auto secondaryId = interner.InsertOrGet("secondary");
    (void) secondaryId;
    REQUIRE(interner.Size() == 2);

    interner.Clear();
    REQUIRE(interner.Empty());
    REQUIRE(storedAlloc.deallocations == storedAlloc.allocations);

    auto stats = interner.GetStatistics();
    REQUIRE(stats.totalBytesStored == 0);
    REQUIRE(stats.pageDeallocations >= 1);
}

TEST_CASE("StringInterner supports empty strings")
{
    NGIN::Utilities::StringInterner<> interner;

    auto id = interner.InsertOrGet("");
    REQUIRE(id != NGIN::Utilities::StringInterner<>::INVALID_ID);
    REQUIRE(interner.View(id).empty());

    NGIN::Utilities::StringInterner<>::IdType out {};
    REQUIRE(interner.TryGetId("", out));
    REQUIRE(out == id);

    interner.Clear();
    REQUIRE_FALSE(interner.TryGetId("", out));
}

TEST_CASE("StringInterner supports custom threading policy")
{
    using ThreadedInterner = NGIN::Utilities::StringInterner<NGIN::Memory::SystemAllocator, std::mutex>;

    ThreadedInterner           interner;
    std::array<std::string, 3> values {"alpha", "beta", "gamma"};

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&]() {
            for (int i = 0; i < 100; ++i)
            {
                auto id = interner.InsertOrGet(values[static_cast<std::size_t>(i) % values.size()]);
                (void) id;
            }
        });
    }
    for (auto& thread: threads)
        thread.join();

    REQUIRE(interner.Size() == values.size());

    auto stats = interner.GetStatistics();
    REQUIRE(stats.inserted == values.size());
    REQUIRE(stats.totalBytesStored == 14);// lengths 5 + 4 + 5
    REQUIRE(stats.lookupHits == stats.lookups - stats.inserted);
}

TEST_CASE("StringInterner grows across multiple pages")
{
    NGIN::Utilities::StringInterner<> interner;

    const std::string first(6000, 'a');
    const std::string second(9000, 'b');

    auto firstId  = interner.InsertOrGet(first);
    auto secondId = interner.InsertOrGet(second);

    REQUIRE(firstId != NGIN::Utilities::StringInterner<>::INVALID_ID);
    REQUIRE(secondId != NGIN::Utilities::StringInterner<>::INVALID_ID);
    REQUIRE(interner.Size() == 2);

    auto stats = interner.GetStatistics();
    REQUIRE(stats.pageAllocations >= 2);
    REQUIRE(stats.totalBytesStored == first.size() + second.size());
    REQUIRE(interner.View(firstId).size() == first.size());
    REQUIRE(interner.View(secondId).size() == second.size());
}

TEST_CASE("StringInterner Intern returns stable views")
{
    NGIN::Utilities::StringInterner<> interner;

    auto view1 = interner.Intern("component");
    auto view2 = interner.Intern("component");

    REQUIRE(view1.data() == view2.data());
    REQUIRE(view1 == view2);

    auto stats = interner.GetStatistics();
    REQUIRE(stats.inserted == 1);
    REQUIRE(stats.lookupHits == stats.lookups - stats.inserted);
}
