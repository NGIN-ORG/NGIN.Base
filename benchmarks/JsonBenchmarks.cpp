#include <NGIN/Benchmark.hpp>
#include <NGIN/Serialization/JSON/JsonParser.hpp>

#include <iostream>
#include <string_view>

#if defined(NGIN_HAVE_SIMDJSON)
#include <simdjson.h>
#endif

#if defined(NGIN_HAVE_RAPIDJSON)
#include <rapidjson/document.h>
#endif

int main()
{
    using namespace NGIN;
    using namespace NGIN::Serialization;

    const char* smallJson = R"({"name":"NGIN","count":3,"active":true,"tags":["a","b","c"],"child":{"x":1}})";

    const char* mediumJson = R"({
        "items": [
            {"id":1,"name":"alpha","flags":[true,false,true]},
            {"id":2,"name":"beta","flags":[false,false,true]},
            {"id":3,"name":"gamma","flags":[true,true,true]},
            {"id":4,"name":"delta","flags":[false,true,false]}
        ],
        "config": {
            "threshold": 0.75,
            "enabled": true,
            "meta": {"version": "1.0", "build": 42}
        }
    })";

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        auto result = JsonParser::Parse(std::string_view {smallJson});
        ctx.doNotOptimize(result.HasValue());
        ctx.stop();
    },
                        "JsonParser small document");

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        auto result = JsonParser::Parse(std::string_view {mediumJson});
        ctx.doNotOptimize(result.HasValue());
        ctx.stop();
    },
                        "JsonParser medium document");

#if defined(NGIN_HAVE_SIMDJSON)
    const simdjson::padded_string smallPadded(std::string_view {smallJson});
    const simdjson::padded_string mediumPadded(std::string_view {mediumJson});

    simdjson::dom::parser parser;

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        auto doc = parser.parse(smallPadded);
        ctx.doNotOptimize(doc.error());
        ctx.stop();
    },
                        "simdjson small document");

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        auto doc = parser.parse(mediumPadded);
        ctx.doNotOptimize(doc.error());
        ctx.stop();
    },
                        "simdjson medium document");
#endif

#if defined(NGIN_HAVE_RAPIDJSON)
    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        rapidjson::Document doc;
        doc.Parse(smallJson);
        ctx.doNotOptimize(!doc.HasParseError());
        ctx.stop();
    },
                        "RapidJSON small document");

    Benchmark::Register([&](BenchmarkContext& ctx) {
        ctx.start();
        rapidjson::Document doc;
        doc.Parse(mediumJson);
        ctx.doNotOptimize(!doc.HasParseError());
        ctx.stop();
    },
                        "RapidJSON medium document");
#endif

    auto results = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);

    return 0;
}
