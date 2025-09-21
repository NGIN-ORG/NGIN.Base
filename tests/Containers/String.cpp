/// @file StringTest.cpp
/// @brief Tests for NGIN::Containers::String using Catch2.

#include <NGIN/Containers/String.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>

using NGIN::Containers::String;

namespace
{
    bool CStrEqual(const char* a, const char* b)
    {
        if (!a || !b)
        {
            return a == b;
        }
        return std::strcmp(a, b) == 0;
    }
}// namespace

TEST_CASE("String default construction", "[Containers][String]")
{
    String s;
    CHECK(s.GetSize() == 0U);
    CHECK(CStrEqual(s.CStr(), ""));
}

TEST_CASE("String handles null pointer construction", "[Containers][String]")
{
    const char* nullStr = nullptr;
    String      s(nullStr);
    CHECK(s.GetSize() == 0U);
    CHECK(CStrEqual(s.CStr(), ""));
}

TEST_CASE("String constructs from small C string", "[Containers][String]")
{
    String s("Hello");
    CHECK(s.GetSize() == 5U);
    CHECK(CStrEqual(s.CStr(), "Hello"));
}

TEST_CASE("String constructs from large C string", "[Containers][String]")
{
    std::string large(60, 'A');
    String      s(large.c_str());
    CHECK(s.GetSize() == large.size());
    CHECK(CStrEqual(s.CStr(), large.c_str()));
}

TEST_CASE("String copy constructor", "[Containers][String]")
{
    SECTION("small buffer")
    {
        String original("Small Test");
        String copy(original);
        CHECK(copy.GetSize() == original.GetSize());
        CHECK(CStrEqual(copy.CStr(), original.CStr()));
    }

    SECTION("heap storage")
    {
        std::string large(70, 'B');
        String      original(large.c_str());
        String      copy(original);
        CHECK(copy.GetSize() == original.GetSize());
        CHECK(CStrEqual(copy.CStr(), original.CStr()));
        CHECK(original.CStr() != copy.CStr());
    }
}

TEST_CASE("String move constructor", "[Containers][String]")
{
    SECTION("small buffer")
    {
        String      source("MoveSmall");
        const char* oldPointer = source.CStr();
        String      moved(std::move(source));
        CHECK(moved.GetSize() == 9U);
        CHECK(CStrEqual(moved.CStr(), "MoveSmall"));
        CHECK(oldPointer != moved.CStr());
    }

    SECTION("heap storage")
    {
        std::string large(70, 'M');
        String      source(large.c_str());
        const char* oldPointer = source.CStr();
        String      moved(std::move(source));
        CHECK(moved.GetSize() == large.size());
        CHECK(CStrEqual(moved.CStr(), large.c_str()));
        CHECK(oldPointer == moved.CStr());
    }
}

TEST_CASE("String copy assignment", "[Containers][String]")
{
    SECTION("small to small")
    {
        String a("Alpha");
        String b("Beta");
        b = a;
        CHECK(b.GetSize() == a.GetSize());
        CHECK(CStrEqual(b.CStr(), "Alpha"));
    }

    SECTION("heap to heap")
    {
        std::string largeA(80, 'A');
        std::string largeB(90, 'B');
        String      a(largeA.c_str());
        String      b(largeB.c_str());
        b = a;
        CHECK(b.GetSize() == a.GetSize());
        CHECK(CStrEqual(b.CStr(), largeA.c_str()));
        CHECK(b.CStr() != a.CStr());
    }
}

TEST_CASE("String move assignment", "[Containers][String]")
{
    SECTION("small buffer")
    {
        String      source("Hello");
        String      target("World");
        const char* oldPointer = source.CStr();
        target                 = std::move(source);
        CHECK(target.GetSize() == 5U);
        CHECK(CStrEqual(target.CStr(), "Hello"));
        CHECK(oldPointer != target.CStr());
    }

    SECTION("heap storage")
    {
        std::string large(75, 'Z');
        String      source(large.c_str());
        String      target("Small");
        const char* oldPointer = source.CStr();
        target                 = std::move(source);
        CHECK(target.GetSize() == large.size());
        CHECK(CStrEqual(target.CStr(), large.c_str()));
        CHECK(target.CStr() == oldPointer);
    }
}

TEST_CASE("String append operations", "[Containers][String]")
{
    SECTION("SBO append")
    {
        String first("Hello");
        String second("World");
        first.Append(second);
        CHECK(first.GetSize() == 10U);
        CHECK(CStrEqual(first.CStr(), "HelloWorld"));
    }

    SECTION("append triggers heap")
    {
        String      prefix("SBO start: ");
        std::string large(60, 'X');
        String      suffix(large.c_str());
        prefix.Append(suffix);
        std::string expected = std::string("SBO start: ") + large;
        CHECK(prefix.GetSize() == expected.size());
        CHECK(CStrEqual(prefix.CStr(), expected.c_str()));
    }

    SECTION("operator +=")
    {
        String value("Test");
        String suffix("++");
        value += suffix;
        CHECK(value.GetSize() == 6U);
        CHECK(CStrEqual(value.CStr(), "Test++"));
    }
}

TEST_CASE("String self assignment", "[Containers][String]")
{
    SECTION("operator=")
    {
        String value("Self");
        value = value;
        CHECK(value.GetSize() == 4U);
        CHECK(CStrEqual(value.CStr(), "Self"));
    }

    SECTION("operator+=")
    {
        String value("Repeat");
        value += value;
        CHECK(value.GetSize() == 12U);
        CHECK(CStrEqual(value.CStr(), "RepeatRepeat"));
    }
}

TEST_CASE("String clear and assignment", "[Containers][String]")
{
    String value("Clear me");
    value.clear();
    CHECK(value.GetSize() == 0U);
    CHECK(CStrEqual(value.CStr(), ""));

    value.Assign("Assigned");
    CHECK(value.GetSize() == 8U);
    CHECK(CStrEqual(value.CStr(), "Assigned"));
}
