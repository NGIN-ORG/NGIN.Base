#include <NGIN/Utilities/Expected.hpp>

#include <catch2/catch_test_macros.hpp>

#include <NGIN/Meta/TypeTraits.hpp>

#include <utility>

namespace
{
    struct MoveOnly
    {
        int value {0};

        explicit MoveOnly(int v) noexcept
            : value {v}
        {
        }

        MoveOnly(const MoveOnly&) = delete;
        MoveOnly& operator=(const MoveOnly&) = delete;

        MoveOnly(MoveOnly&& other) noexcept
            : value {other.value}
        {
            other.value = -1;
        }

        MoveOnly& operator=(MoveOnly&& other) noexcept
        {
            value = other.value;
            other.value = -1;
            return *this;
        }
    };

    struct NoCopyAssign
    {
        int value {0};

        explicit NoCopyAssign(int v) noexcept
            : value {v}
        {
        }

        NoCopyAssign(const NoCopyAssign& other) noexcept
            : value {other.value}
        {
        }

        NoCopyAssign& operator=(const NoCopyAssign&) = delete;

        NoCopyAssign(NoCopyAssign&&) = default;
        NoCopyAssign& operator=(NoCopyAssign&&) = default;
    };

    struct NoMoveAssign
    {
        int value {0};

        explicit NoMoveAssign(int v) noexcept
            : value {v}
        {
        }

        NoMoveAssign(const NoMoveAssign&) = delete;
        NoMoveAssign& operator=(const NoMoveAssign&) = delete;

        NoMoveAssign(NoMoveAssign&& other) noexcept
            : value {other.value}
        {
            other.value = -1;
        }

        NoMoveAssign& operator=(NoMoveAssign&&) = delete;
    };

    struct CountingError
    {
        inline static int s_destructCount = 0;

        int value {0};

        explicit CountingError(int v) noexcept
            : value {v}
        {
        }

        CountingError(const CountingError& other) noexcept
            : value {other.value}
        {
        }

        CountingError(CountingError&& other) noexcept
            : value {other.value}
        {
            other.value = -1;
        }

        CountingError& operator=(const CountingError&) = default;
        CountingError& operator=(CountingError&&) = default;

        ~CountingError() { ++s_destructCount; }

        static void Reset() { s_destructCount = 0; }
    };
}

TEST_CASE("Expected<T,E> basic value construction")
{
    using Expected = NGIN::Utilities::Expected<int, int>;

    Expected a {NGIN::Utilities::InPlaceType<int> {}, 42};
    REQUIRE(a.HasValue());
    REQUIRE(a.Value() == 42);

    Expected b {123};
    REQUIRE(b.HasValue());
    REQUIRE(b.Value() == 123);
}

TEST_CASE("Expected<T,E> basic error construction")
{
    using Expected = NGIN::Utilities::Expected<int, int>;

    Expected e {NGIN::Utilities::Unexpected<int> {7}};
    REQUIRE_FALSE(e.HasValue());
    REQUIRE(e.Error() == 7);

    Expected f {NGIN::Utilities::InPlaceType<int> {}, 9};
    REQUIRE(f.HasValue());
    REQUIRE(f.Value() == 9);
}

TEST_CASE("Expected<T,E> assignment reconstructs when not assignable")
{
    using Expected = NGIN::Utilities::Expected<NoCopyAssign, int>;

    Expected a {NGIN::Utilities::InPlaceType<NoCopyAssign> {}, 1};
    Expected b {NGIN::Utilities::InPlaceType<NoCopyAssign> {}, 2};

    a = b;
    REQUIRE(a.HasValue());
    REQUIRE(a.Value().value == 2);
}

TEST_CASE("Expected<T,E> move-only value")
{
    using Expected = NGIN::Utilities::Expected<MoveOnly, int>;

    Expected a {NGIN::Utilities::InPlaceType<MoveOnly> {}, 5};
    REQUIRE(a.HasValue());
    REQUIRE(a.Value().value == 5);

    Expected b {std::move(a)};
    REQUIRE(b.HasValue());
    REQUIRE(b.Value().value == 5);
}

TEST_CASE("Expected<T,E> ValueOr")
{
    using Expected = NGIN::Utilities::Expected<int, int>;

    const Expected hasValue {NGIN::Utilities::InPlaceType<int> {}, 3};
    const Expected hasError {NGIN::Utilities::Unexpected<int> {11}};

    REQUIRE(hasValue.ValueOr(99) == 3);
    REQUIRE(hasError.ValueOr(99) == 99);

    Expected rvalueHasValue {NGIN::Utilities::InPlaceType<int> {}, 4};
    REQUIRE(std::move(rvalueHasValue).ValueOr(77) == 4);

    Expected rvalueHasError {NGIN::Utilities::Unexpected<int> {12}};
    REQUIRE(std::move(rvalueHasError).ValueOr(77) == 77);
}

TEST_CASE("Expected<T,E> ErrorOr")
{
    using Expected = NGIN::Utilities::Expected<int, int>;

    const Expected hasValue {NGIN::Utilities::InPlaceType<int> {}, 3};
    const Expected hasError {NGIN::Utilities::Unexpected<int> {11}};

    REQUIRE(hasValue.ErrorOr(99) == 99);
    REQUIRE(hasError.ErrorOr(99) == 11);

    Expected rvalueHasValue {NGIN::Utilities::InPlaceType<int> {}, 4};
    REQUIRE(std::move(rvalueHasValue).ErrorOr(77) == 77);

    Expected rvalueHasError {NGIN::Utilities::Unexpected<int> {12}};
    REQUIRE(std::move(rvalueHasError).ErrorOr(77) == 12);
}

TEST_CASE("Expected<T,E> rvalue Value/Error accessors move")
{
    using ExpectedValue = NGIN::Utilities::Expected<MoveOnly, int>;
    ExpectedValue a {NGIN::Utilities::InPlaceType<MoveOnly> {}, 42};

    MoveOnly extracted = std::move(a).Value();
    REQUIRE(extracted.value == 42);
    REQUIRE(a.HasValue());
    REQUIRE(a.ValueUnsafe().value == -1);

    using ExpectedError = NGIN::Utilities::Expected<int, CountingError>;
    ExpectedError b {NGIN::Utilities::Unexpected<CountingError> {CountingError {7}}};
    CountingError extractedError = std::move(b).Error();
    REQUIRE(extractedError.value == 7);
    REQUIRE_FALSE(b.HasValue());
    REQUIRE(b.ErrorUnsafe().value == -1);
}

TEST_CASE("Expected<T,E> Swap")
{
    using Expected = NGIN::Utilities::Expected<int, int>;

    // value/value
    {
        Expected a {NGIN::Utilities::InPlaceType<int> {}, 1};
        Expected b {NGIN::Utilities::InPlaceType<int> {}, 2};
        a.Swap(b);
        REQUIRE(a.HasValue());
        REQUIRE(b.HasValue());
        REQUIRE(a.Value() == 2);
        REQUIRE(b.Value() == 1);
    }

    // error/error
    {
        Expected a {NGIN::Utilities::Unexpected<int> {10}};
        Expected b {NGIN::Utilities::Unexpected<int> {20}};
        a.Swap(b);
        REQUIRE_FALSE(a.HasValue());
        REQUIRE_FALSE(b.HasValue());
        REQUIRE(a.Error() == 20);
        REQUIRE(b.Error() == 10);
    }

    // value/error
    {
        Expected a {NGIN::Utilities::InPlaceType<int> {}, 7};
        Expected b {NGIN::Utilities::Unexpected<int> {9}};
        a.Swap(b);
        REQUIRE_FALSE(a.HasValue());
        REQUIRE(a.Error() == 9);
        REQUIRE(b.HasValue());
        REQUIRE(b.Value() == 7);
    }
}

TEST_CASE("Expected<T,E> Swap supports non-assignable payload")
{
    using Expected = NGIN::Utilities::Expected<NoMoveAssign, int>;

    Expected a {NGIN::Utilities::InPlaceType<NoMoveAssign> {}, 1};
    Expected b {NGIN::Utilities::InPlaceType<NoMoveAssign> {}, 2};

    a.Swap(b);
    REQUIRE(a.HasValue());
    REQUIRE(b.HasValue());
    REQUIRE(a.Value().value == 2);
    REQUIRE(b.Value().value == 1);
}

TEST_CASE("Expected<void,E> success and error")
{
    using Expected = NGIN::Utilities::Expected<void, int>;

    Expected ok;
    REQUIRE(ok.HasValue());

    Expected err {NGIN::Utilities::Unexpected<int> {8}};
    REQUIRE_FALSE(err.HasValue());
    REQUIRE(err.Error() == 8);
}

TEST_CASE("Expected<void,E> state transitions destroy error")
{
    using Expected = NGIN::Utilities::Expected<void, CountingError>;

    CountingError::Reset();

    {
        Expected e {NGIN::Utilities::InPlaceType<CountingError> {}, 17};
        REQUIRE_FALSE(e.HasValue());
        REQUIRE(e.Error().value == 17);

        e.EmplaceValue();
        REQUIRE(e.HasValue());
    }

    // One destruction of the previously-held error.
    REQUIRE(CountingError::s_destructCount == 1);
}

TEST_CASE("Expected<void,E> ErrorOr")
{
    using Expected = NGIN::Utilities::Expected<void, int>;

    const Expected ok;
    const Expected err {NGIN::Utilities::Unexpected<int> {5}};

    REQUIRE(ok.ErrorOr(9) == 9);
    REQUIRE(err.ErrorOr(9) == 5);
}

TEST_CASE("Expected<void,E> Swap")
{
    using Expected = NGIN::Utilities::Expected<void, int>;

    // ok/ok
    {
        Expected a;
        Expected b;
        a.Swap(b);
        REQUIRE(a.HasValue());
        REQUIRE(b.HasValue());
    }

    // err/err
    {
        Expected a {NGIN::Utilities::Unexpected<int> {1}};
        Expected b {NGIN::Utilities::Unexpected<int> {2}};
        a.Swap(b);
        REQUIRE_FALSE(a.HasValue());
        REQUIRE_FALSE(b.HasValue());
        REQUIRE(a.Error() == 2);
        REQUIRE(b.Error() == 1);
    }

    // ok/err
    {
        Expected a;
        Expected b {NGIN::Utilities::Unexpected<int> {3}};
        a.Swap(b);
        REQUIRE_FALSE(a.HasValue());
        REQUIRE(a.Error() == 3);
        REQUIRE(b.HasValue());
    }
}

static_assert(NGIN::Meta::TypeTraits<NGIN::Utilities::Expected<int, int>>::IsTriviallyCopyable());
