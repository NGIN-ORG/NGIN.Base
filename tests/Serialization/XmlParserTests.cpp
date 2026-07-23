#include <NGIN/Serialization/XML/XmlBuilder.hpp>
#include <NGIN/Serialization/XML/XmlEventParser.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>
#include <NGIN/Serialization/XML/XmlStreamWriter.hpp>
#include <NGIN/Serialization/XML/XmlWriter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

namespace
{
    using namespace NGIN;
    using namespace NGIN::Serialization;
    namespace XML = NGIN::Serialization::XML;

    [[nodiscard]] auto Parse(std::string_view         source,
                             const XML::ParseOptions& options = {},
                             const ParseLimits&       limits  = {})
    {
        return XML::Parse(OwnedTextBuffer {source}, options, limits);
    }
}// namespace

TEST_CASE("XML parser exposes allocation-free semantic queries", "[serialization][xml]")
{
    auto parsed = Parse(R"(<root id="42"><child>A &amp; B</child><child><![CDATA[x<y]]></child></root>)");
    REQUIRE(parsed);
    const auto root = parsed.Value().Root();
    REQUIRE(root.Name() == "root");
    REQUIRE(root.Attribute("id"));
    CHECK(root.Attribute("id")->Value() == "42");
    CHECK(root.Children("child").begin() != root.Children("child").end());
    REQUIRE(root.FirstChild("child"));
    CHECK(*root.FirstChild("child")->FirstText() == "A & B");

    UIntSize count = 0;
    for (const auto child: root.Children("child"))
    {
        CHECK(child.Name() == "child");
        ++count;
    }
    CHECK(count == 2);
}

TEST_CASE("XML owning and borrowed documents make source lifetime explicit", "[serialization][xml][ownership]")
{
    auto owned = XML::Parse(OwnedTextBuffer {std::string {"<root><child/></root>"}});
    REQUIRE(owned);
    const auto    rootBeforeMove = owned.Value().Root();
    XML::Document moved          = std::move(owned.Value());
    CHECK(rootBeforeMove.FirstChild("child"));
    CHECK(moved.Root().FirstChild("child"));

    std::string  source = "<borrowed value=\"yes\"/>";
    ParseScratch scratch;
    auto         borrowed = XML::ParseBorrowed(BorrowedTextView {source}, scratch);
    REQUIRE(borrowed);
    CHECK(borrowed.Value().SourceText().data() == source.data());
}

TEST_CASE("XML parser enforces one root and matching tags", "[serialization][xml][well-formed]")
{
    CHECK_FALSE(Parse(""));
    CHECK_FALSE(Parse("<a/><b/>"));
    CHECK_FALSE(Parse("<a></b>"));
    CHECK_FALSE(Parse("text<a/>"));
    CHECK_FALSE(Parse("<a/>text"));
    CHECK(Parse(" \n<!--before--><a/><?after ok?> "));
}

TEST_CASE("XML parser rejects duplicate attributes and malformed lexical constructs", "[serialization][xml][well-formed]")
{
    auto duplicate = Parse(R"(<a x="1" x="2"/>)");
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.Error().code == ParseErrorCode::DuplicateName);
    CHECK(duplicate.Error().related.has_value());

    CHECK_FALSE(Parse(R"(<a x=unquoted/>)"));
    CHECK_FALSE(Parse(R"(<a x="unterminated/>)"));
    CHECK_FALSE(Parse("<a><!-- bad -- comment --></a>"));
    CHECK_FALSE(Parse("<a>bad ]]> text</a>"));
    CHECK_FALSE(Parse("<1bad/>"));
}

TEST_CASE("XML semantic trivia policy explicitly controls comments and processing instructions",
          "[serialization][xml][options]")
{
    auto discarded = Parse("<root><!--note--><?tool ok?></root>");
    REQUIRE(discarded);
    CHECK(discarded.Value().Root().Children().Empty());

    XML::ParseOptions options;
    options.trivia = XML::TriviaPolicy::Preserve;
    auto preserved = Parse("<root><!--note--><?tool ok?></root>", options);
    REQUIRE(preserved);
    REQUIRE(preserved.Value().Root().Children().Size() == 2);
    CHECK(preserved.Value().Root().Children()[0].Kind() == XML::NodeKind::Comment);
    CHECK(preserved.Value().Root().Children()[1].Kind() ==
          XML::NodeKind::ProcessingInstruction);
}

TEST_CASE("XML entities are always interpreted and invalid references rejected", "[serialization][xml][entities]")
{
    auto parsed = Parse(R"(<a value="&quot;&#x1F600;&quot;">&lt;&#65;&amp;</a>)");
    REQUIRE(parsed);
    CHECK(parsed.Value().Root().Attribute("value")->Value() ==
          std::string_view("\"\xF0\x9F\x98\x80\"", 6));
    CHECK(*parsed.Value().Root().FirstText() == "<A&");

    CHECK_FALSE(Parse("<a>&unknown;</a>"));
    CHECK_FALSE(Parse("<a>&#0;</a>"));
    CHECK_FALSE(Parse("<a>&#xD800;</a>"));
    CHECK_FALSE(Parse("<a>&unterminated</a>"));
}

TEST_CASE("XML profile rejects DOCTYPE and external identifiers by default", "[serialization][xml][security]")
{
    CHECK_FALSE(Parse("<!DOCTYPE root><root/>"));

    XML::ParseOptions options;
    options.doctype = XML::DoctypePolicy::AllowWithoutExternalEntities;
    CHECK(Parse("<!DOCTYPE root><root/>", options));
    CHECK_FALSE(Parse(R"(<!DOCTYPE root SYSTEM "file:///secret"><root/>)", options));
}

TEST_CASE("XML parser applies depth and resource limits", "[serialization][xml][limits]")
{
    ParseLimits limits;
    limits.maxDepth = 2;
    auto parsed     = Parse("<a><b><c/></b></a>", {}, limits);
    REQUIRE_FALSE(parsed);
    CHECK(parsed.Error().code == ParseErrorCode::DepthExceeded);

    limits            = {};
    limits.maxMembers = 1;
    CHECK_FALSE(Parse(R"(<a x="1" y="2"/>)", {}, limits));
}

TEST_CASE("XML syntax documents preserve comments and formatting byte-for-byte", "[serialization][xml][syntax]")
{
    const std::string source =
            "<?xml version=\"1.0\"?>\r\n"
            "<!-- heading -->\r\n"
            "<root x='1'>\r\n  <child />\r\n</root>\r\n";
    auto syntax = XML::ParseSyntax(OwnedTextBuffer {source});
    REQUIRE(syntax);
    REQUIRE(syntax.Value().Tokens().size() >= 6);
    auto written = XML::Writer::Write(syntax.Value());
    REQUIRE(written);
    CHECK(written.Value() == source);
}

TEST_CASE("XML builder and semantic writer escape and round-trip content", "[serialization][xml][writer]")
{
    XML::Builder builder;
    auto         text = builder.Text("A < B & C");
    REQUIRE(text);
    const std::array attributes {XML::Attribute {"quote", "\"yes\" & more"}};
    const std::array children {text.Value()};
    auto             root = builder.Element("root", attributes, children);
    REQUIRE(root);
    auto document = builder.Finish(root.Value());
    REQUIRE(document);

    auto written = XML::Writer::Write(document.Value());
    REQUIRE(written);
    CHECK(written.Value() == R"(<root quote="&quot;yes&quot; &amp; more">A &lt; B &amp; C</root>)");
    auto reparsed = Parse(written.Value());
    REQUIRE(reparsed);
    CHECK(*reparsed.Value().Root().FirstText() == "A < B & C");
}

TEST_CASE("XML builder rejects invalid profile characters", "[serialization][xml][builder]")
{
    XML::Builder builder;
    CHECK_FALSE(builder.Text(std::string_view {"\x01", 1}));
    CHECK_FALSE(builder.Comment("bad--comment"));
    CHECK_FALSE(builder.ProcessingInstruction("XmL", " forbidden"));
}

TEST_CASE("XML stream writer validates structure and escapes profile content",
          "[serialization][xml][writer][stream]")
{
    std::string       output;
    XML::WriteOptions options;
    options.includeDeclaration = true;
    XML::StreamWriter writer {MakeTextSink(output), options};
    REQUIRE(writer.BeginElement("Project"));
    REQUIRE(writer.Attribute("Name", "A&B"));
    REQUIRE(writer.BeginElement("Description"));
    REQUIRE(writer.Text("x < y"));
    REQUIRE(writer.EndElement());
    REQUIRE(writer.Comment("safe"));
    REQUIRE(writer.EndElement());
    REQUIRE(writer.Finish());
    CHECK(output ==
          R"(<?xml version="1.0" encoding="UTF-8"?><Project Name="A&amp;B"><Description>x &lt; y</Description><!--safe--></Project>)");
    REQUIRE(Parse(output));

    output.clear();
    writer.Reset(MakeTextSink(output));
    REQUIRE(writer.BeginElement("root"));
    REQUIRE(writer.Attribute("x", "1"));
    CHECK_FALSE(writer.Attribute("x", "2"));
}

TEST_CASE("XML contiguous event parser delivers decoded semantic events",
          "[serialization][xml][events]")
{
    std::vector<XML::EventKind> kinds;
    std::string                 text;
    auto                        handler = [&](const XML::Event& event) {
        kinds.push_back(event.kind);
        if (event.kind == XML::EventKind::Text)
            text.assign(event.value);
        return XML::EventAction::Continue();
    };
    ParseScratch scratch;
    auto         result = XML::EventParser::ParseContiguous(
            BorrowedTextView {"<root x=\"1\">A&amp;B</root>"}, handler, scratch);
    REQUIRE(result);
    REQUIRE(kinds.size() == 4);
    CHECK(kinds[0] == XML::EventKind::StartElement);
    CHECK(kinds[1] == XML::EventKind::Attribute);
    CHECK(kinds[2] == XML::EventKind::Text);
    CHECK(kinds[3] == XML::EventKind::EndElement);
    CHECK(text == "A&B");
}

TEST_CASE("XML memory accounting includes finalized element views",
          "[serialization][xml][memory][limits]")
{
    auto baseline = Parse("<root><item/><item/><nested><value/></nested></root>");
    REQUIRE(baseline);
    REQUIRE(baseline.Value().MemoryCommitted() > 0);

    ParseLimits limits;
    limits.maxTotalMemoryBytes = baseline.Value().MemoryCommitted() - 1;
    auto limited               = XML::Parse(
            OwnedTextBuffer {"<root><item/><item/><nested><value/></nested></root>"},
            {},
            limits);
    REQUIRE_FALSE(limited);
    CHECK(limited.Error().code == ParseErrorCode::LimitExceeded);
}
