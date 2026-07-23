#include <NGIN/Serialization/JSON/JsonParser.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size > 1024 * 1024)
        return 0;
    NGIN::Serialization::ParseLimits limits;
    limits.maxInputBytes = 1024 * 1024;
    limits.maxTotalMemoryBytes = 8 * 1024 * 1024;
    auto result = NGIN::Serialization::JSON::Parse(
            NGIN::Serialization::OwnedTextBuffer {
                    std::string_view {reinterpret_cast<const char*>(data), size}},
            {},
            limits);
    (void)result;
    return 0;
}
