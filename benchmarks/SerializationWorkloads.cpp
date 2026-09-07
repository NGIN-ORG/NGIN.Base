#include <NGIN/Serialization/JSON.hpp>
#include <NGIN/Serialization/XML.hpp>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace NGIN::Serialization;
    namespace JSON                = NGIN::Serialization::JSON;
    namespace XML                 = NGIN::Serialization::XML;
    volatile std::size_t consumed = 0;

    template<class Work>
    void Measure(const std::string& name, std::size_t repetitions, Work work)
    {
        std::vector<double> samples;
        for (int sample = 0; sample < 11; ++sample)
        {
            const auto  start = std::chrono::steady_clock::now();
            std::size_t count = 0;
            for (std::size_t i = 0; i < repetitions; ++i)
                count += work();
            const auto end = std::chrono::steady_clock::now();
            consumed       = count;
            if (sample >= 2)
                samples.push_back(std::chrono::duration<double, std::nano>(end - start).count() / repetitions);
        }
        std::sort(samples.begin(), samples.end());
        std::cout << name << ',' << samples[samples.size() / 2] << '\n';
    }

    std::string Object(std::size_t count)
    {
        std::string text = "{";
        for (std::size_t i = 0; i < count; ++i)
        {
            if (i)
                text += ',';
            text += "\"key" + std::to_string(i) + "\":" + std::to_string(i);
        }
        return text + '}';
    }

    std::string Array(std::size_t bytes)
    {
        std::string text = "[0";
        for (std::size_t i = 1; text.size() < bytes; ++i)
            text += ',' + std::to_string(i);
        return text + ']';
    }

    std::string Attributes(std::size_t count)
    {
        std::string text = "<root";
        for (std::size_t i = 0; i < count; ++i)
            text += " key" + std::to_string(i) + "=\"" + std::to_string(i) + "\"";
        return text + "/>";
    }

    std::string Elements(std::size_t bytes)
    {
        std::string text = "<root>";
        while (text.size() < bytes)
            text += "<item key=\"42\">value</item>";
        return text + "</root>";
    }


}// namespace

int main()
{
    using namespace NGIN::Serialization;
    namespace JSON = NGIN::Serialization::JSON;
    namespace XML  = NGIN::Serialization::XML;
    std::cout << std::fixed << std::setprecision(1) << "workload,value\n";
    const std::vector<std::pair<std::string, std::string>> jsonCases {
            {"tiny", R"({"name":"NGIN","count":3,"active":true,"tags":["a","b","c"]})"},
            {"object-8", Object(8)},
            {"object-32", Object(32)},
            {"object-1000", Object(1000)},
            {"object-8000", Object(8000)},
            {"array-100KiB", Array(100 * 1024)},
            {"array-1MiB", Array(1024 * 1024)},
            {"escapes", R"({"a":"line\nnext","b":"quote\"backslash\\","c":"\u20ac"})"},
    };
    for (const auto& [name, text]: jsonCases)
    {
        auto checked = JSON::Parse(text);
        if (!checked)
            throw std::runtime_error("Invalid JSON benchmark fixture");
        std::cout << "JSON/memory/" << name << ',' << checked->MemoryCommitted() << '\n';
        const std::size_t repeats = text.size() < 1024 ? 1000 : 1;
        Measure("JSON/parse/" + name, repeats, [&] {
            auto parsed = JSON::Parse(text);
            if (!parsed)
                throw std::runtime_error("JSON parse failed");
            return parsed->NodeCount();
        });
        if (auto object = checked->Root().TryObject(); object && name.starts_with("object-"))
        {
            const std::string key = "key" + std::to_string(object->Size() - 1);
            Measure("JSON/find-last/" + name, 10000, [&] { return object->Find(key).has_value(); });
            Measure("JSON/find-missing/" + name, 10000, [&] { return object->Find("missing").has_value(); });
        }
        if (name == "tiny" || name == "array-100KiB" || name == "object-1000")
        {
            Measure("JSON/chunks-4096/" + name, repeats, [&] {
                std::size_t                  count   = 0;
                auto                         handler = [&count](const JSON::Event&) { ++count; return JSON::EventAction::Continue(); };
                ParseScratch                 scratch;
                JSON::IncrementalEventParser parser {handler, scratch};
                for (std::size_t i = 0; i < text.size(); i += 4096)
                    if (parser.Feed(std::string_view(text).substr(i, 4096)).HasError())
                        throw std::runtime_error("JSON feed failed");
                if (!parser.Finish().IsComplete())
                    throw std::runtime_error("JSON finish failed");
                return count;
            });
        }
    }
    const std::vector<std::pair<std::string, std::string>> xmlCases {
            {"tiny", "<Package Name=\"NGIN.Base\"><Build Mode=\"Source\"/></Package>"},
            {"attributes-8", Attributes(8)},
            {"attributes-32", Attributes(32)},
            {"attributes-1000", Attributes(1000)},
            {"elements-100KiB", Elements(100 * 1024)},
            {"elements-1MiB", Elements(1024 * 1024)},
    };
    for (const auto& [name, text]: xmlCases)
    {
        auto checked = XML::Parse(text);
        if (!checked)
            throw std::runtime_error("Invalid XML benchmark fixture");
        std::cout << "XML/memory/" << name << ',' << checked->MemoryCommitted() << '\n';
        const std::size_t repeats = text.size() < 1024 ? 1000 : 1;
        Measure("XML/parse/" + name, repeats, [&] {
            auto parsed = XML::Parse(text);
            if (!parsed)
                throw std::runtime_error("XML parse failed");
            return parsed->NodeCount();
        });
        const auto root = checked->Root();
        if (name.starts_with("attributes-"))
        {
            const std::string key = "key" + std::to_string(root.Attributes().Size() - 1);
            Measure("XML/attribute-last/" + name, 10000, [&] { return root.Attribute(key).has_value(); });
        }
        if (name == "elements-100KiB")
            Measure("XML/iterate-children/" + name, 10, [&] {
                std::size_t count = 0;
                for (auto child: root.Children())
                    count += child.IsValid();
                return count;
            });
        if (name == "tiny" || name == "elements-100KiB" || name == "attributes-1000")
            Measure("XML/chunks-4096/" + name, repeats, [&] {
                std::size_t                 count   = 0;
                auto                        handler = [&count](const XML::Event&) { ++count; return XML::EventAction::Continue(); };
                ParseScratch                scratch;
                XML::IncrementalEventParser parser {handler, scratch};
                for (std::size_t i = 0; i < text.size(); i += 4096)
                    if (parser.Feed(std::string_view(text).substr(i, 4096)).HasError())
                        throw std::runtime_error("XML feed failed");
                if (!parser.Finish().IsComplete())
                    throw std::runtime_error("XML finish failed");
                return count;
            });
    }
}
