#include <catch2/catch_test_macros.hpp>

#include <NGIN/IO/Path.hpp>

TEST_CASE("IO.Path empty and root classification", "[IO][Path]")
{
    const NGIN::IO::Path empty {};
    REQUIRE(empty.IsEmpty());
    REQUIRE(empty.IsRelative());
    REQUIRE_FALSE(empty.IsAbsolute());
    REQUIRE_FALSE(empty.IsRoot());
    REQUIRE(empty.View().empty());

    const NGIN::IO::Path posixRoot {"/"};
    REQUIRE_FALSE(posixRoot.IsEmpty());
    REQUIRE(posixRoot.IsAbsolute());
    REQUIRE_FALSE(posixRoot.IsRelative());
    REQUIRE(posixRoot.IsRoot());
    REQUIRE_FALSE(posixRoot.HasFilename());
    REQUIRE(posixRoot.Filename().empty());
    REQUIRE(posixRoot.Parent().View() == "/");

    const NGIN::IO::Path driveRelative {"C:folder/file.txt"};
    REQUIRE_FALSE(driveRelative.IsAbsolute());
    REQUIRE(driveRelative.IsRelative());
    REQUIRE_FALSE(driveRelative.IsRoot());

    const NGIN::IO::Path driveAbsolute {"C:/folder/file.txt"};
    REQUIRE(driveAbsolute.IsAbsolute());
    REQUIRE_FALSE(driveAbsolute.IsRelative());
    REQUIRE_FALSE(driveAbsolute.IsRoot());

    const NGIN::IO::Path driveRoot {"C:/"};
    REQUIRE(driveRoot.IsAbsolute());
    REQUIRE(driveRoot.IsRoot());
    REQUIRE(driveRoot.Parent().View() == "C:/");

    const NGIN::IO::Path absoluteSingleSegment {"/alpha"};
    REQUIRE(absoluteSingleSegment.Parent().View() == "/");

    const NGIN::IO::Path driveSingleSegment {"C:/alpha"};
    REQUIRE(driveSingleSegment.Parent().View() == "C:/");
}

TEST_CASE("IO.Path normalization handles separators and dot segments", "[IO][Path]")
{
    NGIN::IO::Path mixed {"a\\b/./c/file.txt"};
    mixed.Normalize();
    REQUIRE(mixed.View() == "a/b/c/file.txt");

    NGIN::IO::Path relative {"./alpha//beta/../gamma///"};
    relative.Normalize();
    REQUIRE(relative.View() == "alpha/gamma");

    NGIN::IO::Path leadingParents {"../../a/./b/.."};
    leadingParents.Normalize();
    REQUIRE(leadingParents.View() == "../../a");

    NGIN::IO::Path absolute {"/alpha//beta/../gamma/."};
    absolute.Normalize();
    REQUIRE(absolute.View() == "/alpha/gamma");

    NGIN::IO::Path rootedParent {"/../alpha"};
    rootedParent.Normalize();
    REQUIRE(rootedParent.View() == "/alpha");

    NGIN::IO::Path drivePath {"C:\\alpha\\.\\beta\\..\\gamma"};
    drivePath.Normalize();
    REQUIRE(drivePath.View() == "C:/alpha/gamma");
}

TEST_CASE("IO.Path decomposition covers filenames stems and extensions", "[IO][Path]")
{
    const NGIN::IO::Path regular {"a/b/c/file.txt"};
    REQUIRE(regular.Filename() == "file.txt");
    REQUIRE(regular.Stem() == "file");
    REQUIRE(regular.Extension() == "txt");
    REQUIRE(regular.HasFilename());
    REQUIRE(regular.HasExtension());
    REQUIRE(regular.Parent().View() == "a/b/c");

    const NGIN::IO::Path hidden {"config/.gitignore"};
    REQUIRE(hidden.Filename() == ".gitignore");
    REQUIRE(hidden.Stem() == ".gitignore");
    REQUIRE(hidden.Extension().empty());
    REQUIRE_FALSE(hidden.HasExtension());
    REQUIRE(hidden.Parent().View() == "config");

    const NGIN::IO::Path multiDot {"archive.tar.gz"};
    REQUIRE(multiDot.Filename() == "archive.tar.gz");
    REQUIRE(multiDot.Stem() == "archive.tar");
    REQUIRE(multiDot.Extension() == "gz");

    const NGIN::IO::Path trailingSlash {"a/b/c/"};
    REQUIRE(trailingSlash.HasFilename());
    REQUIRE(trailingSlash.Filename() == "c");
    REQUIRE(trailingSlash.Parent().View() == "a/b");

    const NGIN::IO::Path singleSegment {"file"};
    REQUIRE(singleSegment.Parent().IsEmpty());
}

TEST_CASE("IO.Path lexical relative handles matches mismatches and identical paths", "[IO][Path]")
{
    const NGIN::IO::Path base {"/A/B"};
    const NGIN::IO::Path child {"/A/B/C/file.bin"};
    const NGIN::IO::Path sibling {"/A/D/file.bin"};
    const NGIN::IO::Path same {"/A/B"};
    const NGIN::IO::Path relativeBase {"A/B"};

    const auto childRelative = child.LexicallyRelativeTo(base);
    REQUIRE(childRelative.View() == "C/file.bin");

    const auto siblingRelative = sibling.LexicallyRelativeTo(base);
    REQUIRE(siblingRelative.View() == "../D/file.bin");

    const auto sameRelative = same.LexicallyRelativeTo(base);
    REQUIRE(sameRelative.View() == ".");

    const auto mismatchedKind = child.LexicallyRelativeTo(relativeBase);
    REQUIRE(mismatchedKind.View() == "/A/B/C/file.bin");

    const NGIN::IO::Path otherDrive {"D:/A/B/C"};
    const auto           driveMismatch = child.LexicallyRelativeTo(otherDrive);
    REQUIRE(driveMismatch.View() == "/A/B/C/file.bin");
}

TEST_CASE("IO.Path prefix and suffix helpers use current lexical semantics", "[IO][Path]")
{
    const NGIN::IO::Path path {"alpha/beta/gamma/file.txt"};

    REQUIRE(path.StartsWith(NGIN::IO::Path {"alpha"}));
    REQUIRE(path.StartsWith(NGIN::IO::Path {"alpha/beta"}));
    REQUIRE(path.StartsWith(NGIN::IO::Path {"alpha/beta/"}));
    REQUIRE_FALSE(path.StartsWith(NGIN::IO::Path {"alp"}));
    REQUIRE_FALSE(path.StartsWith(NGIN::IO::Path {"alpha/bet"}));

    REQUIRE(path.EndsWith(NGIN::IO::Path {"file.txt"}));
    REQUIRE(path.EndsWith(NGIN::IO::Path {"gamma/file.txt"}));
    REQUIRE(path.EndsWith(NGIN::IO::Path {"txt"}));
    REQUIRE_FALSE(path.EndsWith(NGIN::IO::Path {"gamma/file.tx"}));
}

TEST_CASE("IO.Path mutation helpers join append replace extension and remove filename", "[IO][Path]")
{
    const NGIN::IO::Path base {"/A/B"};
    const auto           joined = base.Join("C/file.bin");
    REQUIRE(joined.View() == "/A/B/C/file.bin");

    NGIN::IO::Path appended {"alpha"};
    appended.Append("beta");
    appended.Append("gamma.txt");
    REQUIRE(appended.View() == "alpha/beta/gamma.txt");

    appended.ReplaceExtension("log");
    REQUIRE(appended.View() == "alpha/beta/gamma.log");

    appended.ReplaceExtension(".json");
    REQUIRE(appended.View() == "alpha/beta/gamma.json");

    NGIN::IO::Path hidden {"config/.gitignore"};
    hidden.ReplaceExtension("bak");
    REQUIRE(hidden.View() == "config/.gitignore.bak");

    NGIN::IO::Path removeFilename {"alpha/beta/gamma.txt"};
    removeFilename.RemoveFilename();
    REQUIRE(removeFilename.View() == "alpha/beta");

    NGIN::IO::Path root {"/"};
    root.RemoveFilename();
    REQUIRE(root.View() == "/");
}

TEST_CASE("IO.Path native conversion preserves lexical form", "[IO][Path]")
{
    const auto fromNative = NGIN::IO::Path::FromNative("alpha\\beta/gamma.txt");
    REQUIRE(fromNative.View() == "alpha\\beta/gamma.txt");

    const auto native = NGIN::IO::Path {"alpha/beta/gamma.txt"}.ToNative();
#if defined(_WIN32)
    REQUIRE(std::string_view {native.Data(), native.Size()} == "alpha\\beta\\gamma.txt");
#else
    REQUIRE(std::string_view {native.Data(), native.Size()} == "alpha/beta/gamma.txt");
#endif
}
