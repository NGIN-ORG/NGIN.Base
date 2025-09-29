#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NGIN/Utilities/Any.hpp>

#include <NGIN/Memory/SystemAllocator.hpp>

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
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

    struct MoveOnly
    {
        std::unique_ptr<int> value;

        explicit MoveOnly(int v)
            : value(std::make_unique<int>(v))
        {
        }

        MoveOnly(MoveOnly&&) noexcept            = default;
        MoveOnly& operator=(MoveOnly&&) noexcept = default;

        MoveOnly(const MoveOnly&)            = delete;
        MoveOnly& operator=(const MoveOnly&) = delete;
    };

    struct NonTrivial
    {
        int               marker;
        static inline int instances    = 0;
        static inline int destructions = 0;

        explicit NonTrivial(int m)
            : marker(m)
        {
            ++instances;
        }

        NonTrivial(const NonTrivial& other)
            : marker(other.marker)
        {
            ++instances;
        }

        NonTrivial(NonTrivial&& other) noexcept
            : marker(other.marker)
        {
            other.marker = -1;
            ++instances;
        }

        ~NonTrivial()
        {
            ++destructions;
        }
    };
}// namespace

TEST_CASE("Any stores small types inline")
{
    NGIN::Utilities::Any<> any;
    any.Emplace<int>(42);

    REQUIRE(any.HasValue());
    REQUIRE(any.IsInline());
    REQUIRE(any.Is<int>());
    REQUIRE(any.Cast<int>() == 42);
    REQUIRE(any.TryCast<float>() == nullptr);
}

TEST_CASE("Any reports metadata and supports Reset")
{
    NGIN::Utilities::Any<> any;

    auto& str = any.Emplace<std::string>("hello world");
    REQUIRE(str == "hello world");
    REQUIRE(any.HasValue());
    REQUIRE(any.Size() == sizeof(std::string));
    REQUIRE(any.Alignment() == alignof(std::string));
    REQUIRE(any.Is<std::string>());
    REQUIRE(any.GetTypeId() != NGIN::Utilities::Any<>::VOID_TYPE_ID);

    any.Reset();
    REQUIRE_FALSE(any.HasValue());

    auto voidAny = NGIN::Utilities::Any<>::MakeVoid();
    REQUIRE_FALSE(voidAny.HasValue());
    REQUIRE(voidAny.GetTypeId() == NGIN::Utilities::Any<>::VOID_TYPE_ID);
    REQUIRE_THROWS_AS(voidAny.Visit([](auto) {}), std::logic_error);
}

TEST_CASE("Any uses heap for large allocations")
{
    CountingAllocator alloc;
    using Large = std::array<std::uint64_t, 8>;// 64 bytes

    NGIN::Utilities::Any<32, CountingAllocator> any(alloc);
    auto&                                       stored = any.GetAllocator();
    any.Emplace<Large>(Large {1, 2, 3, 4, 5, 6, 7, 8});

    REQUIRE(any.HasValue());
    REQUIRE_FALSE(any.IsInline());
    REQUIRE(stored.allocations == 1);
    REQUIRE(any.Cast<Large>()[0] == 1);

    any.Reset();
    REQUIRE_FALSE(any.HasValue());
    REQUIRE(stored.deallocations == 1);
}

TEST_CASE("Any move semantics transfer ownership")
{
    NonTrivial::instances    = 0;
    NonTrivial::destructions = 0;

    NGIN::Utilities::Any<> original;
    original.Emplace<NonTrivial>(99);

    REQUIRE(original.HasValue());
    REQUIRE(original.Cast<NonTrivial>().marker == 99);

    NGIN::Utilities::Any<> moved(std::move(original));
    REQUIRE_FALSE(original.HasValue());
    REQUIRE(moved.HasValue());
    REQUIRE(moved.Cast<NonTrivial>().marker == 99);

    moved.Reset();
    REQUIRE(NonTrivial::destructions == NonTrivial::instances);
}

TEST_CASE("Any copy constructor duplicates copyable types")
{
    NGIN::Utilities::Any<> any;
    any.Emplace<std::string>("hello");

    NGIN::Utilities::Any<> copy(any);

    REQUIRE(copy.HasValue());
    REQUIRE(copy.Is<std::string>());
    REQUIRE(copy.Cast<std::string>() == "hello");
    REQUIRE(any.Cast<std::string>() == "hello");
}

TEST_CASE("Any Cast throws on mismatch")
{
    NGIN::Utilities::Any<> any;
    any.Emplace<int>(123);

    REQUIRE_THROWS_AS(any.Cast<float>(), std::bad_any_cast);
    REQUIRE_THROWS_AS(any.MakeView().Cast<float>(), std::bad_any_cast);
}

TEST_CASE("Any copy throws for move-only content")
{
    NGIN::Utilities::Any<> any;
    any.Emplace<MoveOnly>(7);

    REQUIRE_THROWS_AS([&]() { auto attempt = NGIN::Utilities::Any<>(any); }(), std::bad_any_cast);
    REQUIRE(any.HasValue());
    REQUIRE(*any.Cast<MoveOnly>().value == 7);
}

TEST_CASE("Any Visit exposes view helper")
{
    NGIN::Utilities::Any<> any;
    any.Emplace<int>(10);

    auto doubled = any.Visit([](NGIN::Utilities::Any<>::View view) {
        auto& ref = view.Cast<int>();
        ref *= 2;
        return ref;
    });

    REQUIRE(doubled == 20);
    REQUIRE(any.Cast<int>() == 20);

    const auto& constAny = any;
    auto        value    = constAny.Visit([](NGIN::Utilities::Any<>::ConstView view) {
        return view.Cast<int>();
    });
    REQUIRE(value == 20);
}

TEST_CASE("Any TryCast returns nullptr on mismatched types")
{
    NGIN::Utilities::Any<> any;
    any.Emplace<float>(3.5f);

    REQUIRE(any.TryCast<int>() == nullptr);
    auto* ptr = any.TryCast<float>();
    REQUIRE(ptr != nullptr);
    REQUIRE(*ptr == Catch::Approx(3.5f));

    const auto& constAny  = any;
    const float* constPtr = constAny.TryCast<float>();
    REQUIRE(constPtr != nullptr);
}

TEST_CASE("Any MakeView reflects live object")
{
    NGIN::Utilities::Any<> any;
    any.Emplace<std::vector<int>>(std::vector<int> {1, 2, 3});

    auto view = any.MakeView();
    auto& vec = view.Cast<std::vector<int>>();
    vec.push_back(4);

    REQUIRE(any.Cast<std::vector<int>>().size() == 4);

    const auto& constAny = any;
    const auto  constView = constAny.MakeView();
    REQUIRE(constView.Cast<std::vector<int>>().front() == 1);
}
