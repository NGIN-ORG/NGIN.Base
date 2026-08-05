/// @file Exception.cpp
/// @brief Tests for the NGIN exception base and unsupported-operation error.

#include <NGIN/Exceptions/Exception.hpp>
#include <NGIN/Exceptions/NotSupportedException.hpp>

#include <catch2/catch_test_macros.hpp>
#include <string_view>

namespace NGIN::Exceptions
{
    TEST_CASE("Exception preserves C string messages", "[Exceptions]")
    {
        const Exception exception {"failure"};
        CHECK(std::string_view {exception.GetMessage()} == "failure");
    }

    TEST_CASE("NotSupportedException supports empty and NGIN string messages", "[Exceptions]")
    {
        const NotSupportedException empty;
        CHECK(std::string_view {empty.GetMessage()}.empty());

        const NGIN::Text::String    message {"unsupported operation"};
        const NotSupportedException copied {message};
        CHECK(std::string_view {copied.GetMessage()} == message.View());

        NotSupportedException moved {NGIN::Text::String {"temporary message"}};
        CHECK(std::string_view {moved.GetMessage()} == "temporary message");
    }
}// namespace NGIN::Exceptions
