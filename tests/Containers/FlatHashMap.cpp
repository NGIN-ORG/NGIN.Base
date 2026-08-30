/// @file HashMapTest.cpp
/// @brief Tests for NGIN::Containers::FlatHashMap using Catch2.

#include "../Support/FailureInjection.hpp"
#include <NGIN/Containers/FlatHashMap.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

using NGIN::Containers::FlatHashMap;

namespace
{
    class CopyFailingValue final
    {
    public:
        CopyFailingValue(const int value, NGIN::Tests::FailureCountdown& failures, std::size_t& live) noexcept
            : m_value(value), m_failures(&failures), m_live(&live)
        {
            ++*m_live;
        }

        CopyFailingValue(const CopyFailingValue& other)
            : m_value(other.m_value), m_failures(other.m_failures), m_live(other.m_live)
        {
            m_failures->Hit();
            ++*m_live;
        }

        CopyFailingValue(CopyFailingValue&& other) noexcept
            : m_value(other.m_value), m_failures(other.m_failures), m_live(other.m_live)
        {
            ++*m_live;
        }

        CopyFailingValue& operator=(const CopyFailingValue& other)
        {
            other.m_failures->Hit();
            m_value = other.m_value;
            return *this;
        }

        CopyFailingValue& operator=(CopyFailingValue&& other) noexcept
        {
            m_value = other.m_value;
            return *this;
        }

        ~CopyFailingValue()
        {
            --*m_live;
        }

        [[nodiscard]] int Value() const noexcept
        {
            return m_value;
        }

        friend bool operator==(const CopyFailingValue& left, const CopyFailingValue& right) noexcept
        {
            return left.m_value == right.m_value;
        }

    private:
        int                            m_value;
        NGIN::Tests::FailureCountdown* m_failures;
        std::size_t*                   m_live;
    };

    struct CopyFailingHash final
    {
        [[nodiscard]] std::size_t operator()(const CopyFailingValue& value) const noexcept
        {
            return std::hash<int> {}(value.Value());
        }
    };

    struct ThrowingHash final
    {
        NGIN::Tests::FailureCountdown* failures;

        [[nodiscard]] std::size_t operator()(const int value) const
        {
            failures->Hit();
            return std::hash<int> {}(value);
        }
    };

    struct ThrowingEqual final
    {
        NGIN::Tests::FailureCountdown* failures;

        [[nodiscard]] bool operator()(const int left, const int right) const
        {
            failures->Hit();
            return left == right;
        }
    };
}// namespace

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
    const std::size_t capacity = map.Capacity();
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
    const std::size_t     initialCapacity = map.Capacity();
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

TEST_CASE("FlatHashMap publishes a bucket only after key and value construction", "[Containers][FlatHashMap]")
{
    NGIN::Tests::FailureCountdown      failures;
    std::size_t                        live = 0;
    CopyFailingValue                   source(7, failures, live);
    FlatHashMap<int, CopyFailingValue> map;

    failures.Arm(0);
    CHECK_THROWS_AS(map.Insert(1, source), std::runtime_error);
    failures.Disable();

    CHECK(map.Size() == 0U);
    CHECK_FALSE(map.Contains(1));
    CHECK(live == 1U);
}

TEST_CASE("FlatHashMap leaves no bucket state when key construction fails", "[Containers][FlatHashMap]")
{
    NGIN::Tests::FailureCountdown                       failures;
    std::size_t                                         live = 0;
    CopyFailingValue                                    key(7, failures, live);
    FlatHashMap<CopyFailingValue, int, CopyFailingHash> map;

    failures.Arm(0);
    CHECK_THROWS_AS(map.Insert(key, 70), std::runtime_error);
    failures.Disable();

    CHECK(map.Size() == 0U);
    CHECK_FALSE(map.Contains(key));
    CHECK(live == 1U);
}

TEST_CASE("FlatHashMap copy construction rolls back partially copied entries", "[Containers][FlatHashMap]")
{
    NGIN::Tests::FailureCountdown failures;
    std::size_t                   live = 0;
    {
        CopyFailingValue                   first(1, failures, live);
        CopyFailingValue                   second(2, failures, live);
        FlatHashMap<int, CopyFailingValue> source;
        source.Insert(1, std::move(first));
        source.Insert(2, std::move(second));
        const std::size_t liveBeforeCopy = live;

        failures.Arm(1);
        CHECK_THROWS_AS((FlatHashMap<int, CopyFailingValue> {source}), std::runtime_error);
        failures.Disable();

        CHECK(source.Size() == 2U);
        CHECK(live == liveBeforeCopy);
    }
    CHECK(live == 0U);
}

TEST_CASE("FlatHashMap failed copy assignment preserves the destination", "[Containers][FlatHashMap]")
{
    NGIN::Tests::FailureCountdown failures;
    std::size_t                   live = 0;
    {
        CopyFailingValue                   sourceValue(10, failures, live);
        CopyFailingValue                   destinationValue(20, failures, live);
        FlatHashMap<int, CopyFailingValue> source;
        FlatHashMap<int, CopyFailingValue> destination;
        source.Insert(1, std::move(sourceValue));
        destination.Insert(2, std::move(destinationValue));
        const std::size_t liveBeforeCopy = live;

        failures.Arm(0);
        CHECK_THROWS_AS(destination = source, std::runtime_error);
        failures.Disable();

        CHECK(destination.Size() == 1U);
        CHECK(destination.GetRef(2).Value() == 20);
        CHECK(source.GetRef(1).Value() == 10);
        CHECK(live == liveBeforeCopy);
    }
    CHECK(live == 0U);
}

TEST_CASE("FlatHashMap growth allocation failure preserves entries", "[Containers][FlatHashMap]")
{
    using Allocator                                                            = NGIN::Tests::FailureAllocator<>;
    std::shared_ptr<Allocator::State>                                    state = std::make_shared<Allocator::State>();
    Allocator                                                            allocator(state);
    FlatHashMap<int, int, std::hash<int>, std::equal_to<int>, Allocator> map(
            16, std::hash<int> {}, std::equal_to<int> {}, allocator);

    for (int value = 0; value < 12; ++value)
        map.Insert(value, value * 10);
    state->successfulAllocationsBeforeFailure = state->allocationAttempts;

    CHECK_THROWS_AS(map.Insert(12, 120), std::bad_alloc);
    CHECK(map.Size() == 12U);
    CHECK(map.Capacity() == 16U);
    for (int value = 0; value < 12; ++value)
        CHECK(map.Get(value) == value * 10);
}

TEST_CASE("FlatHashMap grows before exceeding the maximum load", "[Containers][FlatHashMap]")
{
    FlatHashMap<int, int> map;
    for (int value = 0; value < 12; ++value)
        map.Insert(value, value);
    CHECK(map.Capacity() == 16U);

    map.Insert(12, 12);
    CHECK(map.Capacity() == 32U);
}

TEST_CASE("FlatHashMap supports move-only values across rehash", "[Containers][FlatHashMap]")
{
    FlatHashMap<int, std::unique_ptr<int>> map;
    for (int value = 0; value < 40; ++value)
        map.Insert(value, std::make_unique<int>(value));

    CHECK(map.Size() == 40U);
    CHECK(*map.GetRef(39) == 39);
}

TEST_CASE("FlatHashMap propagates hash and equality exceptions", "[Containers][FlatHashMap]")
{
    NGIN::Tests::FailureCountdown                      hashFailures;
    NGIN::Tests::FailureCountdown                      equalFailures;
    FlatHashMap<int, int, ThrowingHash, ThrowingEqual> map(
            16, ThrowingHash {&hashFailures}, ThrowingEqual {&equalFailures});
    map.Insert(1, 10);

    hashFailures.Arm(0);
    CHECK_THROWS_AS(map.GetPtr(1), std::runtime_error);
    hashFailures.Disable();

    equalFailures.Arm(0);
    CHECK_THROWS_AS(map.GetPtr(1), std::runtime_error);
    equalFailures.Disable();
    CHECK(map.Get(1) == 10);
}

TEST_CASE("FlatHashMap rejects unrepresentable reserve requests", "[Containers][FlatHashMap]")
{
    FlatHashMap<int, int> map;
    CHECK_THROWS_AS(map.Reserve((std::numeric_limits<std::size_t>::max)()), std::length_error);
    CHECK(map.Size() == 0U);
    CHECK(map.Capacity() == 16U);
}
