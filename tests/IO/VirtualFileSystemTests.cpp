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
        const auto path        = tempDirectory.Value().Join("ngin_base_vfs_test_" + std::to_string(uniqueValue));

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
    REQUIRE(canonical.Value().View() == virtualFile.View());

    auto weaklyCanonical = vfs.WeaklyCanonical(virtualRoot.Join("missing/child.txt"));
    REQUIRE(weaklyCanonical.HasValue());
    REQUIRE(weaklyCanonical.Value().View() == virtualRoot.Join("missing/child.txt").LexicallyNormal().View());

    auto symlinkTarget = vfs.ReadSymlink(virtualLink);
    REQUIRE(symlinkTarget.HasValue());
    REQUIRE(symlinkTarget.Value().View() == virtualFile.View());

    auto sameFile = vfs.SameFile(virtualFile, virtualHard);
    REQUIRE(sameFile.HasValue());
    REQUIRE(sameFile.Value());

    auto tempDirectory = vfs.CreateTempDirectory(virtualRoot, "dir_");
    REQUIRE(tempDirectory.HasValue());
    auto tempFile = vfs.CreateTempFile(virtualRoot, "file_");
    REQUIRE(tempFile.HasValue());
    REQUIRE(vfs.Exists(tempDirectory.Value()).Value());
    REQUIRE(vfs.Exists(tempFile.Value()).Value());

    REQUIRE(NGIN::IO::WriteAllText(vfs, virtualReplace, "replacement").HasValue());
    REQUIRE(vfs.ReplaceFile(virtualReplace, virtualFile).HasValue());
    auto text = NGIN::IO::ReadAllText(vfs, virtualFile);
    REQUIRE(text.HasValue());
    REQUIRE(std::string(text.Value().Data(), text.Value().Size()) == "replacement");

    RemoveTempDir(backingFs, realRoot);
}

TEST_CASE("IO.VirtualFileSystem directory handles scope relative operations", "[IO][VirtualFileSystem]")
{
    NGIN::IO::LocalFileSystem backingFs;
    const auto                realRoot = MakeTempDir(backingFs);

    auto mount = std::make_shared<NGIN::IO::LocalMount>(realRoot, NGIN::IO::MountPoint {.virtualPrefix = NGIN::IO::Path {"/v"}});

    NGIN::IO::VirtualFileSystem vfs;
    vfs.AddMount(mount);

    const NGIN::IO::Path virtualRoot {"/v"};
    const auto           nestedDir = virtualRoot.Join("nested");
    const auto           childDir  = nestedDir.Join("child");

    REQUIRE(vfs.CreateDirectories(childDir).HasValue());
    REQUIRE(NGIN::IO::WriteAllText(vfs, nestedDir.Join("seed.txt"), "seed").HasValue());

    auto directory = vfs.OpenDirectory(nestedDir);
    REQUIRE(directory.HasValue());

    auto exists = directory->Exists(NGIN::IO::Path {"seed.txt"});
    REQUIRE(exists.HasValue());
    REQUIRE(exists.Value());

    auto child = directory->OpenDirectory(NGIN::IO::Path {"child"});
    REQUIRE(child.HasValue());

    NGIN::IO::FileOpenOptions openOptions;
    openOptions.access      = NGIN::IO::FileAccess::Write;
    openOptions.share       = NGIN::IO::FileShare::Read;
    openOptions.disposition = NGIN::IO::FileCreateDisposition::CreateAlways;

    auto openedFile = directory->OpenFile(NGIN::IO::Path {"from_handle.txt"}, openOptions);
    REQUIRE(openedFile.HasValue());

    const std::string payload     = "virtual-dir-handle";
    const auto        writeResult = openedFile->Write({reinterpret_cast<const NGIN::Byte*>(payload.data()), payload.size()});
    REQUIRE(writeResult.HasValue());
    REQUIRE(writeResult.Value() == payload.size());
    openedFile->Close();

    auto writtenText = NGIN::IO::ReadAllText(vfs, nestedDir.Join("from_handle.txt"));
    REQUIRE(writtenText.HasValue());
    REQUIRE(std::string(writtenText.Value().Data(), writtenText.Value().Size()) == payload);

    REQUIRE(directory->CreateDirectory(NGIN::IO::Path {"created"}).HasValue());
    REQUIRE(vfs.Exists(nestedDir.Join("created")).Value());
    REQUIRE(directory->RemoveDirectory(NGIN::IO::Path {"created"}).HasValue());
    REQUIRE_FALSE(vfs.Exists(nestedDir.Join("created")).Value());

    REQUIRE(directory->RemoveFile(NGIN::IO::Path {"from_handle.txt"}).HasValue());
    REQUIRE_FALSE(vfs.Exists(nestedDir.Join("from_handle.txt")).Value());

    auto escaped = directory->Exists(NGIN::IO::Path {"../outside.txt"});
    REQUIRE_FALSE(escaped.HasValue());
    REQUIRE(escaped.Error().code == NGIN::IO::IOErrorCode::InvalidPath);

    RemoveTempDir(backingFs, realRoot);
}
