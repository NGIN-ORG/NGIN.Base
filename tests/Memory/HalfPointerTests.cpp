/// @file HalfPointerTests.cpp
/// @brief Tests for HalfPointer utilities.

#include <NGIN/Memory/HalfPointer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>

TEST_CASE("HalfPointer converts to/from absolute pointers", "[Memory][HalfPointer]")
{
    std::byte buffer[256] {};
    void*     base = static_cast<void*>(buffer);
    void*     ptr  = static_cast<void*>(buffer + 123);

    NGIN::Memory::HalfPointer hp(base, ptr);
    auto*                     back = hp.ToAbsolute(buffer);
    CHECK(static_cast<void*>(back) == ptr);
}

TEST_CASE("HalfPointer default-constructs to invalid", "[Memory][HalfPointer]")
{
    std::byte buffer[8] {};
    NGIN::Memory::HalfPointer hp;
    CHECK(hp.ToAbsolute(buffer) == nullptr);
}

