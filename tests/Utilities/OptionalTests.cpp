/// @file OptionalTests.cpp
/// @brief Tests for NGIN::Utilities::Optional.

#include <NGIN/Utilities/Optional.hpp>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <utility>

namespace
{
    struct CountingType
    {
        inline static int s_defaultCtorCount = 0;
        inline static int s_copyCtorCount = 0;
        inline static int s_moveCtorCount = 0;
        inline static int s_dtorCount = 0;

        CountingType() { ++s_defaultCtorCount; }
        CountingType(const CountingType& other)
            : value {other.value}
        {
            ++s_copyCtorCount;
        }
        CountingType(CountingType&& other) noexcept
            : value {other.value}
        {
            ++s_moveCtorCount;
        }
        CountingType& operator=(const CountingType&) = default;
        CountingType& operator=(CountingType&&) noexcept = default;
        ~CountingType() { ++s_dtorCount; }

        static void ResetCounts()
        {
            s_defaultCtorCount = 0;
            s_copyCtorCount = 0;
            s_moveCtorCount = 0;
            s_dtorCount = 0;
        }

        int value {0};
    };

    struct NoCopyAssign
    {
        inline static int s_dtorCount = 0;

        NoCopyAssign() = default;
        explicit NoCopyAssign(int v)
            : value {v}
        {
        }

        NoCopyAssign(const NoCopyAssign&) = default;
        NoCopyAssign(NoCopyAssign&&) noexcept = default;

        NoCopyAssign& operator=(const NoCopyAssign&) = delete;
        NoCopyAssign& operator=(NoCopyAssign&&) noexcept = default;

        ~NoCopyAssign() { ++s_dtorCount; }

        int value {0};
    };

    struct NoMoveAssign
    {
        inline static int s_dtorCount = 0;

        NoMoveAssign() = default;
        explicit NoMoveAssign(int v)
            : value {v}
        {
        }

        NoMoveAssign(const NoMoveAssign&) = default;
        NoMoveAssign(NoMoveAssign&&) noexcept = default;

        NoMoveAssign& operator=(const NoMoveAssign&) = default;
        NoMoveAssign& operator=(NoMoveAssign&&) noexcept = delete;

        ~NoMoveAssign() { ++s_dtorCount; }

        int value {0};
    };

    struct MoveOnly
    {
        MoveOnly() = default;
        explicit MoveOnly(int v)
            : value {v}
        {
        }

        MoveOnly(const MoveOnly&) = delete;
        MoveOnly& operator=(const MoveOnly&) = delete;

        MoveOnly(MoveOnly&&) noexcept = default;
        MoveOnly& operator=(MoveOnly&&) noexcept = default;

        int value {0};
    };

    static_assert(std::is_trivially_copyable_v<int>);
}

TEST_CASE("Optional default constructs empty", "[Utilities][Optional]")
{
    NGIN::Utilities::Optional<int> opt;
    CHECK(!opt.HasValue());
    CHECK(static_cast<bool>(opt) == false);
    CHECK(opt.Ptr() == nullptr);
    CHECK(opt.TryGet() == nullptr);
}

TEST_CASE("Optional emplace/value access", "[Utilities][Optional]")
{
    NGIN::Utilities::Optional<int> opt;
    opt.Emplace(123);
    REQUIRE(opt.HasValue());
    CHECK(opt.Value() == 123);
    CHECK(*opt == 123);
    CHECK(opt.Ptr() != nullptr);
    CHECK(opt.TryGet() == opt.Ptr());
    CHECK(opt.ValueOr(7) == 123);
}

TEST_CASE("Optional reset destroys when non-trivial", "[Utilities][Optional]")
{
    CountingType::ResetCounts();

    NGIN::Utilities::Optional<CountingType> opt;
    opt.Emplace();
    REQUIRE(opt.HasValue());

    opt.Reset();
    CHECK(!opt.HasValue());
    CHECK(CountingType::s_dtorCount == 1);
}

TEST_CASE("Optional copy/move preserve engaged state", "[Utilities][Optional]")
{
    CountingType::ResetCounts();

    NGIN::Utilities::Optional<CountingType> a;
    a.Emplace().value = 5;

    NGIN::Utilities::Optional<CountingType> b {a};
    REQUIRE(b.HasValue());
    CHECK(b.Value().value == 5);

    NGIN::Utilities::Optional<CountingType> c {std::move(a)};
    REQUIRE(c.HasValue());
    CHECK(c.Value().value == 5);
}

TEST_CASE("Optional triviality for trivially copyable T", "[Utilities][Optional]")
{
    using OptInt = NGIN::Utilities::Optional<int>;
    static_assert(std::is_trivially_copyable_v<OptInt>);
    static_assert(std::is_trivially_destructible_v<OptInt>);
}

TEST_CASE("Optional copy assignment reconstructs when T not copy-assignable", "[Utilities][Optional]")
{
    NoCopyAssign::s_dtorCount = 0;

    NGIN::Utilities::Optional<NoCopyAssign> a;
    a.Emplace(1);
    NGIN::Utilities::Optional<NoCopyAssign> b;
    b.Emplace(2);

    b = a;
    REQUIRE(b.HasValue());
    CHECK(b.Value().value == 1);
    CHECK(NoCopyAssign::s_dtorCount == 1);
}

TEST_CASE("Optional move assignment reconstructs when T not move-assignable", "[Utilities][Optional]")
{
    NoMoveAssign::s_dtorCount = 0;

    NGIN::Utilities::Optional<NoMoveAssign> a;
    a.Emplace(7);
    NGIN::Utilities::Optional<NoMoveAssign> b;
    b.Emplace(9);

    b = std::move(a);
    REQUIRE(b.HasValue());
    CHECK(b.Value().value == 7);
    CHECK(NoMoveAssign::s_dtorCount == 1);
}

TEST_CASE("Optional self-assignment is a no-op", "[Utilities][Optional]")
{
    NGIN::Utilities::Optional<int> opt;
    opt.Emplace(42);
    opt = opt;
    CHECK(opt.HasValue());
    CHECK(opt.Value() == 42);
}

TEST_CASE("Optional Swap handles all engagement combinations", "[Utilities][Optional]")
{
    SECTION("both empty")
    {
        NGIN::Utilities::Optional<int> a;
        NGIN::Utilities::Optional<int> b;
        a.Swap(b);
        CHECK(!a.HasValue());
        CHECK(!b.HasValue());
    }

    SECTION("both engaged")
    {
        NGIN::Utilities::Optional<int> a;
        NGIN::Utilities::Optional<int> b;
        a.Emplace(1);
        b.Emplace(2);
        a.Swap(b);
        REQUIRE(a.HasValue());
        REQUIRE(b.HasValue());
        CHECK(a.Value() == 2);
        CHECK(b.Value() == 1);
    }

    SECTION("left engaged")
    {
        NGIN::Utilities::Optional<int> a;
        NGIN::Utilities::Optional<int> b;
        a.Emplace(5);
        a.Swap(b);
        CHECK(!a.HasValue());
        REQUIRE(b.HasValue());
        CHECK(b.Value() == 5);
    }

    SECTION("right engaged")
    {
        NGIN::Utilities::Optional<int> a;
        NGIN::Utilities::Optional<int> b;
        b.Emplace(6);
        a.Swap(b);
        REQUIRE(a.HasValue());
        CHECK(a.Value() == 6);
        CHECK(!b.HasValue());
    }
}

TEST_CASE("Optional ValueOr on rvalue moves value", "[Utilities][Optional]")
{
    NGIN::Utilities::Optional<MoveOnly> opt;
    opt.Emplace(11);

    MoveOnly out = std::move(opt).ValueOr(MoveOnly {99});
    CHECK(out.value == 11);
}
