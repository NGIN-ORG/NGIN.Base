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

    using XML::Parse;
}// namespace

TEST_CASE("XML parser exposes allocation-free semantic queries", "[serialization][xml]")
{
    auto parsed = Parse(R"(<root id="42"><child>A &amp; B</child><child><![CDATA[x<y]]></child></root>)");
    REQUIRE(parsed);
    const auto root = parsed.value().Root();
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

TEST_CASE("XML documents survive temporary input and document moves", "[serialization][xml][ownership]")
{
    auto owned = XML::Parse(std::string {"<root><child/></root>"});
    REQUIRE(owned);
    const auto    rootBeforeMove = owned.value().Root();
    XML::Document moved          = std::move(owned.value());
    CHECK(rootBeforeMove.FirstChild("child"));
    CHECK(moved.Root().FirstChild("child"));
}

TEST_CASE("XML string-view parsing owns source and decoded strings", "[serialization][xml][ownership]")
{
    XML::Document document;
    {
        std::string       source   = R"(<root plain="original" escaped="A&amp;B">line&#10;two</root>)";
        const std::string expected = source;
        source += "trailing bytes outside the view";
        auto parsed = XML::Parser::Parse(std::string_view {source.data(), expected.size()});
        REQUIRE(parsed);
        source.assign(source.size(), 'x');
        CHECK(parsed.value().SourceText() == expected);
        document = std::move(parsed.value());
    }
    CHECK(document.Root().Attribute("plain")->Value() == "original");
    CHECK(document.Root().Attribute("escaped")->Value() == "A&B");
    CHECK(*document.Root().FirstText() == "line\ntwo");
}

TEST_CASE("XML string-view syntax parsing owns exact source bytes", "[serialization][xml][ownership][syntax]")
{
    const std::string expected = "<?xml version='1.0'?>\r\n<root value='A&amp;B'/>\r\n";
    auto              syntax   = [&] {
        std::string source = expected + "trailing bytes outside the view";
        return XML::Parser::ParseSyntax(std::string_view {source.data(), expected.size()});
    }();
    REQUIRE(syntax);
    XML::SyntaxDocument moved  = std::move(syntax.value());
    auto                output = XML::Writer::Write(moved);
    REQUIRE(output);
    CHECK(output.value() == expected);
}

TEST_CASE("XML string-view parsing rejects empty input and enforces copy limits", "[serialization][xml][limits]")
{
    CHECK_FALSE(XML::Parse(std::string_view {}));
    CHECK_FALSE(XML::ParseSyntax(std::string_view {}));
    CHECK_FALSE(XML::Parse("<root>"));
    CHECK_FALSE(XML::ParseSyntax("<root>"));
    ParseLimits limits;
    for (bool inputLimit: {true, false})
    {
        limits = {};
        if (inputLimit)
            limits.maxInputBytes = 3;
        else
            limits.maxTotalMemoryBytes = 3;
        auto semantic = XML::Parse("<root/>", {}, limits);
        REQUIRE_FALSE(semantic);
        CHECK(semantic.error().code == ParseErrorCode::LimitExceeded);
        auto syntax = XML::ParseSyntax("<root/>", {}, limits);
        REQUIRE_FALSE(syntax);
        CHECK(syntax.error().code == ParseErrorCode::LimitExceeded);
    }
}

TEST_CASE("XML parsing decodes entities while preserving source text",
          "[serialization][xml][ownership]")
{
    auto parsed = XML::Parse(R"(<root value="A &amp; B">line&#10;two&#x21;</root>)");
    REQUIRE(parsed);

    const auto root  = parsed.value().Root();
    const auto value = root.Attribute("value");
    REQUIRE(value);
    CHECK(value->Value() == "A & B");
    REQUIRE(root.FirstText());
    CHECK(*root.FirstText() == "line\ntwo!");

    const auto source = parsed.value().SourceText();
    CHECK(source == R"(<root value="A &amp; B">line&#10;two&#x21;</root>)");
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
    CHECK(duplicate.error().code == ParseErrorCode::DuplicateName);
    CHECK(duplicate.error().related.has_value());

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
    CHECK(discarded.value().Root().Children().Empty());

    XML::ParseOptions options;
    options.trivia = XML::TriviaPolicy::Preserve;
    auto preserved = Parse("<root><!--note--><?tool ok?></root>", options);
    REQUIRE(preserved);
    REQUIRE(preserved.value().Root().Children().Size() == 2);
    auto child = preserved.value().Root().Children().begin();
    CHECK((*child).Kind() == XML::NodeKind::Comment);
    ++child;
    CHECK((*child).Kind() ==
          XML::NodeKind::ProcessingInstruction);
}

TEST_CASE("XML entities are always interpreted and invalid references rejected", "[serialization][xml][entities]")
{
    auto parsed = Parse(R"(<a value="&quot;&#x1F600;&quot;">&lt;&#65;&amp;</a>)");
    REQUIRE(parsed);
    CHECK(parsed.value().Root().Attribute("value")->Value() ==
          std::string_view("\"\xF0\x9F\x98\x80\"", 6));
    CHECK(*parsed.value().Root().FirstText() == "<A&");

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
    CHECK(parsed.error().code == ParseErrorCode::DepthExceeded);

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
    auto syntax = XML::ParseSyntax(source);
    REQUIRE(syntax);
    REQUIRE(syntax.value().Tokens().size() >= 6);
    auto written = XML::Writer::Write(syntax.value());
    REQUIRE(written);
    CHECK(written.value() == source);
}

TEST_CASE("XML builder and semantic writer escape and round-trip content", "[serialization][xml][writer]")
{
    XML::Builder builder;
    auto         text = builder.Text("A < B & C");
    REQUIRE(text);
    const std::array attributes {XML::Attribute {"quote", "\"yes\" & more"}};
    const std::array children {text.value()};
    auto             root = builder.Element("root", attributes, children);
    REQUIRE(root);
    auto document = builder.Finish(root.value());
    REQUIRE(document);

    auto written = XML::Writer::Write(document.value());
    REQUIRE(written);
    CHECK(written.value() == R"(<root quote="&quot;yes&quot; &amp; more">A &lt; B &amp; C</root>)");
    auto reparsed = Parse(written.value());
    REQUIRE(reparsed);
    CHECK(*reparsed.value().Root().FirstText() == "A < B & C");
}

TEST_CASE("XML builder rejects invalid profile characters", "[serialization][xml][builder]")
{
    XML::Builder builder;
    CHECK_FALSE(builder.Text(std::string_view {"\x01", 1}));
    CHECK_FALSE(builder.Comment("bad--comment"));
    CHECK_FALSE(builder.ProcessingInstruction("XmL", " forbidden"));
}

TEST_CASE("XML builder enforces single-parent child ownership",
          "[serialization][xml][builder][ownership]")
{
    XML::Builder builder;
    auto         child = builder.Element("child", {}, {});
    REQUIRE(child);

    const std::array duplicateChildren {child.value(), child.value()};
    CHECK_FALSE(builder.Element("duplicate", {}, duplicateChildren));

    const std::array children {child.value()};
    auto             firstParent = builder.Element("first", {}, children);
    REQUIRE(firstParent);
    CHECK_FALSE(builder.Element("second", {}, children));
    CHECK_FALSE(builder.Finish(child.value()));
    CHECK(builder.Finish(firstParent.value()));
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
            "<root x=\"1\">A&amp;B</root>", handler, scratch);
    REQUIRE(result);
    REQUIRE(kinds.size() == 4);
    CHECK(kinds[0] == XML::EventKind::StartElement);
    CHECK(kinds[1] == XML::EventKind::Attribute);
    CHECK(kinds[2] == XML::EventKind::Text);
    CHECK(kinds[3] == XML::EventKind::EndElement);
    CHECK(text == "A&B");
}

TEST_CASE("XML event parser preserves semantic order, trivia, source identity, and token spans",
          "[serialization][xml][events]")
{
    constexpr std::string_view source =
            R"(<root a="A&amp;B"><child/>text<![CDATA[x<y]]><!--note--><?pi body?></root>)";
    constexpr SourceId      sourceId {17};
    std::vector<XML::Event> events;
    std::string             decodedAttribute;
    auto                    handler = [&](const XML::Event& event) {
        events.push_back(event);
        if (event.kind == XML::EventKind::Attribute)
            decodedAttribute.assign(event.value);
        return XML::EventAction::Continue();
    };

    XML::ParseOptions options;
    options.trivia = XML::TriviaPolicy::Preserve;
    options.source = sourceId;
    ParseScratch scratch;
    auto         result = XML::EventParser::ParseContiguous(
            source, handler, scratch, options);

    REQUIRE(result);
    REQUIRE(events.size() == 9);
    CHECK(events[0].kind == XML::EventKind::StartElement);
    CHECK(events[0].name == "root");
    CHECK(source.substr(events[0].span.begin, events[0].span.Length()) == "<root");
    CHECK(events[1].kind == XML::EventKind::Attribute);
    CHECK(events[1].name == "a");
    CHECK(decodedAttribute == "A&B");
    CHECK(source.substr(events[1].span.begin, events[1].span.Length()) == R"(a="A&amp;B")");
    CHECK(events[2].kind == XML::EventKind::StartElement);
    CHECK(events[2].name == "child");
    CHECK(events[3].kind == XML::EventKind::EndElement);
    CHECK(source.substr(events[3].span.begin, events[3].span.Length()) == "/>");
    CHECK(events[4].kind == XML::EventKind::Text);
    CHECK(events[4].value == "text");
    CHECK(events[5].kind == XML::EventKind::CData);
    CHECK(events[5].value == "x<y");
    CHECK(events[6].kind == XML::EventKind::Comment);
    CHECK(events[6].value == "note");
    CHECK(events[7].kind == XML::EventKind::ProcessingInstruction);
    CHECK(events[7].name == "pi");
    CHECK(events[7].value == " body");
    CHECK(events[8].kind == XML::EventKind::EndElement);
    CHECK(source.substr(events[8].span.begin, events[8].span.Length()) == "</root>");
    for (const auto& event: events)
        CHECK(event.span.source == sourceId);
}

TEST_CASE("XML event parser reports duplicate attributes and resource limits",
          "[serialization][xml][events][limits]")
{
    auto         handler = [](const XML::Event&) { return XML::EventAction::Continue(); };
    ParseScratch scratch;

    auto duplicate = XML::EventParser::ParseContiguous(
            R"(<root x="1" x="2"/>)", handler, scratch);
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.error().code == ParseErrorCode::DuplicateName);
    CHECK(duplicate.error().related.has_value());

    ParseLimits limits;
    limits.maxDepth = 1;
    auto depth      = XML::EventParser::ParseContiguous(
            "<root><child/></root>", handler, scratch, {}, limits);
    REQUIRE_FALSE(depth);
    CHECK(depth.error().code == ParseErrorCode::DepthExceeded);

    limits          = {};
    limits.maxNodes = 1;
    auto nodes      = XML::EventParser::ParseContiguous(
            "<root><child/></root>", handler, scratch, {}, limits);
    REQUIRE_FALSE(nodes);
    CHECK(nodes.error().code == ParseErrorCode::LimitExceeded);
}

TEST_CASE("XML event parser preserves handler control flow",
          "[serialization][xml][events]")
{
    ParseScratch scratch;
    auto         stoppingHandler = [](const XML::Event&) {
        return XML::EventAction::Stop(42);
    };
    auto stopped = XML::EventParser::ParseContiguous(
            "<root/>", stoppingHandler, scratch);
    REQUIRE_FALSE(stopped);
    CHECK(stopped.error().code == ParseErrorCode::HandlerRejected);
    CHECK(stopped.error().consumerContext == 42);
    CHECK(stopped.error().span.begin == 0);
    CHECK(stopped.error().span.end == 5);
}

TEST_CASE("XML memory accounting enforces retained DOM limits",
          "[serialization][xml][memory][limits]")
{
    auto baseline = Parse("<root><item/><item/><nested><value/></nested></root>");
    REQUIRE(baseline);
    REQUIRE(baseline.value().MemoryCommitted() > 0);
    CHECK(baseline.value().MemoryUsed() <= baseline.value().MemoryCommitted());
    CHECK(baseline.value().PeakMemoryCommitted() >= baseline.value().MemoryCommitted());
    CHECK(baseline.value().AllocationCount() > 0);

    ParseLimits limits;
    limits.maxTotalMemoryBytes = baseline.value().MemoryCommitted() - 1;
    auto limited               = XML::Parse(
            "<root><item/><item/><nested><value/></nested></root>",
            {},
            limits);
    REQUIRE_FALSE(limited);
    CHECK(limited.error().code == ParseErrorCode::LimitExceeded);
}

TEST_CASE("XML wide attributes support indexed lookup and reject duplicates", "[serialization][xml]")
{
    for (UIntSize count: {15U, 16U, 17U, 64U, 1000U})
    {
        INFO("attributes=" << count);
        std::string              attributes;
        std::vector<std::string> keys;
        for (UIntSize i = 0; i < count; ++i)
        {
            keys.push_back("key" + std::to_string(i));
            attributes += ' ' + keys.back() + "=\"" + std::to_string(i) + '"';
        }
        auto parsed = XML::Parse("<root" + attributes + "><child" + attributes + "/></root>");
        REQUIRE(parsed);
        const auto root  = parsed->Root();
        const auto child = (*root.Children().begin()).TryElement();
        REQUIRE(child);
        for (UIntSize i = 0; i < count; ++i)
        {
            REQUIRE(root.Attribute(keys[i]));
            CHECK(root.Attribute(keys[i])->Value() == std::to_string(i));
            CHECK(child->Attribute(keys[i])->Value() == std::to_string(i));
        }
        CHECK_FALSE(root.Attribute("absent"));
        UIntSize order = 0;
        for (const auto attribute: root.Attributes())
            CHECK(attribute.Name() == keys[order++]);
        auto rejected = XML::Parse("<root" + attributes + " key0=\"again\"/>");
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error().code == ParseErrorCode::DuplicateName);
        REQUIRE(rejected.error().related);
        CHECK(rejected.error().related->begin == 6);

        XML::Builder                builder;
        std::vector<XML::Attribute> values;
        for (const auto& key: keys)
            values.push_back({key, "value"});
        values.push_back({keys.front(), "duplicate"});
        auto invalid = builder.Element("root", values, {});
        REQUIRE_FALSE(invalid);
        CHECK(invalid.error().code == XML::BuildErrorCode::InvalidContent);
        values.pop_back();
        auto element = builder.Element("root", values, {});
        REQUIRE(element);
        auto document = builder.Finish(*element);
        REQUIRE(document);
        for (const auto& key: keys)
            CHECK(document->Root().Attribute(key)->Value() == "value");
    }
}
