#include <NGIN/Serialization/JSON/JsonParser.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size > 256 * 1024)
        return 0;
    const std::string_view bytes {reinterpret_cast<const char*>(data), size};
    std::string json = "{\"value\":\"";
    json.append(bytes);
    json += "\"}";
    std::string xml = "<root value=\"";
    xml.append(bytes);
    xml += "\">";
    xml.append(bytes);
    xml += "</root>";
    (void)NGIN::Serialization::JSON::Parse(
            NGIN::Serialization::OwnedTextBuffer {json});
    (void)NGIN::Serialization::XML::Parse(
            NGIN::Serialization::OwnedTextBuffer {xml});
    return 0;
}
