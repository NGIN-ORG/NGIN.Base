#include <NGIN/Serialization/JSON/JsonParser.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace
{
    namespace fs = std::filesystem;
    using namespace NGIN::Serialization;

    [[nodiscard]] fs::path CorpusRoot()
    {
        return fs::path {__FILE__}.parent_path() / "Corpus";
    }

    [[nodiscard]] std::string Read(const fs::path& path)
    {
        std::ifstream input {path, std::ios::binary};
        if (!input)
            throw std::runtime_error("failed to open corpus fixture: " + path.string());
        return {std::istreambuf_iterator<char> {input}, std::istreambuf_iterator<char> {}};
    }
}

TEST_CASE("checked-in JSON corpus matches the strict default profile",
          "[serialization][json][corpus]")
{
    for (const auto& entry: fs::directory_iterator(CorpusRoot() / "json" / "valid"))
    {
        INFO(entry.path().filename().string());
        CHECK(NGIN::Serialization::JSON::Parse(OwnedTextBuffer {Read(entry.path())}));
    }
    for (const auto& entry: fs::directory_iterator(CorpusRoot() / "json" / "invalid"))
    {
        INFO(entry.path().filename().string());
        CHECK_FALSE(NGIN::Serialization::JSON::Parse(OwnedTextBuffer {Read(entry.path())}));
    }
}

TEST_CASE("checked-in XML corpus matches the secure semantic profile",
          "[serialization][xml][corpus]")
{
    for (const auto& entry: fs::directory_iterator(CorpusRoot() / "xml" / "valid"))
    {
        INFO(entry.path().filename().string());
        CHECK(NGIN::Serialization::XML::Parse(OwnedTextBuffer {Read(entry.path())}));
    }
    for (const auto& entry: fs::directory_iterator(CorpusRoot() / "xml" / "invalid"))
    {
        INFO(entry.path().filename().string());
        CHECK_FALSE(NGIN::Serialization::XML::Parse(OwnedTextBuffer {Read(entry.path())}));
    }
}
