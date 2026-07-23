#include <NGIN/Benchmark.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Serialization/JSON/JsonEventParser.hpp>
#include <NGIN/Serialization/JSON/JsonParser.hpp>
#include <NGIN/Serialization/JSON/JsonWriter.hpp>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(NGIN_HAVE_SIMDJSON)
#include <simdjson.h>
#endif

#if defined(NGIN_HAVE_RAPIDJSON)
#include <rapidjson/document.h>
#endif

namespace
{
    using NGIN::Benchmark;
    using NGIN::BenchmarkContext;
    using NGIN::BenchmarkResult;
    using NGIN::Units::Milliseconds;

    struct InputCase
    {
        std::string name;
        std::string source;
        bool        valid {true};
    };

    struct BatchScale
    {
        std::string name;
        std::size_t operations {1};
    };

    class CountingAllocator
    {
    public:
        [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment) noexcept
        {
            void* memory = m_upstream.Allocate(size, alignment);
            if (memory)
            {
                ++allocationCount;
                allocatedBytes += size;
                liveBytes += size;
                peakLiveBytes = (std::max) (peakLiveBytes, liveBytes);
            }
            return memory;
        }

        void Deallocate(void* memory, std::size_t size, std::size_t alignment) noexcept
        {
            if (memory)
                liveBytes -= (std::min) (liveBytes, size);
            m_upstream.Deallocate(memory, size, alignment);
        }

        [[nodiscard]] constexpr std::size_t MaxSize() const noexcept
        {
            return (std::numeric_limits<std::size_t>::max)();
        }

        [[nodiscard]] constexpr std::size_t Remaining() const noexcept
        {
            return MaxSize();
        }

        [[nodiscard]] constexpr bool Owns(const void*) const noexcept
        {
            return true;
        }

        std::size_t allocationCount {0};
        std::size_t allocatedBytes {0};
        std::size_t liveBytes {0};
        std::size_t peakLiveBytes {0};

    private:
        NGIN::Memory::SystemAllocator m_upstream {};
    };

    [[nodiscard]] std::string NumericArray(std::size_t approximateBytes)
    {
        std::string result = "[";
        for (std::size_t value = 0; result.size() < approximateBytes; ++value)
        {
            if (value != 0)
                result.push_back(',');
            result += std::to_string(value);
        }
        result.push_back(']');
        return result;
    }

    [[nodiscard]] std::string NestedArray(std::size_t depth)
    {
        std::string result(depth, '[');
        result.push_back('0');
        result.append(depth, ']');
        return result;
    }

    [[nodiscard]] std::size_t BatchSize(std::size_t inputBytes)
    {
        if (inputBytes <= 512)
            return 256;
        if (inputBytes <= 4 * 1024)
            return 64;
        if (inputBytes <= 32 * 1024)
            return 8;
        return 1;
    }

    template<typename F>
    void RegisterBatched(std::vector<BatchScale>& scales,
                         std::string              name,
                         std::size_t              operations,
                         F                        operation)
    {
        scales.push_back(BatchScale {.name = name, .operations = operations});
        Benchmark::Register(
                [operations, operation = std::move(operation)](BenchmarkContext& context) mutable {
                    context.start();
                    for (std::size_t index = 0; index < operations; ++index)
                        operation(context);
                    context.stop();
                },
                name);
    }

    void NormalizePerDocument(std::vector<BenchmarkResult<Milliseconds>>& results,
                              const std::vector<BatchScale>&              scales)
    {
        for (auto& result: results)
        {
            const auto scale = std::find_if(scales.begin(), scales.end(), [&](const BatchScale& candidate) {
                return candidate.name == result.name;
            });
            if (scale == scales.end() || scale->operations == 1)
                continue;

            const auto divisor       = static_cast<NGIN::F64>(scale->operations);
            result.averageTime       = Milliseconds {result.averageTime.GetValue() / divisor};
            result.minTime           = Milliseconds {result.minTime.GetValue() / divisor};
            result.maxTime           = Milliseconds {result.maxTime.GetValue() / divisor};
            result.standardDeviation = Milliseconds {result.standardDeviation.GetValue() / divisor};
            result.medianTime        = Milliseconds {result.medianTime.GetValue() / divisor};
            result.percentile25      = Milliseconds {result.percentile25.GetValue() / divisor};
            result.percentile75      = Milliseconds {result.percentile75.GetValue() / divisor};
        }
    }

    [[nodiscard]] bool ValidateNginInputs(const std::vector<InputCase>& cases)
    {
        namespace JSON = NGIN::Serialization::JSON;
        using namespace NGIN::Serialization;

        for (const auto& input: cases)
        {
            auto result = JSON::Parse(OwnedTextBuffer {input.source});
            if (result.HasValue() != input.valid)
            {
                std::cerr << "NGIN preflight disagreed with expected validity for '" << input.name << "'.\n";
                return false;
            }
        }
        return true;
    }

    void PrintNginMemory(const std::vector<InputCase>& cases)
    {
        namespace JSON = NGIN::Serialization::JSON;
        using namespace NGIN;
        using namespace NGIN::Serialization;

        std::cout << "\nNGIN JSON document memory (bytes; arena allocations exclude source/vector allocations)\n";
        std::cout << std::left << std::setw(20) << "Input"
                  << std::right << std::setw(12) << "Source"
                  << std::setw(12) << "Nodes"
                  << std::setw(14) << "Owned used"
                  << std::setw(14) << "Owned cap"
                  << std::setw(14) << "Borrow cap"
                  << std::setw(14) << "Arena allocs"
                  << '\n';

        for (const auto& input: cases)
        {
            if (!input.valid)
                continue;

            CountingAllocator    ownedAllocator;
            const ParseResources ownedResources {
                    .allocator = Memory::PolyAllocatorRef {ownedAllocator},
            };
            auto owned = JSON::Parse(OwnedTextBuffer {input.source}, {}, {}, ownedResources);

            CountingAllocator    borrowedAllocator;
            const ParseResources borrowedResources {
                    .allocator = Memory::PolyAllocatorRef {borrowedAllocator},
            };
            ParseScratch scratch;
            auto         borrowed = JSON::ParseBorrowed(
                    BorrowedTextView {input.source}, scratch, {}, {}, borrowedResources);

            if (!owned || !borrowed)
                continue;

            std::cout << std::left << std::setw(20) << input.name
                      << std::right << std::setw(12) << input.source.size()
                      << std::setw(12) << owned.Value().NodeCount()
                      << std::setw(14) << owned.Value().MemoryUsed()
                      << std::setw(14) << owned.Value().MemoryCommitted()
                      << std::setw(14) << borrowed.Value().MemoryCommitted()
                      << std::setw(14) << ownedAllocator.allocationCount
                      << '\n';
        }
    }
}// namespace

int main()
{
    using namespace NGIN;
    using namespace NGIN::Serialization;
    namespace JSON = NGIN::Serialization::JSON;

    Benchmark::defaultConfig.iterations       = 200;
    Benchmark::defaultConfig.warmupIterations = 20;
    Benchmark::defaultConfig.keepRawTimings   = true;

    std::vector<InputCase> cases;
    cases.push_back({
            .name   = "tiny",
            .source = R"({"name":"NGIN","count":3,"active":true,"tags":["a","b","c"]})",
    });
    cases.push_back({
            .name = "tool-event",
            .source =
                    R"({"schemaVersion":"1.0","kind":"NGIN.ToolDriver.Event","runId":"bench","sequence":9007199254740993,"type":"metric","data":{"name":"compile.ms","value":12.5,"unit":"ms"}})",
    });
    cases.push_back({.name = "array-1KiB", .source = NumericArray(1024)});
    cases.push_back({.name = "array-100KiB", .source = NumericArray(100 * 1024)});
    cases.push_back({.name = "array-1MiB", .source = NumericArray(1024 * 1024)});
    cases.push_back({
            .name   = "escapes-16KiB",
            .source = std::string {"{\"text\":\""} + std::string(16 * 1024, '\\') + "n\"}",
    });
    cases.push_back({.name = "nested-64", .source = NestedArray(64)});
    cases.push_back({.name = "invalid-truncated", .source = R"({"items":[1,2,3})", .valid = false});

    if (!ValidateNginInputs(cases))
        return 1;

    std::vector<BatchScale> scales;
    ParseScratch            borrowedScratch;

    for (const auto& input: cases)
    {
        const auto  operations = BatchSize(input.source.size());
        const auto* inputPtr   = &input;

        RegisterBatched(
                scales,
                "JSON/NGIN owning/" + input.name,
                operations,
                [inputPtr](BenchmarkContext& context) {
                    auto result = JSON::Parse(OwnedTextBuffer {inputPtr->source});
                    context.doNotOptimize(result.HasValue());
                    if (result)
                        context.doNotOptimize(result.Value().NodeCount());
                });

        RegisterBatched(
                scales,
                "JSON/NGIN borrowed/" + input.name,
                operations,
                [inputPtr, &borrowedScratch](BenchmarkContext& context) {
                    auto result = JSON::ParseBorrowed(
                            BorrowedTextView {inputPtr->source}, borrowedScratch);
                    context.doNotOptimize(result.HasValue());
                    if (result)
                        context.doNotOptimize(result.Value().NodeCount());
                });

        RegisterBatched(
                scales,
                "JSON/NGIN in-situ/" + input.name,
                operations,
                [inputPtr](BenchmarkContext& context) {
                    auto result = JSON::ParseInSitu(MutableTextBuffer {inputPtr->source});
                    context.doNotOptimize(result.HasValue());
                    if (result)
                        context.doNotOptimize(result.Value().NodeCount());
                });
    }

    ParseScratch eventScratch;
    auto         eventHandler = [](const JSON::Event&) {
        return JSON::EventAction::Continue();
    };
    for (const auto caseIndex: {std::size_t {1}, std::size_t {3}})
    {
        const auto* input = &cases[caseIndex];
        RegisterBatched(
                scales,
                "JSON/NGIN events/" + input->name,
                BatchSize(input->source.size()),
                [input, &eventScratch, &eventHandler](BenchmarkContext& context) {
                    auto result = JSON::EventParser::ParseContiguous(
                            BorrowedTextView {input->source}, eventHandler, eventScratch);
                    context.doNotOptimize(result.HasValue());
                });
    }

    auto parsedForWrite = JSON::Parse(OwnedTextBuffer {cases[3].source});
    RegisterBatched(
            scales,
            "JSON/NGIN writer/array-100KiB",
            1,
            [&parsedForWrite](BenchmarkContext& context) {
                auto result = JSON::Writer::Write(parsedForWrite.Value());
                if (result)
                    context.doNotOptimize(result.Value().size());
            });

#if defined(NGIN_HAVE_SIMDJSON)
    std::vector<simdjson::padded_string> paddedCases;
    paddedCases.reserve(cases.size());
    for (const auto& input: cases)
        paddedCases.emplace_back(input.source);

    simdjson::dom::parser simdParser;
    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        auto document = simdParser.parse(paddedCases[index]);
        if ((document.error() == simdjson::SUCCESS) != cases[index].valid)
        {
            std::cerr << "simdjson preflight disagreed with expected validity for '"
                      << cases[index].name << "'.\n";
            return 1;
        }
    }

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        const auto operations = BatchSize(cases[index].source.size());
        RegisterBatched(
                scales,
                "JSON/simdjson reused/" + cases[index].name,
                operations,
                [index, &paddedCases, &simdParser](BenchmarkContext& context) {
                    auto document = simdParser.parse(paddedCases[index]);
                    context.doNotOptimize(document.error());
                });
    }
#endif

#if defined(NGIN_HAVE_RAPIDJSON)
    for (const auto& input: cases)
    {
        rapidjson::Document document;
        document.Parse(input.source.data(), input.source.size());
        if ((!document.HasParseError()) != input.valid)
        {
            std::cerr << "RapidJSON preflight disagreed with expected validity for '"
                      << input.name << "'.\n";
            return 1;
        }
    }

    for (const auto& input: cases)
    {
        const auto  operations = BatchSize(input.source.size());
        const auto* inputPtr   = &input;
        RegisterBatched(
                scales,
                "JSON/RapidJSON/" + input.name,
                operations,
                [inputPtr](BenchmarkContext& context) {
                    rapidjson::Document document;
                    document.Parse(inputPtr->source.data(), inputPtr->source.size());
                    context.doNotOptimize(document.HasParseError());
                    if (!document.HasParseError())
                        context.doNotOptimize(document.GetType());
                });
    }
#endif

    std::cout << "JSON parser comparison: Release, " << Benchmark::defaultConfig.iterations
              << " samples after " << Benchmark::defaultConfig.warmupIterations
              << " warmups; batched cases are normalized to time per document.\n";
    auto results = Benchmark::RunAll<Milliseconds>();
    NormalizePerDocument(results, scales);
    Benchmark::PrintSummaryTable(std::cout, results);
    PrintNginMemory(cases);
    return 0;
}
