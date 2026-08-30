/// @file FilePrimitives.cpp
/// @brief Focused tests for low-level file primitives.

#include <NGIN/IO/FileView.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace
{
    std::filesystem::path MakeTempFilePath()
    {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / ("ngin_base_file_" + std::to_string(tick) + ".txt");
    }

    std::string ToString(std::span<const NGIN::Byte> bytes)
    {
        std::string result;
        result.reserve(bytes.size());
        for (const NGIN::Byte byte: bytes)
        {
            result.push_back(std::to_integer<char>(byte));
        }
        return result;
    }
}// namespace

TEST_CASE("IO.FileView maps or buffers an existing file", "[IO][FileView]")
{
    const std::filesystem::path filePath = MakeTempFilePath();
    const std::string           content  = "ngin-base-view";

    {
        std::ofstream output(filePath, std::ios::binary);
        REQUIRE(output.good());
        output << content;
    }

    NGIN::IO::FileView fileView;
    const auto         openResult = fileView.Open(NGIN::IO::Path(filePath.string()));
    REQUIRE(openResult.has_value());
    CHECK(fileView.IsOpen());
    CHECK(fileView.Size() == content.size());
    CHECK(ToString(fileView.Data()) == content);

    fileView.Close();
    std::filesystem::remove(filePath);
}

#if defined(__linux__)
TEST_CASE("IO.FileView buffers a readable sysfs file that cannot be mapped", "[IO][FileView]")
{
    // sysfs reports a page-sized regular file while serving shorter generated
    // contents through read(2); its files do not support mmap(2). This exercises
    // the production fallback without a test-only switch.
    const std::filesystem::path filePath = "/sys/devices/system/cpu/online";
    std::error_code             existsError;
    if (!std::filesystem::exists(filePath, existsError) || existsError)
        SKIP("the Linux sysfs CPU topology file is unavailable");

    std::ifstream input(filePath, std::ios::binary);
    REQUIRE(input.good());
    std::string expected;
    char        character = '\0';
    while (input.get(character))
        expected.push_back(character);
    REQUIRE_FALSE(expected.empty());

    NGIN::IO::FileView                                       fileView;
    const NGIN::Utilities::Expected<void, NGIN::IO::IOError> openResult =
            fileView.Open(NGIN::IO::Path(filePath.string()));
    REQUIRE(openResult.has_value());
    CHECK(fileView.IsOpen());
    CHECK(fileView.Size() == expected.size());
    CHECK(ToString(fileView.Data()) == expected);
}
#endif
