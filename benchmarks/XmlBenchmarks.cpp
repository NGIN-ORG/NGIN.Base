#include <NGIN/Benchmark.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <iostream>

#if defined(NGIN_HAVE_PUGIXML)
#include <pugixml.hpp>
#endif

#if defined(NGIN_HAVE_TINYXML2)
#include <tinyxml2.h>
#endif

int main()
{
    using namespace NGIN;
    using namespace NGIN::Serialization;

    const char* smallXml = R"(<root id="42"><child>Value</child><child attr="x"/></root>)";

    const char* mediumXml = R"(<root>
        <items>
            <item id="1" name="alpha"/>
            <item id="2" name="beta"/>
            <item id="3" name="gamma"/>
            <item id="4" name="delta"/>
        </items>
        <config threshold="0.75" enabled="true">
            <meta version="1.0" build="42"/>
        </config>
    </root>)";

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        auto result = XmlParser::Parse(std::string_view {smallXml});
        ctx.doNotOptimize(result.HasValue());
        ctx.stop();
    },
                        "XmlParser small document");

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        auto result = XmlParser::Parse(std::string_view {mediumXml});
        ctx.doNotOptimize(result.HasValue());
        ctx.stop();
    },
                        "XmlParser medium document");

#if defined(NGIN_HAVE_PUGIXML)
    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        pugi::xml_document doc;
        const auto         result = doc.load_string(smallXml);
        ctx.doNotOptimize(result.status == pugi::status_ok);
        ctx.stop();
    },
                        "pugixml small document");

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        pugi::xml_document doc;
        const auto         result = doc.load_string(mediumXml);
        ctx.doNotOptimize(result.status == pugi::status_ok);
        ctx.stop();
    },
                        "pugixml medium document");
#endif

#if defined(NGIN_HAVE_TINYXML2)
    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        tinyxml2::XMLDocument doc;
        const auto            result = doc.Parse(smallXml);
        ctx.doNotOptimize(result == tinyxml2::XML_SUCCESS);
        ctx.stop();
    },
                        "tinyxml2 small document");

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        tinyxml2::XMLDocument doc;
        const auto            result = doc.Parse(mediumXml);
        ctx.doNotOptimize(result == tinyxml2::XML_SUCCESS);
        ctx.stop();
    },
                        "tinyxml2 medium document");
#endif

    auto results = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);

    return 0;
}
