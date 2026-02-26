#include <catch2/catch_test_macros.hpp>

#include <NGIN/IO/Path.hpp>

TEST_CASE("IO.Path lexical helpers", "[IO][Path]")
{
    NGIN::IO::Path path{"a\\b/./c/file.txt"};
    path.Normalize();

    REQUIRE(path.View() == "a/b/c/file.txt");
    REQUIRE(path.Filename() == "file.txt");
    REQUIRE(path.Stem() == "file");
    REQUIRE(path.Extension() == "txt");
    REQUIRE(path.HasFilename());
    REQUIRE(path.HasExtension());
    REQUIRE(path.Parent().View() == "a/b/c");
}

TEST_CASE("IO.Path relative and replace extension", "[IO][Path]")
{
    NGIN::IO::Path base{"/A/B"};
    NGIN::IO::Path child{"/A/B/C/file.bin"};

    auto rel = child.LexicallyRelativeTo(base);
    REQUIRE(rel.View() == "C/file.bin");

    child.ReplaceExtension("txt");
    REQUIRE(child.View() == "/A/B/C/file.txt");
}
