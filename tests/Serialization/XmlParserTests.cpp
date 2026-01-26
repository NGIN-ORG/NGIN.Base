#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("XmlParser parses basic document", "[serialization][xml]")
{
    using namespace NGIN::Serialization;

    const char* input  = R"(<root id="42"><child>Value</child><child attr="x"/></root>)";
    auto        result = XmlParser::Parse(std::string_view {input});
    REQUIRE(result.HasValue());

    const XmlElement* root = result.ValueUnsafe().Root();
    REQUIRE(root != nullptr);
    REQUIRE(root->name == "root");

    const XmlAttribute* idAttr = root->FindAttribute("id");
    REQUIRE(idAttr != nullptr);
    REQUIRE(idAttr->value == "42");

    REQUIRE(root->children.Size() == 2);
    REQUIRE(root->children[0].type == XmlNode::Type::Element);
    REQUIRE(root->children[1].type == XmlNode::Type::Element);

    const XmlElement* child = root->children[0].element;
    REQUIRE(child->name == "child");
    REQUIRE(child->children.Size() == 1);
    REQUIRE(child->children[0].type == XmlNode::Type::Text);
    REQUIRE(child->children[0].text == "Value");
}

TEST_CASE("XmlParser decodes entities", "[serialization][xml]")
{
    using namespace NGIN::Serialization;

    const char* input  = R"(<root>Tom &amp; Jerry</root>)";
    auto        result = XmlParser::Parse(std::string_view {input});
    REQUIRE(result.HasValue());

    const XmlElement* root = result.ValueUnsafe().Root();
    REQUIRE(root != nullptr);
    REQUIRE(root->children.Size() == 1);
    REQUIRE(root->children[0].type == XmlNode::Type::Text);
    REQUIRE(root->children[0].text == "Tom & Jerry");
}

TEST_CASE("XmlParser rejects mismatched tags", "[serialization][xml]")
{
    using namespace NGIN::Serialization;

    const char* input  = R"(<root><child></root>)";
    auto        result = XmlParser::Parse(std::string_view {input});
    REQUIRE_FALSE(result.HasValue());
}
