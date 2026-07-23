#include <NGIN/Benchmark.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Serialization/XML/XmlEventParser.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>
#include <NGIN/Serialization/XML/XmlWriter.hpp>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#if defined(NGIN_HAVE_PUGIXML)
#include <pugixml.hpp>
#endif

#if defined(NGIN_HAVE_TINYXML2)
#include <tinyxml2.h>
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

    [[nodiscard]] std::string Elements(std::size_t approximateBytes)
    {
        std::string result = "<Project Name=\"Bench\"><Items>";
        for (std::size_t index = 0; result.size() < approximateBytes; ++index)
            result += "<Item Id=\"" + std::to_string(index) + "\" Enabled=\"true\"/>";
        result += "</Items></Project>";
        return result;
    }

    [[nodiscard]] std::string EntityHeavy(std::size_t count)
    {
        std::string result = "<root value=\"";
        for (std::size_t index = 0; index < count; ++index)
            result += "&amp;";
        result += "\">text</root>";
        return result;
    }

    [[nodiscard]] std::string NestedElements(std::size_t depth)
    {
        std::string result;
        for (std::size_t index = 0; index < depth; ++index)
            result += "<n>";
        result += "value";
        for (std::size_t index = 0; index < depth; ++index)
            result += "</n>";
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
        namespace XML = NGIN::Serialization::XML;
        using namespace NGIN::Serialization;

        for (const auto& input: cases)
        {
            auto result = XML::Parse(OwnedTextBuffer {input.source});
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
        namespace XML = NGIN::Serialization::XML;
        using namespace NGIN;
        using namespace NGIN::Serialization;

        std::cout << "\nNGIN XML document memory (bytes; arena allocations exclude source/vector allocations)\n";
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
            auto owned = XML::Parse(OwnedTextBuffer {input.source}, {}, {}, ownedResources);

            CountingAllocator    borrowedAllocator;
            const ParseResources borrowedResources {
                    .allocator = Memory::PolyAllocatorRef {borrowedAllocator},
            };
            ParseScratch scratch;
            auto         borrowed = XML::ParseBorrowed(
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
    namespace XML = NGIN::Serialization::XML;

    Benchmark::defaultConfig.iterations       = 200;
    Benchmark::defaultConfig.warmupIterations = 20;
    Benchmark::defaultConfig.keepRawTimings   = true;

    std::vector<InputCase> cases;
    cases.push_back({
            .name   = "package-manifest",
            .source = R"(<?xml version="1.0"?><Package Name="NGIN.Base"><Build Mode="Source"/></Package>)",
    });
    cases.push_back({
            .name = "project-manifest",
            .source =
                    R"(<Project Name="Hello.Hosted"><Application><Build><Sources><Source Path="src/main.cpp"/></Sources></Build></Application></Project>)",
    });
    cases.push_back({.name = "elements-1KiB", .source = Elements(1024)});
    cases.push_back({.name = "elements-100KiB", .source = Elements(100 * 1024)});
    cases.push_back({.name = "elements-1MiB", .source = Elements(1024 * 1024)});
    cases.push_back({.name = "entities-20KiB", .source = EntityHeavy(4096)});
    cases.push_back({.name = "nested-64", .source = NestedElements(64)});
    cases.push_back({.name = "invalid-mismatch", .source = "<root><item></root>", .valid = false});

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
                "XML/NGIN owning/" + input.name,
                operations,
                [inputPtr](BenchmarkContext& context) {
                    auto result = XML::Parse(OwnedTextBuffer {inputPtr->source});
                    context.doNotOptimize(result.HasValue());
                    if (result)
                        context.doNotOptimize(result.Value().NodeCount());
                });

        RegisterBatched(
                scales,
                "XML/NGIN borrowed/" + input.name,
                operations,
                [inputPtr, &borrowedScratch](BenchmarkContext& context) {
                    auto result = XML::ParseBorrowed(
                            BorrowedTextView {inputPtr->source}, borrowedScratch);
                    context.doNotOptimize(result.HasValue());
                    if (result)
                        context.doNotOptimize(result.Value().NodeCount());
                });
    }

    ParseScratch eventScratch;
    auto         eventHandler = [](const XML::Event&) {
        return XML::EventAction::Continue();
    };
    for (const auto caseIndex: {std::size_t {1}, std::size_t {3}})
    {
        const auto* input = &cases[caseIndex];
        RegisterBatched(
                scales,
                "XML/NGIN events/" + input->name,
                BatchSize(input->source.size()),
                [input, &eventScratch, &eventHandler](BenchmarkContext& context) {
                    auto result = XML::EventParser::ParseContiguous(
                            BorrowedTextView {input->source}, eventHandler, eventScratch);
                    context.doNotOptimize(result.HasValue());
                });
    }

    for (const auto caseIndex: {std::size_t {1}, std::size_t {3}})
    {
        const auto* input = &cases[caseIndex];
        RegisterBatched(
                scales,
                "XML/NGIN syntax/" + input->name,
                BatchSize(input->source.size()),
                [input](BenchmarkContext& context) {
                    auto result = XML::ParseSyntax(OwnedTextBuffer {input->source});
                    context.doNotOptimize(result.HasValue());
                });
    }

    auto parsedForWrite = XML::Parse(OwnedTextBuffer {cases[3].source});
    RegisterBatched(
            scales,
            "XML/NGIN writer/elements-100KiB",
            1,
            [&parsedForWrite](BenchmarkContext& context) {
                auto result = XML::Writer::Write(parsedForWrite.Value());
                if (result)
                    context.doNotOptimize(result.Value().size());
            });

#if defined(NGIN_HAVE_PUGIXML)
    for (const auto& input: cases)
    {
        pugi::xml_document document;
        const auto         result = document.load_buffer(input.source.data(), input.source.size());
        if ((result.status == pugi::status_ok) != input.valid)
        {
            std::cerr << "pugixml preflight disagreed with expected validity for '"
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
                "XML/pugixml/" + input.name,
                operations,
                [inputPtr](BenchmarkContext& context) {
                    pugi::xml_document document;
                    const auto         result = document.load_buffer(
                            inputPtr->source.data(), inputPtr->source.size());
                    context.doNotOptimize(result.status);
                });
    }
#endif

#if defined(NGIN_HAVE_TINYXML2)
    for (const auto& input: cases)
    {
        tinyxml2::XMLDocument document;
        const auto            result = document.Parse(input.source.data(), input.source.size());
        if ((result == tinyxml2::XML_SUCCESS) != input.valid)
        {
            std::cerr << "TinyXML2 preflight disagreed with expected validity for '"
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
                "XML/TinyXML2/" + input.name,
                operations,
                [inputPtr](BenchmarkContext& context) {
                    tinyxml2::XMLDocument document;
                    const auto            result = document.Parse(
                            inputPtr->source.data(), inputPtr->source.size());
                    context.doNotOptimize(result);
                });
    }
#endif

    std::cout << "XML parser comparison: Release, " << Benchmark::defaultConfig.iterations
              << " samples after " << Benchmark::defaultConfig.warmupIterations
              << " warmups; batched cases are normalized to time per document.\n";
    auto results = Benchmark::RunAll<Milliseconds>();
    NormalizePerDocument(results, scales);
    Benchmark::PrintSummaryTable(std::cout, results);
    PrintNginMemory(cases);
    return 0;
}
