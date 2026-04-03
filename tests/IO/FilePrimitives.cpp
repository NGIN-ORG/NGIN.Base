/// @file FilePrimitives.cpp
/// @brief Focused tests for low-level file primitives.

#include <NGIN/IO/File.hpp>
#include <NGIN/IO/FileView.hpp>

#include <array>
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

TEST_CASE("IO.File reads an existing file", "[IO][File]")
{
    const std::filesystem::path filePath = MakeTempFilePath();
    const std::string           content  = "ngin-base-file";

    {
        std::ofstream output(filePath, std::ios::binary);
        REQUIRE(output.good());
        output << content;
    }

    NGIN::IO::File file;
    const auto     openResult = file.Open(NGIN::IO::Path(filePath.string()), NGIN::IO::File::OpenMode::Read);
    REQUIRE(openResult.HasValue());

    const auto sizeResult = file.Size();
    REQUIRE(sizeResult.HasValue());
    CHECK(sizeResult.Value() == content.size());

    std::array<NGIN::Byte, 32> buffer {};
    const auto                 readResult = file.Read(std::span<NGIN::Byte>(buffer.data(), content.size()));
    REQUIRE(readResult.HasValue());
    CHECK(readResult.Value() == content.size());
    CHECK(ToString(std::span<const NGIN::Byte>(buffer.data(), content.size())) == content);

    file.Close();
    std::filesystem::remove(filePath);
}

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
    REQUIRE(openResult.HasValue());
    CHECK(fileView.IsOpen());
    CHECK(fileView.Size() == content.size());
    CHECK(ToString(fileView.Data()) == content);

    fileView.Close();
    std::filesystem::remove(filePath);
}
