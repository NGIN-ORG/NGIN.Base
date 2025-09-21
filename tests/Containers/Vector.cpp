/// @file VectorTest.cpp
/// @brief Tests for NGIN::Containers::Vector using Catch2.

#include <NGIN/Containers/Vector.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>

using NGIN::Containers::Vector;

namespace
{
    struct NonPod
    {
        int   data;
        char* buffer;

        explicit NonPod(int value = 0)
            : data(value), buffer(new char[4] {'T', 'e', 's', 't'}) {}

        NonPod(const NonPod& other)
            : data(other.data), buffer(new char[4])
        {
            std::copy(other.buffer, other.buffer + 4, buffer);
        }

        NonPod(NonPod&& other) noexcept
            : data(other.data), buffer(other.buffer)
        {
            other.buffer = nullptr;
        }

        ~NonPod() { delete[] buffer; }

        NonPod& operator=(const NonPod& other)
        {
            if (this == &other)
            {
                return *this;
            }
            data = other.data;
            delete[] buffer;
            buffer = new char[4];
            std::copy(other.buffer, other.buffer + 4, buffer);
            return *this;
        }

        NonPod& operator=(NonPod&& other) noexcept
        {
            if (this == &other)
            {
                return *this;
            }
            data = other.data;
            delete[] buffer;
            buffer       = other.buffer;
            other.buffer = nullptr;
            return *this;
        }
    };
}// namespace

TEST_CASE("Vector default construction", "[Containers][Vector]")
{
    Vector<int> vec;
    CHECK(vec.Size() == 0U);
    CHECK(vec.Capacity() == 0U);
}

TEST_CASE("Vector reserves capacity at construction", "[Containers][Vector]")
{
    Vector<int> vec(10);
    CHECK(vec.Size() == 0U);
    CHECK(vec.Capacity() >= 10U);
}

TEST_CASE("Vector push back", "[Containers][Vector]")
{
    Vector<int> vec;
    vec.PushBack(1);
    vec.PushBack(2);
    vec.PushBack(3);
    REQUIRE(vec.Size() == 3U);
    CHECK(vec[0] == 1);
    CHECK(vec[1] == 2);
    CHECK(vec[2] == 3);
}

TEST_CASE("Vector grows capacity automatically", "[Containers][Vector]")
{
    Vector<int> vec(2);
    vec.PushBack(10);
    vec.PushBack(20);
    vec.PushBack(30);
    CHECK(vec.Size() == 3U);
    CHECK(vec.Capacity() >= 3U);
    CHECK(vec[2] == 30);
}

TEST_CASE("Vector insert at index", "[Containers][Vector]")
{
    Vector<int> vec;
    vec.PushBack(1);
    vec.PushBack(2);
    vec.PushBack(4);
    vec.PushAt(2, 3);
    CHECK(vec.Size() == 4U);
    CHECK(vec[2] == 3);
    CHECK(vec[3] == 4);
}

TEST_CASE("Vector insert out of range throws", "[Containers][Vector]")
{
    Vector<int> vec;
    vec.PushBack(1);
    vec.PushBack(2);
    CHECK_THROWS_AS(vec.PushAt(3, 999), std::out_of_range);
}

TEST_CASE("Vector emplace operations", "[Containers][Vector]")
{
    SECTION("EmplaceAt")
    {
        Vector<std::string> vec;
        vec.PushBack("A");
        vec.PushBack("B");
        vec.PushBack("D");
        vec.EmplaceAt(2, "C");
        CHECK(vec.Size() == 4U);
        CHECK(vec[2] == "C");
    }

    SECTION("EmplaceBack")
    {
        Vector<std::string> vec;
        vec.EmplaceBack("Hello");
        vec.EmplaceBack("World");
        CHECK(vec.Size() == 2U);
        CHECK(vec[1] == "World");
    }
}

TEST_CASE("Vector pop back", "[Containers][Vector]")
{
    Vector<int> vec;
    vec.PushBack(10);
    vec.PushBack(20);
    vec.PushBack(30);
    vec.PopBack();
    CHECK(vec.Size() == 2U);
    CHECK(vec[1] == 20);
    vec.PopBack();
    vec.PopBack();
    CHECK_THROWS_AS(vec.PopBack(), std::out_of_range);
}

TEST_CASE("Vector erase at index", "[Containers][Vector]")
{
    Vector<int> vec;
    vec.PushBack(5);
    vec.PushBack(10);
    vec.PushBack(15);
    vec.Erase(1);
    CHECK(vec.Size() == 2U);
    CHECK(vec[0] == 5);
    CHECK(vec[1] == 15);
    CHECK_THROWS_AS(vec.Erase(5), std::out_of_range);
}

TEST_CASE("Vector clear resets size", "[Containers][Vector]")
{
    Vector<int> vec;
    vec.PushBack(1);
    vec.PushBack(2);
    vec.Clear();
    CHECK(vec.Size() == 0U);
    CHECK(vec.Capacity() >= 2U);
}

TEST_CASE("Vector copy semantics", "[Containers][Vector]")
{
    Vector<int> original;
    original.PushBack(7);
    original.PushBack(8);
    Vector<int> copy(original);
    CHECK(copy.Size() == original.Size());
    CHECK(copy[0] == 7);
    CHECK(copy[1] == 8);

    Vector<int> assigned;
    assigned = original;
    CHECK(assigned.Size() == original.Size());
    CHECK(assigned[1] == 8);
}

TEST_CASE("Vector move semantics", "[Containers][Vector]")
{
    Vector<int> source;
    source.PushBack(42);
    Vector<int> moved(std::move(source));
    CHECK(moved.Size() == 1U);
    CHECK(moved[0] == 42);

    Vector<int> target;
    target = std::move(moved);
    CHECK(target.Size() == 1U);
    CHECK(target[0] == 42);
}

TEST_CASE("Vector handles non-POD types", "[Containers][Vector]")
{
    Vector<NonPod> vec;
    vec.EmplaceBack(5);
    vec.EmplaceBack(10);
    CHECK(vec.Size() == 2U);
    CHECK(vec[0].data == 5);

    Vector<NonPod> copy(vec);
    CHECK(copy.Size() == vec.Size());
    CHECK(copy[1].data == 10);

    Vector<NonPod> moved(std::move(vec));
    CHECK(moved.Size() == 2U);
}
