#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Serialization/Core/InputCursor.hpp>
#include <NGIN/Serialization/Core/ParseResources.hpp>
#include <NGIN/Serialization/Core/SourceMap.hpp>
#include <NGIN/Serialization/JSON/JsonParser.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct FailingAllocator
    {
        NGIN::Memory::SystemAllocator system;
        NGIN::UIntSize                successfulAllocationsBeforeFailure {0};
        NGIN::UIntSize                allocations {0};

        [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment) noexcept
        {
            if (allocations++ >= successfulAllocationsBeforeFailure)
                return nullptr;
            return system.Allocate(size, alignment);
        }

        void Deallocate(void* memory, std::size_t size, std::size_t alignment) noexcept
        {
            system.Deallocate(memory, size, alignment);
        }
    };
}// namespace

TEST_CASE("serialization cursor is bounds-safe at empty and end input", "[serialization][core]")
{
    using namespace NGIN::Serialization;
    InputCursor empty {""};
    CHECK(empty.IsEof());
    CHECK(empty.Peek() == '\0');
    CHECK(empty.Peek(100) == '\0');
    empty.Advance();
    CHECK(empty.Offset() == 0);

    InputCursor cursor {"a"};
    CHECK(cursor.Peek() == 'a');
    cursor.Advance();
    CHECK(cursor.Offset() == 1);
    CHECK(cursor.IsEof());
    CHECK(cursor.CurrentPtr() == cursor.EndPtr());
    CHECK(cursor.Peek() == '\0');
    cursor.Advance();
    CHECK(cursor.Offset() == 1);
}

TEST_CASE("serialization source mapping treats CRLF as one newline", "[serialization][core]")
{
    using namespace NGIN::Serialization;
    const SourceMap map {"a\r\nb\nc", SourceId {7}};
    const auto      location = map.Locate(4);
    CHECK(location.source.value == 7);
    CHECK(location.line == 2);
    CHECK(location.column == 2);
}

TEST_CASE("injected allocator failures become diagnostics rather than termination",
          "[serialization][core][allocator]")
{
    using namespace NGIN::Serialization;

    FailingAllocator allocator;
    ParseResources   resources {
              .allocator = NGIN::Memory::PolyAllocatorRef {allocator},
    };

    auto json = JSON::Parse(
            std::string_view {R"({"value":"de\ncoded"})"},
            {},
            {},
            resources);
    REQUIRE_FALSE(json);
    CHECK(json.error().code == ParseErrorCode::OutOfMemory);

    allocator.allocations = 0;
    auto xml              = XML::Parse(
            std::string_view {R"(<root value="de&amp;coded"/>)"},
            {},
            {},
            resources);
    REQUIRE_FALSE(xml);
    CHECK(xml.error().code == ParseErrorCode::OutOfMemory);
}

TEST_CASE("document parse options preserve source identity", "[serialization][core][source]")
{
    using namespace NGIN::Serialization;
    const SourceId source {17};
    auto           json = JSON::Parse(R"({"x":1})", {.source = source});
    REQUIRE(json);
    CHECK(json.value().Root().Span().source == source);
    auto xml = XML::Parse("<root/>", {.source = source});
    REQUIRE(xml);
    CHECK(xml.value().Root().Span().source == source);
    auto syntax = XML::ParseSyntax("<root/>", {.source = source});
    REQUIRE(syntax);
    REQUIRE_FALSE(syntax.value().Tokens().empty());
    CHECK(syntax.value().Tokens().front().span.source == source);
    auto badJson = JSON::Parse("[", {.source = source});
    REQUIRE_FALSE(badJson);
    CHECK(badJson.error().span.source == source);
    auto badXml = XML::Parse("<root>", {.source = source});
    REQUIRE_FALSE(badXml);
    CHECK(badXml.error().span.source == source);
    auto badSyntax = XML::ParseSyntax("<root>", {.source = source});
    REQUIRE_FALSE(badSyntax);
    CHECK(badSyntax.error().span.source == source);
}
