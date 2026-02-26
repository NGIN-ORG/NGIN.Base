#include <catch2/catch_test_macros.hpp>

#include <NGIN/IO/FileSystemUtilities.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>

#include <filesystem>
#include <cstdlib>
#include <string>

namespace
{
    std::filesystem::path MakeTempDir()
    {
        auto dir = std::filesystem::temp_directory_path() / ("ngin_base_fs_test_" + std::to_string(std::rand()));
        std::filesystem::create_directories(dir);
        return dir;
    }
}

TEST_CASE("IO.LocalFileSystem basic read write enumerate", "[IO][LocalFileSystem]")
{
    NGIN::IO::LocalFileSystem fs;
    const auto tempDir = MakeTempDir();
    const NGIN::IO::Path root{tempDir.generic_string()};
    const auto filePath = root.Join("hello.txt");

    REQUIRE(fs.CreateDirectories(root).HasValue());
    REQUIRE(NGIN::IO::WriteAllText(fs, filePath, "hello world").HasValue());

    auto text = NGIN::IO::ReadAllText(fs, filePath);
    REQUIRE(text.HasValue());
    REQUIRE(std::string(text.ValueUnsafe().Data(), text.ValueUnsafe().Size()) == "hello world");

    auto info = fs.GetInfo(filePath);
    REQUIRE(info.HasValue());
    REQUIRE(info.ValueUnsafe().exists);
    REQUIRE(info.ValueUnsafe().type == NGIN::IO::EntryType::File);

    NGIN::IO::EnumerateOptions opts;
    auto enumerator = fs.Enumerate(root, opts);
    REQUIRE(enumerator.HasValue());
    auto next = enumerator.ValueUnsafe()->Next();
    REQUIRE(next.HasValue());
    REQUIRE(next.ValueUnsafe());
    REQUIRE(enumerator.ValueUnsafe()->Current().name.View() == "hello.txt");

    std::filesystem::remove_all(tempDir);
}
