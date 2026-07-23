#include <NGIN/Serialization/JSON/JsonEventParser.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size > 1024 * 1024)
        return 0;
    NGIN::Serialization::ParseScratch scratch;
    auto handler = [](const NGIN::Serialization::JSON::Event&) {
        return NGIN::Serialization::JSON::EventAction::Continue();
    };
    auto result = NGIN::Serialization::JSON::EventParser::ParseContiguous(
            NGIN::Serialization::BorrowedTextView {
                    std::string_view {reinterpret_cast<const char*>(data), size}},
            handler,
            scratch);
    (void)result;
    return 0;
}
