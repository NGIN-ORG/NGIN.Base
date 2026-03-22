#include <catch2/catch_test_macros.hpp>

#include <NGIN/IO/FileSystemUtilities.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>
#include <NGIN/IO/VirtualFileSystem.hpp>

#include <chrono>
#include <memory>
#include <string>

namespace
{
    [[nodiscard]] NGIN::IO::Path MakeTempDir(NGIN::IO::LocalFileSystem& fs)
    {
        auto tempDirectory = fs.TempDirectory();
        REQUIRE(tempDirectory.HasValue());

        const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto path        = tempDirectory.ValueUnsafe().Join("ngin_base_vfs_test_" + std::to_string(uniqueValue));

        REQUIRE(fs.CreateDirectories(path).HasValue());
        return path;
    }

    void RemoveTempDir(NGIN::IO::LocalFileSystem& fs, const NGIN::IO::Path& path)
    {
        NGIN::IO::RemoveOptions options;
        options.recursive     = true;
        options.ignoreMissing = true;
        REQUIRE(fs.RemoveDirectory(path, options).HasValue());
    }
}// namespace

TEST_CASE("IO.VirtualFileSystem forwards path-returning operations", "[IO][VirtualFileSystem]")
{
    NGIN::IO::LocalFileSystem backingFs;
    const auto                realRoot = MakeTempDir(backingFs);

    auto mount = std::make_shared<NGIN::IO::LocalMount>(realRoot, NGIN::IO::MountPoint {.virtualPrefix = NGIN::IO::Path {"/v"}});

    NGIN::IO::VirtualFileSystem vfs;
    vfs.AddMount(mount);

    const NGIN::IO::Path virtualRoot {"/v"};
    const auto           virtualFile    = virtualRoot.Join("hello.txt");
    const auto           virtualLink    = virtualRoot.Join("hello.sym");
    const auto           virtualHard    = virtualRoot.Join("hello.link");
    const auto           virtualReplace = virtualRoot.Join("replacement.txt");

    REQUIRE(NGIN::IO::WriteAllText(vfs, virtualFile, "hello").HasValue());
    REQUIRE(vfs.CreateSymlink(virtualFile, virtualLink).HasValue());
    REQUIRE(vfs.CreateHardLink(virtualFile, virtualHard).HasValue());

    auto canonical = vfs.Canonical(virtualLink);
    REQUIRE(canonical.HasValue());
    REQUIRE(canonical.ValueUnsafe().View() == virtualFile.View());

    auto weaklyCanonical = vfs.WeaklyCanonical(virtualRoot.Join("missing/child.txt"));
    REQUIRE(weaklyCanonical.HasValue());
    REQUIRE(weaklyCanonical.ValueUnsafe().View() == virtualRoot.Join("missing/child.txt").LexicallyNormal().View());

    auto symlinkTarget = vfs.ReadSymlink(virtualLink);
    REQUIRE(symlinkTarget.HasValue());
    REQUIRE(symlinkTarget.ValueUnsafe().View() == virtualFile.View());

    auto sameFile = vfs.SameFile(virtualFile, virtualHard);
    REQUIRE(sameFile.HasValue());
    REQUIRE(sameFile.ValueUnsafe());

    auto tempDirectory = vfs.CreateTempDirectory(virtualRoot, "dir_");
    REQUIRE(tempDirectory.HasValue());
    auto tempFile = vfs.CreateTempFile(virtualRoot, "file_");
    REQUIRE(tempFile.HasValue());
    REQUIRE(vfs.Exists(tempDirectory.ValueUnsafe()).ValueUnsafe());
    REQUIRE(vfs.Exists(tempFile.ValueUnsafe()).ValueUnsafe());

    REQUIRE(NGIN::IO::WriteAllText(vfs, virtualReplace, "replacement").HasValue());
    REQUIRE(vfs.ReplaceFile(virtualReplace, virtualFile).HasValue());
    auto text = NGIN::IO::ReadAllText(vfs, virtualFile);
    REQUIRE(text.HasValue());
    REQUIRE(std::string(text.ValueUnsafe().Data(), text.ValueUnsafe().Size()) == "replacement");

    RemoveTempDir(backingFs, realRoot);
}
