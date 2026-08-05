#include <NGIN/Serialization/XML/XmlEventParser.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    constexpr std::size_t maxInputBytes = 1024 * 1024;
    if (size > maxInputBytes)
        return 0;

    NGIN::Serialization::ParseLimits limits;
    limits.maxInputBytes       = maxInputBytes;
    limits.maxTotalMemoryBytes = 8 * maxInputBytes;
    NGIN::Serialization::ParseScratch scratch;
    auto                              handler = [](const NGIN::Serialization::XML::Event&) {
        return NGIN::Serialization::XML::EventAction::Continue();
    };
    NGIN::Serialization::XML::IncrementalEventParser parser {handler, scratch, {}, limits};

    std::size_t offset = 0;
    while (offset < size)
    {
        const std::size_t width  = (std::min) (size - offset, 1 + static_cast<std::size_t>(data[offset] % 31));
        const auto        result = parser.Feed(std::string_view {
                reinterpret_cast<const char*>(data + offset), width});
        if (result.HasError())
            return 0;
        offset += width;
    }
    (void) parser.Finish();
    return 0;
}
