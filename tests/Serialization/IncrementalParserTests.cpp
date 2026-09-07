#include <NGIN/Serialization/JSON/JsonEventParser.hpp>
#include <NGIN/Serialization/XML/XmlEventParser.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace fs   = std::filesystem;
    namespace JSON = NGIN::Serialization::JSON;
    namespace XML  = NGIN::Serialization::XML;
    using namespace NGIN;
    using namespace NGIN::Serialization;

    struct JsonEvent
    {
        JSON::EventKind kind {};
        SourceSpan      span {};
        std::string     text {};
        bool            boolValue {false};
        Int64           intValue {0};
        UInt64          uintValue {0};
        F64             doubleValue {0};

        [[nodiscard]] friend bool operator==(const JsonEvent&, const JsonEvent&) = default;
    };

    struct XmlEvent
    {
        XML::EventKind kind {};
        SourceSpan     span {};
        std::string    name {};
        std::string    value {};

        [[nodiscard]] friend bool operator==(const XmlEvent&, const XmlEvent&) = default;
    };

    [[nodiscard]] JsonEvent Capture(const JSON::Event& event)
    {
        return {
                .kind        = event.kind,
                .span        = event.span,
                .text        = std::string {event.text},
                .boolValue   = event.boolValue,
                .intValue    = event.intValue,
                .uintValue   = event.uintValue,
                .doubleValue = event.doubleValue,
        };
    }

    [[nodiscard]] XmlEvent Capture(const XML::Event& event)
    {
        return {
                .kind  = event.kind,
                .span  = event.span,
                .name  = std::string {event.name},
                .value = std::string {event.value},
        };
    }

    [[nodiscard]] std::vector<JsonEvent>
    ParseJsonContiguous(std::string_view          source,
                        const JSON::ParseOptions& options  = {},
                        SourceId                  sourceId = {})
    {
        std::vector<JsonEvent> events;
        auto                   handler = [&events](const JSON::Event& event) {
            events.push_back(Capture(event));
            return JSON::EventAction::Continue();
        };
        auto sourceOptions   = options;
        sourceOptions.source = sourceId;
        ParseScratch scratch;
        auto         result = JSON::EventParser::ParseContiguous(
                source, handler, scratch, sourceOptions);
        if (!result)
            throw std::runtime_error {"contiguous JSON fixture did not parse"};
        return events;
    }

    [[nodiscard]] std::vector<XmlEvent>
    ParseXmlContiguous(std::string_view         source,
                       const XML::ParseOptions& options  = {},
                       SourceId                 sourceId = {})
    {
        std::vector<XmlEvent> events;
        auto                  handler = [&events](const XML::Event& event) {
            events.push_back(Capture(event));
            return XML::EventAction::Continue();
        };
        auto sourceOptions   = options;
        sourceOptions.source = sourceId;
        ParseScratch scratch;
        auto         result = XML::EventParser::ParseContiguous(
                source, handler, scratch, sourceOptions);
        if (!result)
            throw std::runtime_error {"contiguous XML fixture did not parse"};
        return events;
    }

    [[nodiscard]] std::vector<JsonEvent>
    ParseJsonChunks(std::string_view             source,
                    const std::vector<UIntSize>& ends,
                    const JSON::ParseOptions&    options  = {},
                    SourceId                     sourceId = {})
    {
        std::vector<JsonEvent> events;
        auto                   handler = [&events](const JSON::Event& event) {
            events.push_back(Capture(event));
            return JSON::EventAction::Continue();
        };
        ParseScratch                 scratch;
        auto                         sourceOptions = options;
        sourceOptions.source                       = sourceId;
        JSON::IncrementalEventParser parser {handler, scratch, sourceOptions};
        UIntSize                     begin = 0;
        UIntSize                     produced = 0;
        for (const auto end: ends)
        {
            if (end < begin || end > source.size())
                throw std::runtime_error {"invalid JSON chunk boundary"};
            const auto fed = parser.Feed(source.substr(begin, end - begin));
            produced += fed.eventsProduced;
            if (fed.HasError())
                throw std::runtime_error {"JSON feed did not request more input"};
            begin = end;
        }
        if (begin != source.size())
            throw std::runtime_error {"JSON chunks did not cover input"};
        const auto finished = parser.Finish();
        if (!finished.IsComplete() || finished.eventsProduced + produced != events.size())
            throw std::runtime_error {"incremental JSON fixture did not parse"};
        return events;
    }

    [[nodiscard]] std::vector<XmlEvent>
    ParseXmlChunks(std::string_view             source,
                   const std::vector<UIntSize>& ends,
                   const XML::ParseOptions&     options  = {},
                   SourceId                     sourceId = {})
    {
        std::vector<XmlEvent> events;
        auto                  handler = [&events](const XML::Event& event) {
            events.push_back(Capture(event));
            return XML::EventAction::Continue();
        };
        ParseScratch                scratch;
        auto                        sourceOptions = options;
        sourceOptions.source                      = sourceId;
        XML::IncrementalEventParser parser {handler, scratch, sourceOptions};
        UIntSize                    begin = 0;
        UIntSize                    produced = 0;
        for (const auto end: ends)
        {
            if (end < begin || end > source.size())
                throw std::runtime_error {"invalid XML chunk boundary"};
            const auto fed = parser.Feed(source.substr(begin, end - begin));
            produced += fed.eventsProduced;
            if (fed.HasError())
                throw std::runtime_error {"XML feed did not request more input"};
            begin = end;
        }
        if (begin != source.size())
            throw std::runtime_error {"XML chunks did not cover input"};
        const auto finished = parser.Finish();
        if (!finished.IsComplete() || finished.eventsProduced + produced != events.size())
            throw std::runtime_error {"incremental XML fixture did not parse"};
        return events;
    }

    [[nodiscard]] std::vector<UIntSize> RandomEnds(UIntSize size, std::mt19937& random)
    {
        std::vector<UIntSize> ends;
        UIntSize              offset = 0;
        while (offset < size)
        {
            const UIntSize remaining = size - offset;
            const UIntSize width     = (std::min) (remaining, static_cast<UIntSize>(1 + random() % 11));
            offset += width;
            ends.push_back(offset);
        }
        if (ends.empty())
            ends.push_back(0);
        return ends;
    }

    [[nodiscard]] fs::path CorpusRoot()
    {
        return fs::path {__FILE__}.parent_path() / "Corpus";
    }

    [[nodiscard]] std::string Read(const fs::path& path)
    {
        std::ifstream input {path, std::ios::binary};
        if (!input)
            throw std::runtime_error {"failed to open corpus fixture: " + path.string()};
        return {std::istreambuf_iterator<char> {input}, std::istreambuf_iterator<char> {}};
    }
}// namespace

TEST_CASE("incremental JSON events equal contiguous events at every byte boundary",
          "[serialization][json][incremental]")
{
    const std::string source =
            R"({"utf8":")"
            "\xF0\x9F\x98\x80"
            R"(","escaped":"\uD83D\uDE00\n","values":[null,true,false,-12,1.25e2,{"x":"y"}]})";
    const SourceId sourceId {31};
    const auto     expected = ParseJsonContiguous(source, {}, sourceId);

    for (UIntSize split = 0; split <= source.size(); ++split)
    {
        INFO("split=" << split);
        CHECK(ParseJsonChunks(source, {split, source.size()}, {}, sourceId) == expected);
    }

    std::mt19937 random {0x4E47494EU};
    for (UIntSize iteration = 0; iteration < 64; ++iteration)
        CHECK(ParseJsonChunks(source, RandomEnds(source.size(), random), {}, sourceId) == expected);
}

TEST_CASE("incremental XML events equal contiguous events at every byte boundary",
          "[serialization][xml][incremental]")
{
    const std::string source =
            R"(<?xml version="1.0"?><?before x?><root a="A&amp;B"><name>)"
            "\xF0\x9F\x98\x80"
            R"(</name><![CDATA[x<y]]><!--note--><empty/></root><?after y?>)";
    XML::ParseOptions options;
    options.trivia = XML::TriviaPolicy::Preserve;
    const SourceId sourceId {32};
    const auto     expected = ParseXmlContiguous(source, options, sourceId);

    for (UIntSize split = 0; split <= source.size(); ++split)
    {
        INFO("split=" << split);
        CHECK(ParseXmlChunks(source, {split, source.size()}, options, sourceId) == expected);
    }

    std::mt19937 random {0x584D4C21U};
    for (UIntSize iteration = 0; iteration < 64; ++iteration)
        CHECK(ParseXmlChunks(source, RandomEnds(source.size(), random), options, sourceId) == expected);
}

TEST_CASE("incremental parsers preserve corpus semantics at every byte boundary",
          "[serialization][incremental][corpus]")
{
    for (const auto& entry: fs::directory_iterator(CorpusRoot() / "json" / "valid"))
    {
        const auto source   = Read(entry.path());
        const auto expected = ParseJsonContiguous(source);
        for (UIntSize split = 0; split <= source.size(); ++split)
        {
            INFO(entry.path().filename().string() << " split=" << split);
            CHECK(ParseJsonChunks(source, {split, source.size()}) == expected);
        }
    }
    for (const auto& entry: fs::directory_iterator(CorpusRoot() / "xml" / "valid"))
    {
        const auto source   = Read(entry.path());
        const auto expected = ParseXmlContiguous(source);
        for (UIntSize split = 0; split <= source.size(); ++split)
        {
            INFO(entry.path().filename().string() << " split=" << split);
            CHECK(ParseXmlChunks(source, {split, source.size()}) == expected);
        }
    }

    auto jsonHandler = [](const JSON::Event&) { return JSON::EventAction::Continue(); };
    auto xmlHandler  = [](const XML::Event&) { return XML::EventAction::Continue(); };
    for (const auto& entry: fs::directory_iterator(CorpusRoot() / "json" / "invalid"))
    {
        const auto source = Read(entry.path());
        for (UIntSize split = 0; split <= source.size(); ++split)
        {
            INFO(entry.path().filename().string() << " split=" << split);
            ParseScratch                 scratch;
            JSON::IncrementalEventParser parser {jsonHandler, scratch};
            (void) parser.Feed(std::string_view {source}.substr(0, split));
            (void) parser.Feed(std::string_view {source}.substr(split));
            CHECK(parser.Finish().HasError());
        }
    }
    for (const auto& entry: fs::directory_iterator(CorpusRoot() / "xml" / "invalid"))
    {
        const auto source = Read(entry.path());
        for (UIntSize split = 0; split <= source.size(); ++split)
        {
            INFO(entry.path().filename().string() << " split=" << split);
            ParseScratch                scratch;
            XML::IncrementalEventParser parser {xmlHandler, scratch};
            (void) parser.Feed(std::string_view {source}.substr(0, split));
            (void) parser.Feed(std::string_view {source}.substr(split));
            CHECK(parser.Finish().HasError());
        }
    }
}

TEST_CASE("incremental parsers keep limits errors and reset state global",
          "[serialization][incremental][limits]")
{
    auto         jsonHandler = [](const JSON::Event&) { return JSON::EventAction::Continue(); };
    ParseScratch jsonScratch;
    ParseLimits  limits;
    limits.maxInputBytes = 4;
    JSON::IncrementalEventParser limitedJson {jsonHandler, jsonScratch, {.source = SourceId {41}}, limits};
    CHECK(limitedJson.Feed("12").status == IncrementalParseStatus::NeedMoreInput);
    const auto jsonLimit = limitedJson.Feed("345");
    REQUIRE(jsonLimit.HasError());
    REQUIRE(jsonLimit.diagnostic);
    CHECK(jsonLimit.diagnostic->code == ParseErrorCode::LimitExceeded);
    CHECK(jsonLimit.diagnostic->span.source == SourceId {41});
    CHECK(jsonLimit.diagnostic->location.offset == 2);

    limits.maxInputBytes       = 1024;
    limits.maxTotalMemoryBytes = 32;
    JSON::IncrementalEventParser memoryLimitedJson {
            jsonHandler,
            jsonScratch,
            {.source = SourceId {41}},
            limits};
    CHECK(memoryLimitedJson.Feed("12").status == IncrementalParseStatus::NeedMoreInput);
    const auto jsonMemoryLimit = memoryLimitedJson.Feed(std::string(100, '3'));
    REQUIRE(jsonMemoryLimit.diagnostic);
    CHECK(jsonMemoryLimit.diagnostic->code == ParseErrorCode::LimitExceeded);

    JSON::IncrementalEventParser incompleteJson {jsonHandler, jsonScratch, {.source = SourceId {42}}, {}};
    CHECK_FALSE(incompleteJson.Feed(R"({"a":")"
                                    "\\uD83D")
                        .HasError());
    const auto jsonIncomplete = incompleteJson.Finish();
    REQUIRE(jsonIncomplete.HasError());
    REQUIRE(jsonIncomplete.diagnostic);
    CHECK(jsonIncomplete.diagnostic->code == ParseErrorCode::UnexpectedEnd);
    CHECK(jsonIncomplete.diagnostic->span.source == SourceId {42});

    const std::string malformedJson = "{\"a\":1,\n\"b\":]}";
    const auto        jsonReference = JSON::Parse(malformedJson, {.source = SourceId {45}});
    REQUIRE_FALSE(jsonReference);
    ParseScratch                 malformedJsonScratch;
    JSON::IncrementalEventParser malformedJsonParser {
            jsonHandler,
            malformedJsonScratch,
            {.source = SourceId {45}},
            {}};
    (void) malformedJsonParser.Feed(std::string_view {malformedJson}.substr(0, 6));
    (void) malformedJsonParser.Feed(std::string_view {malformedJson}.substr(6));
    const auto malformedJsonResult = malformedJsonParser.Finish();
    REQUIRE(malformedJsonResult.diagnostic);
    CHECK(malformedJsonResult.diagnostic->code == jsonReference.error().code);
    CHECK(malformedJsonResult.diagnostic->location.offset == jsonReference.error().location.offset);
    CHECK(malformedJsonResult.diagnostic->span == jsonReference.error().span);

    std::vector<Int64> values;
    auto               resetHandler = [&values](const JSON::Event& event) {
        if (event.kind == JSON::EventKind::Int64)
            values.push_back(event.intValue);
        return JSON::EventAction::Continue();
    };
    ParseScratch                 resetScratch;
    JSON::IncrementalEventParser resettable {resetHandler, resetScratch};
    CHECK(resettable.Feed("1").status == IncrementalParseStatus::NeedMoreInput);
    CHECK(resettable.Finish().IsComplete());
    CHECK(resettable.Finish().IsComplete());
    CHECK(resettable.Feed("2").HasError());
    resettable.Reset();
    CHECK(resettable.Feed("2").status == IncrementalParseStatus::NeedMoreInput);
    CHECK(resettable.Finish().IsComplete());
    CHECK((values == std::vector<Int64> {1, 2}));

    auto         xmlHandler = [](const XML::Event&) { return XML::EventAction::Continue(); };
    ParseScratch xmlScratch;
    limits               = {};
    limits.maxInputBytes = 7;
    XML::IncrementalEventParser limitedXml {xmlHandler, xmlScratch, {.source = SourceId {43}}, limits};
    CHECK(limitedXml.Feed("<root").status == IncrementalParseStatus::NeedMoreInput);
    const auto xmlLimit = limitedXml.Feed("/>");
    CHECK(xmlLimit.status == IncrementalParseStatus::EventProduced);
    const auto xmlLimitExceeded = limitedXml.Feed(" ");
    REQUIRE(xmlLimitExceeded.HasError());
    REQUIRE(xmlLimitExceeded.diagnostic);
    CHECK(xmlLimitExceeded.diagnostic->code == ParseErrorCode::LimitExceeded);

    XML::IncrementalEventParser incompleteXml {xmlHandler, xmlScratch, {.source = SourceId {44}}, {}};
    CHECK_FALSE(incompleteXml.Feed("<root>&amp").HasError());
    const auto xmlIncomplete = incompleteXml.Finish();
    REQUIRE(xmlIncomplete.HasError());
    REQUIRE(xmlIncomplete.diagnostic);
    CHECK(xmlIncomplete.diagnostic->span.source == SourceId {44});

    const std::string malformedXml = "<root>\n<child></root>";
    const auto        xmlReference = XML::Parse(malformedXml, {.source = SourceId {46}});
    REQUIRE_FALSE(xmlReference);
    ParseScratch                malformedXmlScratch;
    XML::IncrementalEventParser malformedXmlParser {
            xmlHandler,
            malformedXmlScratch,
            {.source = SourceId {46}},
            {}};
    (void) malformedXmlParser.Feed(std::string_view {malformedXml}.substr(0, 9));
    (void) malformedXmlParser.Feed(std::string_view {malformedXml}.substr(9));
    const auto malformedXmlResult = malformedXmlParser.Finish();
    REQUIRE(malformedXmlResult.diagnostic);
    CHECK(malformedXmlResult.diagnostic->code == xmlReference.error().code);
    CHECK(malformedXmlResult.diagnostic->location.offset == xmlReference.error().location.offset);
    CHECK(malformedXmlResult.diagnostic->span == xmlReference.error().span);

    XML::ParseOptions doctype;
    doctype.doctype = XML::DoctypePolicy::AllowWithoutExternalEntities;
    XML::IncrementalEventParser external {xmlHandler, xmlScratch, doctype};
    (void) external.Feed(R"(<!DOCTYPE root SYSTEM "file:///secret"><root/>)");
    const auto externalResult = external.Finish();
    REQUIRE(externalResult.HasError());
    REQUIRE(externalResult.diagnostic);
    CHECK(externalResult.diagnostic->code == ParseErrorCode::UnsupportedConstruct);
}

TEST_CASE("incremental parsers propagate handler rejection exactly once",
          "[serialization][incremental][handler]")
{
    UIntSize jsonCalls   = 0;
    auto     jsonHandler = [&jsonCalls](const JSON::Event&) {
        ++jsonCalls;
        return JSON::EventAction::Stop(71);
    };
    ParseScratch                 jsonScratch;
    JSON::IncrementalEventParser json {jsonHandler, jsonScratch};
    CHECK(json.Feed("null").status == IncrementalParseStatus::NeedMoreInput);
    const auto jsonResult = json.Finish();
    REQUIRE(jsonResult.HasError());
    REQUIRE(jsonResult.diagnostic);
    CHECK(jsonResult.diagnostic->code == ParseErrorCode::HandlerRejected);
    CHECK(jsonResult.diagnostic->consumerContext == 71);
    CHECK(jsonCalls == 1);
    CHECK(json.Finish().HasError());
    CHECK(jsonCalls == 1);

    UIntSize xmlCalls   = 0;
    auto     xmlHandler = [&xmlCalls](const XML::Event&) {
        ++xmlCalls;
        return XML::EventAction::Stop(72);
    };
    ParseScratch                xmlScratch;
    XML::IncrementalEventParser xml {xmlHandler, xmlScratch};
    CHECK(xml.Feed("<root/>").HasError());
    const auto xmlResult = xml.Finish();
    REQUIRE(xmlResult.HasError());
    REQUIRE(xmlResult.diagnostic);
    CHECK(xmlResult.diagnostic->code == ParseErrorCode::HandlerRejected);
    CHECK(xmlResult.diagnostic->consumerContext == 72);
    CHECK(xmlCalls == 1);
}

TEST_CASE("incremental parsers deliver during Feed with bounded retained storage", "[serialization][incremental][memory]")
{
    UIntSize     jsonCalls   = 0;
    auto         jsonHandler = [&](const JSON::Event&) { ++jsonCalls; return JSON::EventAction::Continue(); };
    ParseScratch jsonScratch;
    ParseLimits  limits;
    limits.maxTotalMemoryBytes = 4096;
    JSON::IncrementalEventParser json {jsonHandler, jsonScratch, {}, limits};
    CHECK(json.Feed("[").eventsProduced == 1);
    for (UIntSize i = 0; i < 10000; ++i)
    {
        const auto result = json.Feed(i ? R"(,"line\nnext")" : R"("line\nnext")");
        REQUIRE_FALSE(result.HasError());
        CHECK(result.eventsProduced == 1);
        CHECK(json.BufferedBytes() == 0);
        CHECK(json.MemoryCommitted() <= limits.maxTotalMemoryBytes);
    }
    CHECK(jsonCalls == 10001);
    CHECK(json.Feed("]").eventsProduced == 1);
    CHECK(json.Finish().IsComplete());
    CHECK(json.TotalBytes() > 100000);
    CHECK(json.Finish().eventsProduced == 0);

    UIntSize                    xmlCalls   = 0;
    auto                        xmlHandler = [&](const XML::Event&) { ++xmlCalls; return XML::EventAction::Continue(); };
    ParseScratch                xmlScratch;
    XML::IncrementalEventParser xml {xmlHandler, xmlScratch, {}, limits};
    CHECK(xml.Feed("<root>").eventsProduced == 1);
    for (UIntSize i = 0; i < 10000; ++i)
    {
        const auto result = xml.Feed("<item a=\"&amp;\">text</item>");
        REQUIRE_FALSE(result.HasError());
        CHECK(result.eventsProduced == 4);
        CHECK(xml.BufferedBytes() == 0);
        CHECK(xml.MemoryCommitted() <= limits.maxTotalMemoryBytes);
    }
    CHECK(xmlCalls == 40001);
    CHECK(xml.Feed("</root>").eventsProduced == 1);
    CHECK(xml.Finish().IsComplete());
    CHECK(xml.TotalBytes() > 200000);
}

TEST_CASE("incremental JSON duplicate policies and comments survive arbitrary chunk boundaries", "[serialization][incremental][json]")
{
    std::string source = "{/*before*/";
    for (UIntSize i = 0; i < 80; ++i)
        source += "\"key" + std::to_string(i) + "\":{\"a\":[1,2,]},";
    source += R"("\u006bey0":{"a":[99]},//after)";
    source += "\r\n}";
    JSON::ParseOptions options;
    options.comments       = JSON::CommentPolicy::Allow;
    options.trailingCommas = JSON::TrailingCommaPolicy::Allow;
    std::mt19937 random {573};
    for (const auto policy: {JSON::DuplicateKeyPolicy::KeepFirst, JSON::DuplicateKeyPolicy::KeepLast, JSON::DuplicateKeyPolicy::Preserve})
    {
        options.duplicateKeys = policy;
        const auto expected   = ParseJsonContiguous(source, options, SourceId {83});
        for (UIntSize i = 0; i < 16; ++i)
            CHECK(ParseJsonChunks(source, RandomEnds(source.size(), random), options, SourceId {83}) == expected);
    }
    options.duplicateKeys                = JSON::DuplicateKeyPolicy::Reject;
    auto                         handler = [](const JSON::Event&) { return JSON::EventAction::Continue(); };
    ParseScratch                 scratch;
    JSON::IncrementalEventParser parser {handler, scratch, options};
    for (char c: source)
        (void) parser.Feed(std::string_view {&c, 1});
    const auto result = parser.Finish();
    REQUIRE(result.diagnostic);
    CHECK(result.diagnostic->code == ParseErrorCode::DuplicateName);
    REQUIRE(result.diagnostic->related);
    CHECK(result.diagnostic->related->begin == source.find("\"key0\""));
    CHECK(result.diagnostic->span.begin == source.find("\"\\u006bey0\""));
}

TEST_CASE("incremental XML validates wide split tags declarations and trailing content", "[serialization][incremental][xml]")
{
    std::string tag = "<root";
    for (UIntSize i = 0; i < 80; ++i)
        tag += " key" + std::to_string(i) + "=\"A&amp;B\"";
    const std::string source = "\xef\xbb\xbf<?xml version=\"1.0\"?><!DOCTYPE root>" + tag + ">\r\n<!--x--><![CDATA[a>b]]><?task a?></root>";
    XML::ParseOptions options;
    options.trivia        = XML::TriviaPolicy::Preserve;
    options.doctype       = XML::DoctypePolicy::AllowWithoutExternalEntities;
    const auto   expected = ParseXmlContiguous(source, options, SourceId {84});
    std::mt19937 random {573};
    for (UIntSize i = 0; i < 16; ++i)
        CHECK(ParseXmlChunks(source, RandomEnds(source.size(), random), options, SourceId {84}) == expected);
    auto                        handler = [](const XML::Event&) { return XML::EventAction::Continue(); };
    ParseScratch                scratch;
    XML::IncrementalEventParser parser {handler, scratch, options};
    const std::string           duplicate = tag + " key0=\"duplicate\"/>";
    for (char c: duplicate)
        (void) parser.Feed(std::string_view {&c, 1});
    auto result = parser.Finish();
    REQUIRE(result.diagnostic);
    CHECK(result.diagnostic->code == ParseErrorCode::DuplicateName);
    REQUIRE(result.diagnostic->related);
    CHECK(result.diagnostic->related->begin == 6);
    parser.Reset();
    REQUIRE_FALSE(parser.Feed("<root/>").HasError());
    CHECK(parser.Feed("<second/>").HasError());
}

TEST_CASE("incremental errors retain delivered events and global line positions", "[serialization][incremental][diagnostic]")
{
    UIntSize                     count   = 0;
    auto                         handler = [&](const JSON::Event&) { ++count; return JSON::EventAction::Continue(); };
    ParseScratch                 scratch;
    JSON::IncrementalEventParser parser {handler, scratch, {.source = SourceId {85}}};
    CHECK(parser.Feed("[1,\r").eventsProduced == 2);
    CHECK(count == 2);
    REQUIRE_FALSE(parser.Feed("\n").HasError());
    auto result = parser.Feed("}");
    REQUIRE(result.diagnostic);
    CHECK(result.diagnostic->location.offset == 5);
    CHECK(result.diagnostic->location.line == 2);
    CHECK(result.diagnostic->location.column == 1);
    CHECK(result.diagnostic->span.source == SourceId {85});
    CHECK(count == 2);
    CHECK(parser.Finish().HasError());
    CHECK(count == 2);

    auto                         throwing = [](const JSON::Event&) -> JSON::EventAction { throw std::runtime_error {"handler"}; };
    JSON::IncrementalEventParser throwingParser {throwing, scratch};
    CHECK_THROWS_AS(throwingParser.Feed("["), std::runtime_error);
}

TEST_CASE("incremental parsers agree with document validation on mutated inputs", "[serialization][incremental][validation]")
{
    const std::string jsonSeed  = R"({"a":[true,null,-1.2e3,"\u20ac"],"b":{"x":1}})";
    const std::string xmlSeed   = "<root a=\"A&amp;B\"><child>text</child><!--c--><![CDATA[x]]></root>";
    const std::string mutations = "{}[],:<>/!?\"'&;=x0 \r\n\\";
    std::mt19937      random {615};
    auto              jsonHandler = [](const JSON::Event&) { return JSON::EventAction::Continue(); };
    auto              xmlHandler  = [](const XML::Event&) { return XML::EventAction::Continue(); };
    for (UIntSize i = 0; i < 1000; ++i)
    {
        for (bool json: {true, false})
        {
            std::string    source = json ? jsonSeed : xmlSeed;
            const UIntSize offset = random() % source.size();
            if (i % 3 == 0)
                source.erase(offset, 1);
            else if (i % 3 == 1)
                source.insert(offset, 1, mutations[random() % mutations.size()]);
            else
                source[offset] = mutations[random() % mutations.size()];
            INFO("source=" << source);
            ParseScratch scratch;
            UIntSize     begin = 0;
            if (json)
            {
                const bool                   expected = JSON::Parse(source).has_value();
                JSON::IncrementalEventParser parser {jsonHandler, scratch};
                for (const auto end: RandomEnds(source.size(), random))
                {
                    (void) parser.Feed(std::string_view {source}.substr(begin, end - begin));
                    begin = end;
                }
                CHECK(parser.Finish().IsComplete() == expected);
            }
            else
            {
                const bool                  expected = XML::Parse(source).has_value();
                XML::IncrementalEventParser parser {xmlHandler, scratch};
                for (const auto end: RandomEnds(source.size(), random))
                {
                    (void) parser.Feed(std::string_view {source}.substr(begin, end - begin));
                    begin = end;
                }
                CHECK(parser.Finish().IsComplete() == expected);
            }
        }
    }
}
