#include <catch2/catch_test_macros.hpp>

#include <NGIN/IO/FileSystemUtilities.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>

#include <chrono>
#include <cstdlib>
#include <string>

#if !defined(_WIN32)
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace
{
    [[nodiscard]] std::string ToNativeString(const NGIN::IO::Path& path)
    {
        const auto native = path.ToNative();
        return std::string(native.Data(), native.Size());
    }

    [[nodiscard]] NGIN::IO::Path MakeTempDir(NGIN::IO::LocalFileSystem& fs)
    {
        auto tempDirectory = fs.TempDirectory();
        REQUIRE(tempDirectory.HasValue());

        const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto path        = tempDirectory.ValueUnsafe().Join("ngin_base_fs_test_" + std::to_string(uniqueValue));

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

#if !defined(_WIN32)
    int CreateUnixSocket(const NGIN::IO::Path& path)
    {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        REQUIRE(fd >= 0);

        ::sockaddr_un address {};
        address.sun_family = AF_UNIX;
        const auto native  = ToNativeString(path);
        std::strncpy(address.sun_path, native.c_str(), sizeof(address.sun_path) - 1);
        REQUIRE(::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
        return fd;
    }
#endif
}// namespace

TEST_CASE("IO.LocalFileSystem basic read write enumerate", "[IO][LocalFileSystem]")
{
    NGIN::IO::LocalFileSystem fs;
    const auto                root     = MakeTempDir(fs);
    const auto                filePath = root.Join("hello.txt");

    REQUIRE(NGIN::IO::WriteAllText(fs, filePath, "hello world").HasValue());

    auto text = NGIN::IO::ReadAllText(fs, filePath);
    REQUIRE(text.HasValue());
    REQUIRE(std::string(text.ValueUnsafe().Data(), text.ValueUnsafe().Size()) == "hello world");

    auto info = fs.GetInfo(filePath);
    REQUIRE(info.HasValue());
    REQUIRE(info.ValueUnsafe().exists);
    REQUIRE(info.ValueUnsafe().type == NGIN::IO::EntryType::File);

    NGIN::IO::EnumerateOptions options;
    options.populateInfo = true;
    auto enumerator      = fs.Enumerate(root, options);
    REQUIRE(enumerator.HasValue());

    auto next = enumerator.ValueUnsafe()->Next();
    REQUIRE(next.HasValue());
    REQUIRE(next.ValueUnsafe());
    REQUIRE(enumerator.ValueUnsafe()->Current().name.View() == "hello.txt");
    REQUIRE(enumerator.ValueUnsafe()->Current().info.has_value());
    REQUIRE(enumerator.ValueUnsafe()->Current().info->type == NGIN::IO::EntryType::File);

    const auto capabilities = fs.GetCapabilities();
    REQUIRE(capabilities.memoryMappedFiles);
    REQUIRE(capabilities.metadataNoFollow);

    RemoveTempDir(fs, root);
}

TEST_CASE("IO.LocalFileSystem directory handles scope relative operations", "[IO][LocalFileSystem]")
{
    NGIN::IO::LocalFileSystem fs;
    const auto                root      = MakeTempDir(fs);
    const auto                nestedDir = root.Join("nested");
    const auto                childDir  = nestedDir.Join("child");

    REQUIRE(fs.CreateDirectories(childDir).HasValue());
    REQUIRE(NGIN::IO::WriteAllText(fs, nestedDir.Join("seed.txt"), "seed").HasValue());

    auto directory = fs.OpenDirectory(nestedDir);
    REQUIRE(directory.HasValue());

    auto exists = directory.ValueUnsafe()->Exists(NGIN::IO::Path {"seed.txt"});
    REQUIRE(exists.HasValue());
    REQUIRE(exists.ValueUnsafe());

    auto info = directory.ValueUnsafe()->GetInfo(NGIN::IO::Path {"seed.txt"});
    REQUIRE(info.HasValue());
    REQUIRE(info.ValueUnsafe().type == NGIN::IO::EntryType::File);

    auto child = directory.ValueUnsafe()->OpenDirectory(NGIN::IO::Path {"child"});
    REQUIRE(child.HasValue());
    auto childInfo = child.ValueUnsafe()->GetInfo(NGIN::IO::Path {"."});
    REQUIRE(childInfo.HasValue());
    REQUIRE(childInfo.ValueUnsafe().type == NGIN::IO::EntryType::Directory);

    NGIN::IO::FileOpenOptions openOptions;
    openOptions.access      = NGIN::IO::FileAccess::Write;
    openOptions.share       = NGIN::IO::FileShare::Read;
    openOptions.disposition = NGIN::IO::FileCreateDisposition::CreateAlways;

    auto openedFile = directory.ValueUnsafe()->OpenFile(NGIN::IO::Path {"from_handle.txt"}, openOptions);
    REQUIRE(openedFile.HasValue());

    const std::string payload = "dir-handle";
    const auto        writeResult =
            openedFile.ValueUnsafe()->Write({reinterpret_cast<const NGIN::Byte*>(payload.data()), payload.size()});
    REQUIRE(writeResult.HasValue());
    REQUIRE(writeResult.ValueUnsafe() == payload.size());
    openedFile.ValueUnsafe()->Close();

    auto writtenText = NGIN::IO::ReadAllText(fs, nestedDir.Join("from_handle.txt"));
    REQUIRE(writtenText.HasValue());
    REQUIRE(std::string(writtenText.ValueUnsafe().Data(), writtenText.ValueUnsafe().Size()) == payload);

    REQUIRE(directory.ValueUnsafe()->CreateDirectory(NGIN::IO::Path {"created"}).HasValue());
    REQUIRE(fs.Exists(nestedDir.Join("created")).ValueUnsafe());
    REQUIRE(directory.ValueUnsafe()->RemoveDirectory(NGIN::IO::Path {"created"}).HasValue());
    REQUIRE_FALSE(fs.Exists(nestedDir.Join("created")).ValueUnsafe());

    REQUIRE(directory.ValueUnsafe()->RemoveFile(NGIN::IO::Path {"from_handle.txt"}).HasValue());
    REQUIRE_FALSE(fs.Exists(nestedDir.Join("from_handle.txt")).ValueUnsafe());

    auto escaped = directory.ValueUnsafe()->Exists(NGIN::IO::Path {"../outside.txt"});
    REQUIRE_FALSE(escaped.HasValue());
    REQUIRE(escaped.ErrorUnsafe().code == NGIN::IO::IOErrorCode::InvalidPath);

    RemoveTempDir(fs, root);
}

#if !defined(_WIN32)
TEST_CASE("IO.LocalFileSystem extended operations", "[IO][LocalFileSystem][posix]")
{
    NGIN::IO::LocalFileSystem fs;
    const auto                root           = MakeTempDir(fs);
    const auto                nestedDir      = root.Join("nested");
    const auto                filePath       = nestedDir.Join("file.txt");
    const auto                symlinkPath    = root.Join("file.sym");
    const auto                hardLinkPath   = root.Join("file.link");
    const auto                replacementSrc = root.Join("replacement.txt");
    const auto                tempBase       = root.Join("temps");

    REQUIRE(fs.CreateDirectories(nestedDir).HasValue());
    REQUIRE(NGIN::IO::WriteAllText(fs, filePath, "hello").HasValue());
    REQUIRE(fs.CreateSymlink(filePath, symlinkPath).HasValue());
    REQUIRE(fs.CreateHardLink(filePath, hardLinkPath).HasValue());

    auto absolute = fs.Absolute(NGIN::IO::Path {"file.txt"}, nestedDir);
    REQUIRE(absolute.HasValue());
    REQUIRE(absolute.ValueUnsafe().View() == filePath.View());

    auto canonical = fs.Canonical(symlinkPath);
    REQUIRE(canonical.HasValue());
    REQUIRE(canonical.ValueUnsafe().View() == filePath.View());

    auto weaklyCanonical = fs.WeaklyCanonical(root.Join("nested/missing/child.txt"));
    REQUIRE(weaklyCanonical.HasValue());
    REQUIRE(weaklyCanonical.ValueUnsafe().View() == root.Join("nested/missing/child.txt").LexicallyNormal().View());

    auto symlinkTarget = fs.ReadSymlink(symlinkPath);
    REQUIRE(symlinkTarget.HasValue());
    REQUIRE(symlinkTarget.ValueUnsafe().View() == filePath.View());

    auto sameFile = fs.SameFile(filePath, hardLinkPath);
    REQUIRE(sameFile.HasValue());
    REQUIRE(sameFile.ValueUnsafe());

    NGIN::IO::FilePermissions readOnlyPermissions;
    readOnlyPermissions.nativeBits = 0444;
    REQUIRE(fs.SetPermissions(filePath, readOnlyPermissions).HasValue());

    auto info = fs.GetInfo(filePath);
    REQUIRE(info.HasValue());
    REQUIRE((info.ValueUnsafe().permissions.nativeBits & 0222u) == 0);

    REQUIRE(fs.CreateDirectories(tempBase).HasValue());
    auto tempDirectory = fs.CreateTempDirectory(tempBase, "dir_");
    REQUIRE(tempDirectory.HasValue());
    auto tempFile = fs.CreateTempFile(tempBase, "file_");
    REQUIRE(tempFile.HasValue());
    REQUIRE(fs.Exists(tempDirectory.ValueUnsafe()).ValueUnsafe());
    REQUIRE(fs.Exists(tempFile.ValueUnsafe()).ValueUnsafe());

    REQUIRE(NGIN::IO::WriteAllText(fs, replacementSrc, "replacement").HasValue());
    REQUIRE(fs.ReplaceFile(replacementSrc, filePath).HasValue());
    auto replaced = NGIN::IO::ReadAllText(fs, filePath);
    REQUIRE(replaced.HasValue());
    REQUIRE(std::string(replaced.ValueUnsafe().Data(), replaced.ValueUnsafe().Size()) == "replacement");

    RemoveTempDir(fs, root);
}

TEST_CASE("IO.LocalFileSystem POSIX metadata and file types", "[IO][LocalFileSystem][posix]")
{
    NGIN::IO::LocalFileSystem fs;
    const auto                root                = MakeTempDir(fs);
    const auto                filePath            = root.Join("hello.txt");
    const auto                hardLinkPath        = root.Join("hello.link");
    const auto                symlinkPath         = root.Join("hello.sym");
    const auto                danglingSymlinkPath = root.Join("dangling.sym");
    const auto                fifoPath            = root.Join("named.pipe");
    const auto                socketPath          = root.Join("service.sock");

    REQUIRE(NGIN::IO::WriteAllText(fs, filePath, "hello world").HasValue());
    REQUIRE(::link(ToNativeString(filePath).c_str(), ToNativeString(hardLinkPath).c_str()) == 0);
    REQUIRE(::symlink("hello.txt", ToNativeString(symlinkPath).c_str()) == 0);
    REQUIRE(::symlink("missing.txt", ToNativeString(danglingSymlinkPath).c_str()) == 0);
    REQUIRE(::mkfifo(ToNativeString(fifoPath).c_str(), 0640) == 0);
    const int socketFd = CreateUnixSocket(socketPath);

    const auto capabilities = fs.GetCapabilities();
    REQUIRE(capabilities.posixModeBits);
    REQUIRE(capabilities.ownership);
    REQUIRE(capabilities.fileIdentity);
    REQUIRE(capabilities.fifos);
    REQUIRE(capabilities.sockets);

    NGIN::IO::MetadataOptions noFollow;
    noFollow.symlinkMode = NGIN::IO::SymlinkMode::DoNotFollow;

    NGIN::IO::MetadataOptions follow;
    follow.symlinkMode = NGIN::IO::SymlinkMode::Follow;

    auto fileInfo = fs.GetInfo(filePath, noFollow);
    REQUIRE(fileInfo.HasValue());
    REQUIRE(fileInfo.ValueUnsafe().type == NGIN::IO::EntryType::File);
    REQUIRE(fileInfo.ValueUnsafe().ownership.valid);
    REQUIRE(fileInfo.ValueUnsafe().ownership.userId == static_cast<NGIN::UInt32>(::geteuid()));
    REQUIRE(fileInfo.ValueUnsafe().ownership.groupId == static_cast<NGIN::UInt32>(::getegid()));
    REQUIRE(fileInfo.ValueUnsafe().identity.valid);
    REQUIRE(fileInfo.ValueUnsafe().identity.hardLinkCount >= 2);
    REQUIRE(fileInfo.ValueUnsafe().permissions.nativeBits != 0);
    REQUIRE(fileInfo.ValueUnsafe().changed.valid);

    auto hardLinkInfo = fs.GetInfo(hardLinkPath, noFollow);
    REQUIRE(hardLinkInfo.HasValue());
    REQUIRE(hardLinkInfo.ValueUnsafe().identity.hardLinkCount >= 2);

    auto symlinkInfo = fs.GetInfo(symlinkPath, noFollow);
    REQUIRE(symlinkInfo.HasValue());
    REQUIRE(symlinkInfo.ValueUnsafe().type == NGIN::IO::EntryType::Symlink);
    REQUIRE(symlinkInfo.ValueUnsafe().exists);
    REQUIRE(symlinkInfo.ValueUnsafe().symlinkTargetExists);

    auto followedSymlinkInfo = fs.GetInfo(symlinkPath, follow);
    REQUIRE(followedSymlinkInfo.HasValue());
    REQUIRE(followedSymlinkInfo.ValueUnsafe().type == NGIN::IO::EntryType::File);
    REQUIRE(followedSymlinkInfo.ValueUnsafe().exists);

    auto danglingSymlinkInfo = fs.GetInfo(danglingSymlinkPath, noFollow);
    REQUIRE(danglingSymlinkInfo.HasValue());
    REQUIRE(danglingSymlinkInfo.ValueUnsafe().type == NGIN::IO::EntryType::Symlink);
    REQUIRE(danglingSymlinkInfo.ValueUnsafe().exists);
    REQUIRE_FALSE(danglingSymlinkInfo.ValueUnsafe().symlinkTargetExists);

    auto danglingFollowInfo = fs.GetInfo(danglingSymlinkPath, follow);
    REQUIRE(danglingFollowInfo.HasValue());
    REQUIRE(danglingFollowInfo.ValueUnsafe().type == NGIN::IO::EntryType::Symlink);
    REQUIRE(danglingFollowInfo.ValueUnsafe().exists);
    REQUIRE_FALSE(danglingFollowInfo.ValueUnsafe().symlinkTargetExists);

    auto fifoInfo = fs.GetInfo(fifoPath, noFollow);
    REQUIRE(fifoInfo.HasValue());
    REQUIRE(fifoInfo.ValueUnsafe().type == NGIN::IO::EntryType::Fifo);

    auto socketInfo = fs.GetInfo(socketPath, noFollow);
    REQUIRE(socketInfo.HasValue());
    REQUIRE(socketInfo.ValueUnsafe().type == NGIN::IO::EntryType::Socket);

    NGIN::IO::EnumerateOptions enumerateOptions;
    enumerateOptions.includeSymlinks = true;
    enumerateOptions.populateInfo    = true;
    auto enumerator                  = fs.Enumerate(root, enumerateOptions);
    REQUIRE(enumerator.HasValue());

    bool sawFifo    = false;
    bool sawSocket  = false;
    bool sawSymlink = false;
    while (true)
    {
        auto next = enumerator.ValueUnsafe()->Next();
        REQUIRE(next.HasValue());
        if (!next.ValueUnsafe())
            break;

        const auto& entry = enumerator.ValueUnsafe()->Current();
        REQUIRE(entry.info.has_value());
        sawFifo    = sawFifo || entry.type == NGIN::IO::EntryType::Fifo;
        sawSocket  = sawSocket || entry.type == NGIN::IO::EntryType::Socket;
        sawSymlink = sawSymlink || entry.type == NGIN::IO::EntryType::Symlink;
    }

    REQUIRE(sawFifo);
    REQUIRE(sawSocket);
    REQUIRE(sawSymlink);

    ::close(socketFd);
    RemoveTempDir(fs, root);
}

TEST_CASE("IO.LocalFileSystem directory handles can read symlinks", "[IO][LocalFileSystem][posix]")
{
    NGIN::IO::LocalFileSystem fs;
    const auto                root      = MakeTempDir(fs);
    const auto                nestedDir = root.Join("nested");
    const auto                target    = nestedDir.Join("target.txt");
    const auto                linkPath  = nestedDir.Join("target.sym");

    REQUIRE(fs.CreateDirectories(nestedDir).HasValue());
    REQUIRE(NGIN::IO::WriteAllText(fs, target, "hello").HasValue());
    REQUIRE(::symlink("target.txt", ToNativeString(linkPath).c_str()) == 0);

    auto directory = fs.OpenDirectory(nestedDir);
    REQUIRE(directory.HasValue());

    auto targetPath = directory.ValueUnsafe()->ReadSymlink(NGIN::IO::Path {"target.sym"});
    REQUIRE(targetPath.HasValue());
    REQUIRE(targetPath.ValueUnsafe().View() == "target.txt");

    RemoveTempDir(fs, root);
}
#endif
