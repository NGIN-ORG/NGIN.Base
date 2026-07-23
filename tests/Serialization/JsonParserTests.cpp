#include <NGIN/Serialization/JSON/JsonBuilder.hpp>
#include <NGIN/Serialization/JSON/JsonEventParser.hpp>
#include <NGIN/Serialization/JSON/JsonParser.hpp>
#include <NGIN/Serialization/JSON/JsonStreamWriter.hpp>
#include <NGIN/Serialization/JSON/JsonWriter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace NGIN;
    using namespace NGIN::Serialization;
    namespace JSON = NGIN::Serialization::JSON;

    [[nodiscard]] auto Parse(std::string_view          source,
                             const JSON::ParseOptions& options = {},
                             const ParseLimits&        limits  = {})
    {
        return JSON::Parse(OwnedTextBuffer {source}, options, limits);
    }
}// namespace

TEST_CASE("JSON parser exposes checked immutable views", "[serialization][json]")
{
    auto result = Parse(R"({"name":"NGIN","count":3,"active":true,"tags":["a","b"],"child":{"x":1},"nothing":null})");
    REQUIRE(result);

    const auto root = result.Value().Root();
    REQUIRE(root.IsObject());
    const auto object = *root.TryObject();
    REQUIRE(*object.Find("name")->TryString() == "NGIN");
    REQUIRE(*object.Find("count")->TryInt64() == 3);
    REQUIRE(*object.Find("active")->TryBool());
    REQUIRE(object.Find("nothing")->IsNull());
    REQUIRE_FALSE(object.Find("missing").has_value());

    const auto tags = *object.Find("tags")->TryArray();
    REQUIRE(tags.Size() == 2);
    CHECK(*tags[0].TryString() == "a");
    CHECK(*tags[1].TryString() == "b");
    CHECK(*object.Find("child")->TryObject()->Find("x")->TryInt64() == 1);
}

TEST_CASE("JSON owning documents survive temporary input and document moves", "[serialization][json][ownership]")
{
    auto parsed = JSON::Parse(OwnedTextBuffer {std::string {"{\"value\":\"owned\"}"}});
    REQUIRE(parsed);
    const auto     beforeMove = parsed.Value().Root();
    JSON::Document moved      = std::move(parsed.Value());
    REQUIRE(*beforeMove.TryObject()->Find("value")->TryString() == "owned");
    REQUIRE(*moved.Root().TryObject()->Find("value")->TryString() == "owned");
}

TEST_CASE("JSON borrowed parsing is explicit", "[serialization][json][ownership]")
{
    std::string  source = R"({"value":"borrowed"})";
    ParseScratch scratch;
    auto         parsed = JSON::ParseBorrowed(BorrowedTextView {source}, scratch);
    REQUIRE(parsed);
    CHECK(parsed.Value().SourceText().data() == source.data());
    CHECK(*parsed.Value().Root().TryObject()->Find("value")->TryString() == "borrowed");

    source                  = R"({"value":"a\nb"})";
    UIntSize warmedCapacity = 0;
    {
        auto decoded = JSON::ParseBorrowed(BorrowedTextView {source}, scratch);
        REQUIRE(decoded);
        CHECK(*decoded.Value().Root().TryObject()->Find("value")->TryString() == "a\nb");
        warmedCapacity = scratch.Capacity();
    }
    auto reused = JSON::ParseBorrowed(BorrowedTextView {source}, scratch);
    REQUIRE(reused);
    CHECK(scratch.Capacity() == warmedCapacity);
}

TEST_CASE("JSON in-situ parsing is explicit and decodes within owned mutable input",
          "[serialization][json][ownership]")
{
    auto parsed = JSON::ParseInSitu(MutableTextBuffer {R"({"text":"a\nb"})"});
    REQUIRE(parsed);
    CHECK(*parsed.Value().Root().TryObject()->Find("text")->TryString() == "a\nb");
}

TEST_CASE("JSON preserves the full integer domain", "[serialization][json][number]")
{
    auto parsed = Parse(R"([-9223372036854775808,9223372036854775807,9223372036854775808,18446744073709551615,1.25])");
    REQUIRE(parsed);
    const auto values = *parsed.Value().Root().TryArray();
    CHECK(*values[0].TryInt64() == (std::numeric_limits<Int64>::min)());
    CHECK(*values[1].TryInt64() == (std::numeric_limits<Int64>::max)());
    CHECK(values[2].IsUInt64());
    CHECK(*values[2].TryUInt64() == UInt64 {9223372036854775808ULL});
    CHECK(*values[3].TryUInt64() == (std::numeric_limits<UInt64>::max)());
    CHECK(*values[4].TryDouble() == 1.25);

    CHECK_FALSE(Parse("18446744073709551616"));
    CHECK_FALSE(Parse("-9223372036854775809"));
    CHECK_FALSE(Parse("1e9999"));
}

TEST_CASE("JSON extension policies are independently enforced", "[serialization][json][options]")
{
    CHECK_FALSE(Parse(R"({"a":1,})"));
    CHECK_FALSE(Parse("// comment\n1"));

    JSON::ParseOptions options;
    options.comments       = JSON::CommentPolicy::Allow;
    options.trailingCommas = JSON::TrailingCommaPolicy::Allow;
    CHECK(Parse(R"({// comment
        "a":1,
    })",
                options));

    JSON::ParseOptions trusted;
    trusted.utf8 = JSON::Utf8Policy::AssumeValid;
    CHECK(Parse(std::string_view {"\"\xc0\x80\"", 4}, trusted));
}

TEST_CASE("JSON duplicate-key policies are deterministic", "[serialization][json][options]")
{
    CHECK_FALSE(Parse(R"({"a":1,"a":2})"));

    JSON::ParseOptions options;
    options.duplicateKeys = JSON::DuplicateKeyPolicy::KeepFirst;
    auto first            = Parse(R"({"a":1,"a":2})", options);
    REQUIRE(first);
    CHECK(*first.Value().Root().TryObject()->Find("a")->TryInt64() == 1);

    options.duplicateKeys = JSON::DuplicateKeyPolicy::KeepLast;
    auto last             = Parse(R"({"a":1,"a":2})", options);
    REQUIRE(last);
    CHECK(*last.Value().Root().TryObject()->Find("a")->TryInt64() == 2);

    options.duplicateKeys = JSON::DuplicateKeyPolicy::Preserve;
    auto preserved        = Parse(R"({"a":1,"a":2})", options);
    REQUIRE(preserved);
    CHECK(preserved.Value().Root().TryObject()->Size() == 2);
}

TEST_CASE("JSON strings validate escapes, surrogates, controls, and UTF-8", "[serialization][json][unicode]")
{
    auto pair = Parse(R"({"a":"\uD83D\uDE00"})");
    REQUIRE(pair);
    CHECK(*pair.Value().Root().TryObject()->Find("a")->TryString() ==
          std::string_view("\xF0\x9F\x98\x80", 4));

    CHECK_FALSE(Parse("\"\\uD83D\""));
    CHECK_FALSE(Parse("\"\\uDE00\""));
    CHECK_FALSE(Parse("\"\\uZZZZ\""));
    CHECK_FALSE(Parse(std::string_view {"\"\x01\"", 3}));
    CHECK_FALSE(Parse(std::string_view {"\"\xc0\x80\"", 4}));
}

TEST_CASE("JSON parse limits fail with structured diagnostics", "[serialization][json][limits]")
{
    ParseLimits limits;
    limits.maxDepth = 2;
    auto depth      = Parse("[[[]]]", {}, limits);
    REQUIRE_FALSE(depth);
    CHECK(depth.Error().code == ParseErrorCode::DepthExceeded);
    CHECK(depth.Error().location.line == 1);

    limits          = {};
    limits.maxNodes = 2;
    auto nodes      = Parse("[1,2]", {}, limits);
    REQUIRE_FALSE(nodes);
    CHECK(nodes.Error().code == ParseErrorCode::LimitExceeded);
}

TEST_CASE("JSON builder and writer round-trip exact numeric kinds", "[serialization][json][writer]")
{
    JSON::Builder builder;
    const auto    name    = builder.String("NGIN");
    const auto    maximum = builder.UInt((std::numeric_limits<UInt64>::max)());
    REQUIRE(name);
    REQUIRE(maximum);
    const std::array members {
            JSON::ObjectMember {"name", name.Value()},
            JSON::ObjectMember {"maximum", maximum.Value()},
    };
    auto root = builder.Object(members);
    REQUIRE(root);
    auto document = builder.Finish(root.Value());
    REQUIRE(document);

    auto text = JSON::Writer::WriteCanonical(document.Value().Root());
    REQUIRE(text);
    CHECK(text.Value() == R"({"maximum":18446744073709551615,"name":"NGIN"})");
    auto reparsed = Parse(text.Value());
    REQUIRE(reparsed);
    CHECK(reparsed.Value().Root().TryObject()->Find("maximum")->IsUInt64());
}

TEST_CASE("JSON builder rejects invalid UTF-8 and duplicate keys",
          "[serialization][json][builder]")
{
    JSON::Builder builder;
    CHECK_FALSE(builder.String(std::string_view {"\xc0\x80", 2}));
    auto value = builder.Null();
    REQUIRE(value);
    const std::array members {
            JSON::ObjectMember {"same", value.Value()},
            JSON::ObjectMember {"same", value.Value()},
    };
    CHECK_FALSE(builder.Object(members));
}

TEST_CASE("JSON writer escapes every control-character class", "[serialization][json][writer]")
{
    JSON::Builder     builder;
    const std::string value {"\"\x01\b\f\n\r\t\\", 8};
    auto              node = builder.String(value);
    REQUIRE(node);
    auto document = builder.Finish(node.Value());
    REQUIRE(document);
    auto text = JSON::Writer::Write(document.Value());
    REQUIRE(text);
    CHECK(text.Value() == "\"\\\"\\u0001\\b\\f\\n\\r\\t\\\\\"");
    auto reparsed = Parse(text.Value());
    REQUIRE(reparsed);
    CHECK(*reparsed.Value().Root().TryString() == value);
}

TEST_CASE("JSON stream writer enforces structure and reuses retained state",
          "[serialization][json][writer][stream]")
{
    std::string        output;
    JSON::StreamWriter writer {MakeTextSink(output)};
    REQUIRE(writer.BeginObject());
    REQUIRE(writer.Key("sequence"));
    REQUIRE(writer.Int64((std::numeric_limits<Int64>::min)()));
    REQUIRE(writer.Key("values"));
    REQUIRE(writer.BeginArray());
    REQUIRE(writer.String("a\nb"));
    REQUIRE(writer.Bool(true));
    REQUIRE(writer.EndArray());
    REQUIRE(writer.EndObject());
    REQUIRE(writer.Finish());
    CHECK(output == R"({"sequence":-9223372036854775808,"values":["a\nb",true]})");
    REQUIRE(Parse(output));

    output.clear();
    writer.Reset(MakeTextSink(output));
    REQUIRE(writer.String("next"));
    REQUIRE(writer.Finish());
    CHECK(output == R"("next")");

    output.clear();
    writer.Reset(MakeTextSink(output));
    REQUIRE(writer.BeginObject());
    CHECK_FALSE(writer.String("missing-key"));
}

TEST_CASE("JSON contiguous event parser preserves exact numeric categories and handler context",
          "[serialization][json][events]")
{
    std::vector<JSON::EventKind> kinds;
    UInt64                       unsignedValue = 0;
    auto                         handler       = [&](const JSON::Event& event) {
        kinds.push_back(event.kind);
        if (event.kind == JSON::EventKind::UInt64)
            unsignedValue = event.uintValue;
        if (event.kind == JSON::EventKind::Key && event.text == "stop")
            return JSON::EventAction::Stop(77);
        return JSON::EventAction::Continue();
    };
    ParseScratch scratch;
    auto         stopped = JSON::EventParser::ParseContiguous(
            BorrowedTextView {R"({"maximum":18446744073709551615,"stop":null})"},
            handler,
            scratch);
    REQUIRE_FALSE(stopped);
    CHECK(stopped.Error().code == ParseErrorCode::HandlerRejected);
    CHECK(stopped.Error().consumerContext == 77);
    CHECK(unsignedValue == (std::numeric_limits<UInt64>::max)());
    CHECK(kinds.front() == JSON::EventKind::StartObject);
}

TEST_CASE("JSON direct event parser preserves order decoded text policies and token spans",
          "[serialization][json][events]")
{
    struct CapturedEvent
    {
        JSON::EventKind kind;
        std::string     text;
        SourceSpan      span;
    };

    std::vector<CapturedEvent> events;
    auto                       handler = [&](const JSON::Event& event) {
        events.push_back({event.kind, std::string {event.text}, event.span});
        return JSON::EventAction::Continue();
    };

    ParseScratch      scratch;
    const std::string source = R"({"line":"a\nb","values":[null,true,-2,18446744073709551615]})";
    auto              result = JSON::EventParser::ParseContiguous(
            BorrowedTextView {source, SourceId {9}}, handler, scratch);
    REQUIRE(result);
    REQUIRE(events.size() == 11);
    CHECK(events[0].kind == JSON::EventKind::StartObject);
    CHECK((events[0].span == SourceSpan {SourceId {9}, 0, 1}));
    CHECK(events[1].kind == JSON::EventKind::Key);
    CHECK(events[1].text == "line");
    CHECK(events[2].kind == JSON::EventKind::String);
    CHECK(events[2].text == "a\nb");
    CHECK(events[4].kind == JSON::EventKind::StartArray);
    CHECK(events[5].kind == JSON::EventKind::Null);
    CHECK(events[6].kind == JSON::EventKind::Bool);
    CHECK(events[7].kind == JSON::EventKind::Int64);
    CHECK(events[8].kind == JSON::EventKind::UInt64);
    CHECK(events[9].kind == JSON::EventKind::EndArray);
    CHECK(events[10].kind == JSON::EventKind::EndObject);

    JSON::ParseOptions comments;
    comments.comments       = JSON::CommentPolicy::Allow;
    comments.trailingCommas = JSON::TrailingCommaPolicy::Allow;
    events.clear();
    result = JSON::EventParser::ParseContiguous(
            BorrowedTextView {R"([1,/* accepted */2,])"}, handler, scratch, comments);
    REQUIRE(result);
    CHECK(events.size() == 4);
}

TEST_CASE("JSON direct event parser applies duplicate and resource-limit policies",
          "[serialization][json][events][limits]")
{
    std::vector<std::string> keys;
    std::vector<Int64>       values;
    auto                     handler = [&](const JSON::Event& event) {
        if (event.kind == JSON::EventKind::Key)
            keys.emplace_back(event.text);
        if (event.kind == JSON::EventKind::Int64)
            values.push_back(event.intValue);
        return JSON::EventAction::Continue();
    };

    ParseScratch scratch;
    auto         duplicate = JSON::EventParser::ParseContiguous(
            BorrowedTextView {R"({"x":1,"x":2})"}, handler, scratch);
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.Error().code == ParseErrorCode::DuplicateName);
    REQUIRE(duplicate.Error().related);
    CHECK(duplicate.Error().related->begin == 1);
    CHECK(duplicate.Error().related->end == 4);

    JSON::ParseOptions keepFirst;
    keepFirst.duplicateKeys = JSON::DuplicateKeyPolicy::KeepFirst;
    keys.clear();
    values.clear();
    auto kept = JSON::EventParser::ParseContiguous(
            BorrowedTextView {R"({"x":1,"x":{"ignored":2},"y":3})"},
            handler,
            scratch,
            keepFirst);
    REQUIRE(kept);
    CHECK((keys == std::vector<std::string> {"x", "y"}));
    CHECK((values == std::vector<Int64> {1, 3}));

    ParseLimits limits;
    limits.maxNodes = 2;
    auto limited    = JSON::EventParser::ParseContiguous(
            BorrowedTextView {R"([1,2])"}, handler, scratch, {}, limits);
    REQUIRE_FALSE(limited);
    CHECK(limited.Error().code == ParseErrorCode::LimitExceeded);

    auto invalid = JSON::EventParser::ParseContiguous(
            BorrowedTextView {R"({"x":[1,2})"}, handler, scratch);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.Error().code != ParseErrorCode::HandlerRejected);

    auto throwingHandler = [](const JSON::Event& event) -> JSON::EventAction {
        if (event.kind == JSON::EventKind::Null)
            throw std::runtime_error {"consumer failure"};
        return JSON::EventAction::Continue();
    };
    CHECK_THROWS_AS(
            JSON::EventParser::ParseContiguous(
                    BorrowedTextView {"null"}, throwingHandler, scratch),
            std::runtime_error);
}

TEST_CASE("JSON memory accounting includes finalized value views",
          "[serialization][json][memory][limits]")
{
    auto baseline = Parse(R"({"items":[1,2,3,4],"nested":{"enabled":true}})");
    REQUIRE(baseline);
    REQUIRE(baseline.Value().MemoryCommitted() > 0);

    ParseLimits limits;
    limits.maxTotalMemoryBytes = baseline.Value().MemoryCommitted() - 1;
    auto limited               = JSON::Parse(
            OwnedTextBuffer {R"({"items":[1,2,3,4],"nested":{"enabled":true}})"},
            {},
            limits);
    REQUIRE_FALSE(limited);
    CHECK(limited.Error().code == ParseErrorCode::LimitExceeded);
}
