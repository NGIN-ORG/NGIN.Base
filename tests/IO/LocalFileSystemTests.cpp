#include <catch2/catch_test_macros.hpp>

#include <NGIN/IO/FileSystemUtilities.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>

#include <filesystem>
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
    std::filesystem::path MakeTempDir()
    {
        auto dir = std::filesystem::temp_directory_path() / ("ngin_base_fs_test_" + std::to_string(std::rand()));
        std::filesystem::create_directories(dir);
        return dir;
    }

#if !defined(_WIN32)
    int CreateUnixSocket(const std::filesystem::path& path)
    {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        REQUIRE(fd >= 0);

        ::sockaddr_un address {};
        address.sun_family = AF_UNIX;
        std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
        REQUIRE(::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
        return fd;
    }
#endif
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
    opts.populateInfo = true;
    auto enumerator = fs.Enumerate(root, opts);
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

    std::filesystem::remove_all(tempDir);
}

#if !defined(_WIN32)
TEST_CASE("IO.LocalFileSystem POSIX metadata and file types", "[IO][LocalFileSystem][posix]")
{
    NGIN::IO::LocalFileSystem fs;
    const auto                tempDir = MakeTempDir();
    const NGIN::IO::Path      root {tempDir.generic_string()};
    const auto                filePath = tempDir / "hello.txt";
    const auto                hardLinkPath = tempDir / "hello.link";
    const auto                symlinkPath = tempDir / "hello.sym";
    const auto                danglingSymlinkPath = tempDir / "dangling.sym";
    const auto                fifoPath = tempDir / "named.pipe";
    const auto                socketPath = tempDir / "service.sock";

    REQUIRE(NGIN::IO::WriteAllText(fs, NGIN::IO::Path {filePath.generic_string()}, "hello world").HasValue());
    std::filesystem::create_hard_link(filePath, hardLinkPath);
    std::filesystem::create_symlink(filePath.filename(), symlinkPath);
    std::filesystem::create_symlink("missing.txt", danglingSymlinkPath);
    REQUIRE(::mkfifo(fifoPath.c_str(), 0640) == 0);
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

    auto fileInfo = fs.GetInfo(NGIN::IO::Path {filePath.generic_string()}, noFollow);
    REQUIRE(fileInfo.HasValue());
    REQUIRE(fileInfo.ValueUnsafe().type == NGIN::IO::EntryType::File);
    REQUIRE(fileInfo.ValueUnsafe().ownership.valid);
    REQUIRE(fileInfo.ValueUnsafe().ownership.userId == static_cast<NGIN::UInt32>(::geteuid()));
    REQUIRE(fileInfo.ValueUnsafe().ownership.groupId == static_cast<NGIN::UInt32>(::getegid()));
    REQUIRE(fileInfo.ValueUnsafe().identity.valid);
    REQUIRE(fileInfo.ValueUnsafe().identity.hardLinkCount >= 2);
    REQUIRE(fileInfo.ValueUnsafe().permissions.nativeBits != 0);
    REQUIRE(fileInfo.ValueUnsafe().changed.valid);

    auto hardLinkInfo = fs.GetInfo(NGIN::IO::Path {hardLinkPath.generic_string()}, noFollow);
    REQUIRE(hardLinkInfo.HasValue());
    REQUIRE(hardLinkInfo.ValueUnsafe().identity.hardLinkCount >= 2);

    auto symlinkInfo = fs.GetInfo(NGIN::IO::Path {symlinkPath.generic_string()}, noFollow);
    REQUIRE(symlinkInfo.HasValue());
    REQUIRE(symlinkInfo.ValueUnsafe().type == NGIN::IO::EntryType::Symlink);
    REQUIRE(symlinkInfo.ValueUnsafe().exists);
    REQUIRE(symlinkInfo.ValueUnsafe().symlinkTargetExists);

    auto followedSymlinkInfo = fs.GetInfo(NGIN::IO::Path {symlinkPath.generic_string()}, follow);
    REQUIRE(followedSymlinkInfo.HasValue());
    REQUIRE(followedSymlinkInfo.ValueUnsafe().type == NGIN::IO::EntryType::File);
    REQUIRE(followedSymlinkInfo.ValueUnsafe().exists);

    auto danglingSymlinkInfo = fs.GetInfo(NGIN::IO::Path {danglingSymlinkPath.generic_string()}, noFollow);
    REQUIRE(danglingSymlinkInfo.HasValue());
    REQUIRE(danglingSymlinkInfo.ValueUnsafe().type == NGIN::IO::EntryType::Symlink);
    REQUIRE(danglingSymlinkInfo.ValueUnsafe().exists);
    REQUIRE_FALSE(danglingSymlinkInfo.ValueUnsafe().symlinkTargetExists);

    auto danglingFollowInfo = fs.GetInfo(NGIN::IO::Path {danglingSymlinkPath.generic_string()}, follow);
    REQUIRE(danglingFollowInfo.HasValue());
    REQUIRE(danglingFollowInfo.ValueUnsafe().type == NGIN::IO::EntryType::Symlink);
    REQUIRE(danglingFollowInfo.ValueUnsafe().exists);
    REQUIRE_FALSE(danglingFollowInfo.ValueUnsafe().symlinkTargetExists);

    auto fifoInfo = fs.GetInfo(NGIN::IO::Path {fifoPath.generic_string()}, noFollow);
    REQUIRE(fifoInfo.HasValue());
    REQUIRE(fifoInfo.ValueUnsafe().type == NGIN::IO::EntryType::Fifo);

    auto socketInfo = fs.GetInfo(NGIN::IO::Path {socketPath.generic_string()}, noFollow);
    REQUIRE(socketInfo.HasValue());
    REQUIRE(socketInfo.ValueUnsafe().type == NGIN::IO::EntryType::Socket);

    NGIN::IO::EnumerateOptions enumerateOptions;
    enumerateOptions.includeSymlinks = true;
    enumerateOptions.populateInfo = true;
    auto enumerator = fs.Enumerate(root, enumerateOptions);
    REQUIRE(enumerator.HasValue());

    bool sawFifo = false;
    bool sawSocket = false;
    bool sawSymlink = false;
    while (true)
    {
        auto next = enumerator.ValueUnsafe()->Next();
        REQUIRE(next.HasValue());
        if (!next.ValueUnsafe())
            break;

        const auto& entry = enumerator.ValueUnsafe()->Current();
        REQUIRE(entry.info.has_value());
        sawFifo = sawFifo || entry.type == NGIN::IO::EntryType::Fifo;
        sawSocket = sawSocket || entry.type == NGIN::IO::EntryType::Socket;
        sawSymlink = sawSymlink || entry.type == NGIN::IO::EntryType::Symlink;
    }

    REQUIRE(sawFifo);
    REQUIRE(sawSocket);
    REQUIRE(sawSymlink);

    ::close(socketFd);
    std::filesystem::remove_all(tempDir);
}
#endif
