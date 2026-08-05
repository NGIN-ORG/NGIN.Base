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
        ParseScratch scratch;
        auto         result = JSON::EventParser::ParseContiguous(
                BorrowedTextView {source, sourceId}, handler, scratch, options);
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
        ParseScratch scratch;
        auto         result = XML::EventParser::ParseContiguous(
                BorrowedTextView {source, sourceId}, handler, scratch, options);
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
        JSON::IncrementalEventParser parser {handler, scratch, options, ParseLimits {}, sourceId};
        UIntSize                     begin = 0;
        for (const auto end: ends)
        {
            if (end < begin || end > source.size())
                throw std::runtime_error {"invalid JSON chunk boundary"};
            const auto fed = parser.Feed(source.substr(begin, end - begin));
            if (fed.status != IncrementalParseStatus::NeedMoreInput)
                throw std::runtime_error {"JSON feed did not request more input"};
            begin = end;
        }
        if (begin != source.size())
            throw std::runtime_error {"JSON chunks did not cover input"};
        const auto finished = parser.Finish();
        if (!finished.IsComplete() || finished.eventsProduced != events.size())
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
        XML::IncrementalEventParser parser {handler, scratch, options, ParseLimits {}, sourceId};
        UIntSize                    begin = 0;
        for (const auto end: ends)
        {
            if (end < begin || end > source.size())
                throw std::runtime_error {"invalid XML chunk boundary"};
            const auto fed = parser.Feed(source.substr(begin, end - begin));
            if (fed.status != IncrementalParseStatus::NeedMoreInput)
                throw std::runtime_error {"XML feed did not request more input"};
            begin = end;
        }
        if (begin != source.size())
            throw std::runtime_error {"XML chunks did not cover input"};
        const auto finished = parser.Finish();
        if (!finished.IsComplete() || finished.eventsProduced != events.size())
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
            CHECK(parser.Feed(std::string_view {source}.substr(0, split)).status ==
                  IncrementalParseStatus::NeedMoreInput);
            CHECK(parser.Feed(std::string_view {source}.substr(split)).status ==
                  IncrementalParseStatus::NeedMoreInput);
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
            CHECK(parser.Feed(std::string_view {source}.substr(0, split)).status ==
                  IncrementalParseStatus::NeedMoreInput);
            CHECK(parser.Feed(std::string_view {source}.substr(split)).status ==
                  IncrementalParseStatus::NeedMoreInput);
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
    JSON::IncrementalEventParser limitedJson {jsonHandler, jsonScratch, {}, limits, SourceId {41}};
    CHECK(limitedJson.Feed("12").status == IncrementalParseStatus::NeedMoreInput);
    const auto jsonLimit = limitedJson.Feed("345");
    REQUIRE(jsonLimit.HasError());
    REQUIRE(jsonLimit.diagnostic);
    CHECK(jsonLimit.diagnostic->code == ParseErrorCode::LimitExceeded);
    CHECK(jsonLimit.diagnostic->span.source == SourceId {41});
    CHECK(jsonLimit.diagnostic->location.offset == 2);

    limits.maxInputBytes       = 64;
    limits.maxTotalMemoryBytes = 4;
    JSON::IncrementalEventParser memoryLimitedJson {
            jsonHandler,
            jsonScratch,
            {},
            limits,
            SourceId {41}};
    CHECK(memoryLimitedJson.Feed("12").status == IncrementalParseStatus::NeedMoreInput);
    const auto jsonMemoryLimit = memoryLimitedJson.Feed("345");
    REQUIRE(jsonMemoryLimit.diagnostic);
    CHECK(jsonMemoryLimit.diagnostic->code == ParseErrorCode::LimitExceeded);

    JSON::IncrementalEventParser incompleteJson {jsonHandler, jsonScratch, {}, {}, SourceId {42}};
    CHECK(incompleteJson.Feed(R"({"a":")"
                              "\\uD83D")
                  .status ==
          IncrementalParseStatus::NeedMoreInput);
    const auto jsonIncomplete = incompleteJson.Finish();
    REQUIRE(jsonIncomplete.HasError());
    REQUIRE(jsonIncomplete.diagnostic);
    CHECK(jsonIncomplete.diagnostic->code == ParseErrorCode::UnexpectedEnd);
    CHECK(jsonIncomplete.diagnostic->span.source == SourceId {42});

    const std::string malformedJson = "{\"a\":1,\n\"b\":]}";
    ParseScratch      jsonReferenceScratch;
    const auto        jsonReference = JSON::ParseBorrowed(
            BorrowedTextView {malformedJson, SourceId {45}}, jsonReferenceScratch);
    REQUIRE_FALSE(jsonReference);
    ParseScratch                 malformedJsonScratch;
    JSON::IncrementalEventParser malformedJsonParser {
            jsonHandler,
            malformedJsonScratch,
            {},
            {},
            SourceId {45}};
    CHECK(malformedJsonParser.Feed(std::string_view {malformedJson}.substr(0, 6)).status ==
          IncrementalParseStatus::NeedMoreInput);
    CHECK(malformedJsonParser.Feed(std::string_view {malformedJson}.substr(6)).status ==
          IncrementalParseStatus::NeedMoreInput);
    const auto malformedJsonResult = malformedJsonParser.Finish();
    REQUIRE(malformedJsonResult.diagnostic);
    CHECK(malformedJsonResult.diagnostic->code == jsonReference.Error().code);
    CHECK(malformedJsonResult.diagnostic->location.offset == jsonReference.Error().location.offset);
    CHECK(malformedJsonResult.diagnostic->span == jsonReference.Error().span);

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
    XML::IncrementalEventParser limitedXml {xmlHandler, xmlScratch, {}, limits, SourceId {43}};
    CHECK(limitedXml.Feed("<root").status == IncrementalParseStatus::NeedMoreInput);
    const auto xmlLimit = limitedXml.Feed("/>");
    CHECK(xmlLimit.status == IncrementalParseStatus::NeedMoreInput);
    const auto xmlLimitExceeded = limitedXml.Feed(" ");
    REQUIRE(xmlLimitExceeded.HasError());
    REQUIRE(xmlLimitExceeded.diagnostic);
    CHECK(xmlLimitExceeded.diagnostic->code == ParseErrorCode::LimitExceeded);

    XML::IncrementalEventParser incompleteXml {xmlHandler, xmlScratch, {}, {}, SourceId {44}};
    CHECK(incompleteXml.Feed("<root>&amp").status == IncrementalParseStatus::NeedMoreInput);
    const auto xmlIncomplete = incompleteXml.Finish();
    REQUIRE(xmlIncomplete.HasError());
    REQUIRE(xmlIncomplete.diagnostic);
    CHECK(xmlIncomplete.diagnostic->span.source == SourceId {44});

    const std::string malformedXml = "<root>\n<child></root>";
    ParseScratch      xmlReferenceScratch;
    const auto        xmlReference = XML::ParseBorrowed(
            BorrowedTextView {malformedXml, SourceId {46}}, xmlReferenceScratch);
    REQUIRE_FALSE(xmlReference);
    ParseScratch                malformedXmlScratch;
    XML::IncrementalEventParser malformedXmlParser {
            xmlHandler,
            malformedXmlScratch,
            {},
            {},
            SourceId {46}};
    CHECK(malformedXmlParser.Feed(std::string_view {malformedXml}.substr(0, 9)).status ==
          IncrementalParseStatus::NeedMoreInput);
    CHECK(malformedXmlParser.Feed(std::string_view {malformedXml}.substr(9)).status ==
          IncrementalParseStatus::NeedMoreInput);
    const auto malformedXmlResult = malformedXmlParser.Finish();
    REQUIRE(malformedXmlResult.diagnostic);
    CHECK(malformedXmlResult.diagnostic->code == xmlReference.Error().code);
    CHECK(malformedXmlResult.diagnostic->location.offset == xmlReference.Error().location.offset);
    CHECK(malformedXmlResult.diagnostic->span == xmlReference.Error().span);

    XML::ParseOptions doctype;
    doctype.doctype = XML::DoctypePolicy::AllowWithoutExternalEntities;
    XML::IncrementalEventParser external {xmlHandler, xmlScratch, doctype};
    CHECK(external.Feed(R"(<!DOCTYPE root SYSTEM "file:///secret"><root/>)").status ==
          IncrementalParseStatus::NeedMoreInput);
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
    CHECK(xml.Feed("<root/>").status == IncrementalParseStatus::NeedMoreInput);
    const auto xmlResult = xml.Finish();
    REQUIRE(xmlResult.HasError());
    REQUIRE(xmlResult.diagnostic);
    CHECK(xmlResult.diagnostic->code == ParseErrorCode::HandlerRejected);
    CHECK(xmlResult.diagnostic->consumerContext == 72);
    CHECK(xmlCalls == 1);
}
