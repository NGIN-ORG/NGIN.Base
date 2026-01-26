#include <NGIN/Serialization/JSON/JsonParser.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("JsonParser parses basic object", "[serialization][json]")
{
    using namespace NGIN::Serialization;

    const char* input = R"({
        "name": "NGIN",
        "count": 3,
        "active": true,
        "tags": ["a", "b"],
        "child": {"x": 1},
        "nothing": null
    })";

    auto result = JsonParser::Parse(std::string_view {input});
    REQUIRE(result.HasValue());

    const JsonValue& root = result.ValueUnsafe().Root();
    REQUIRE(root.IsObject());
    const JsonObject& obj = root.AsObject();

    const JsonValue* name = obj.Find("name");
    REQUIRE(name != nullptr);
    REQUIRE(name->IsString());
    REQUIRE(name->AsString() == "NGIN");

    const JsonValue* count = obj.Find("count");
    REQUIRE(count != nullptr);
    REQUIRE(count->IsNumber());
    REQUIRE(count->AsNumber() == 3.0);

    const JsonValue* active = obj.Find("active");
    REQUIRE(active != nullptr);
    REQUIRE(active->IsBool());
    REQUIRE(active->AsBool() == true);

    const JsonValue* tags = obj.Find("tags");
    REQUIRE(tags != nullptr);
    REQUIRE(tags->IsArray());
    REQUIRE(tags->AsArray().values.Size() == 2);
    REQUIRE(tags->AsArray().values[0].AsString() == "a");
    REQUIRE(tags->AsArray().values[1].AsString() == "b");

    const JsonValue* child = obj.Find("child");
    REQUIRE(child != nullptr);
    REQUIRE(child->IsObject());
    const JsonValue* childX = child->AsObject().Find("x");
    REQUIRE(childX != nullptr);
    REQUIRE(childX->AsNumber() == 1.0);

    const JsonValue* nothing = obj.Find("nothing");
    REQUIRE(nothing != nullptr);
    REQUIRE(nothing->IsNull());
}

TEST_CASE("JsonParser rejects trailing comma", "[serialization][json]")
{
    using namespace NGIN::Serialization;

    const char* input  = R"({"a": 1,})";
    auto        result = JsonParser::Parse(std::string_view {input});
    REQUIRE_FALSE(result.HasValue());
}

TEST_CASE("JsonParser accepts comments when enabled", "[serialization][json]")
{
    using namespace NGIN::Serialization;

    const char* input = R"({
        // comment
        "a": 1
    })";

    JsonParseOptions options;
    options.allowComments = true;

    auto result = JsonParser::Parse(std::string_view {input}, options);
    REQUIRE(result.HasValue());
}

TEST_CASE("JsonParser rejects invalid unicode escape", "[serialization][json]")
{
    using namespace NGIN::Serialization;

    const char* input  = R"({"a": "\uZZZZ"})";
    auto        result = JsonParser::Parse(std::string_view {input});
    REQUIRE_FALSE(result.HasValue());
}
