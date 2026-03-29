#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/IO/FileSystemDriver.hpp>
#include <NGIN/IO/FileSystemUtilities.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>

#include <array>
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
        const auto path        = tempDirectory.Value().Join("ngin_base_fs_test_" + std::to_string(uniqueValue));

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

    template<typename T>
    auto RunAsyncTask(NGIN::IO::AsyncTask<T>& task, NGIN::Async::TaskContext& ctx) -> NGIN::Async::TaskOutcome<T, NGIN::IO::IOError>
    {
        task.Schedule(ctx);
        return task.Get();
    }

    auto RunAsyncTask(NGIN::IO::AsyncTaskVoid& task, NGIN::Async::TaskContext& ctx) -> NGIN::Async::TaskOutcome<void, NGIN::IO::IOError>
    {
        task.Schedule(ctx);
        return task.Get();
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
    REQUIRE(std::string(text.Value().Data(), text.Value().Size()) == "hello world");

    auto info = fs.GetInfo(filePath);
    REQUIRE(info.HasValue());
    REQUIRE(info.Value().exists);
    REQUIRE(info.Value().type == NGIN::IO::EntryType::File);

    NGIN::IO::EnumerateOptions options;
    options.populateInfo = true;
    auto enumerator      = fs.Enumerate(root, options);
    REQUIRE(enumerator.HasValue());

    auto next = enumerator->Next();
    REQUIRE(next.HasValue());
    REQUIRE(next.Value());
    REQUIRE(enumerator->Current().name.View() == "hello.txt");
    REQUIRE(enumerator->Current().info.has_value());
    REQUIRE(enumerator->Current().info->type == NGIN::IO::EntryType::File);

    const auto capabilities = fs.GetCapabilities();
    REQUIRE(capabilities.memoryMappedFiles);
    REQUIRE(capabilities.metadataNoFollow);

    RemoveTempDir(fs, root);
}

TEST_CASE("IO.LocalFileSystem async file operations use value handles", "[IO][LocalFileSystem][Async]")
{
    NGIN::IO::LocalFileSystem fs;
    const auto                root     = MakeTempDir(fs);
    const auto                filePath = root.Join("async.bin");

    const std::string payload = "async local filesystem payload";
    REQUIRE(NGIN::IO::WriteAllText(fs, filePath, payload).HasValue());

    NGIN::IO::FileSystemDriver driver;
    auto                       ctx = driver.MakeTaskContext();

    NGIN::IO::FileOpenOptions readOptions;
    readOptions.access      = NGIN::IO::FileAccess::Read;
    readOptions.disposition = NGIN::IO::FileCreateDisposition::OpenExisting;

    auto openTask = fs.OpenFileAsync(ctx, filePath, readOptions);
    auto opened   = RunAsyncTask(openTask, ctx);
    REQUIRE(opened.Succeeded());
    auto file = std::move(opened.Value());
    REQUIRE(file.IsValid());
    REQUIRE(file.IsOpen());

    std::array<NGIN::Byte, 64> buffer {};
    auto                       readTask   = file.ReadAsync(ctx, std::span<NGIN::Byte>(buffer.data(), buffer.size()));
    auto                       readResult = RunAsyncTask(readTask, ctx);
    REQUIRE(readResult.Succeeded());
    const auto readCount = readResult.Value();
    REQUIRE(readCount == payload.size());
    REQUIRE(std::string(reinterpret_cast<const char*>(buffer.data()), readCount) == payload);

    auto closeTask   = file.CloseAsync(ctx);
    auto closeResult = RunAsyncTask(closeTask, ctx);
    REQUIRE(closeResult.Succeeded());
    REQUIRE_FALSE(file.IsOpen());

    auto emptyHandle = NGIN::IO::AsyncFileHandle {};
    auto emptyRead   = emptyHandle.ReadAsync(ctx, std::span<NGIN::Byte>(buffer.data(), 1));
    auto emptyResult = RunAsyncTask(emptyRead, ctx);
    REQUIRE(emptyResult.IsDomainError());
    REQUIRE(emptyResult.DomainError().code == NGIN::IO::IOErrorCode::InvalidArgument);

    RemoveTempDir(fs, root);
}

TEST_CASE("IO.LocalFileSystem async utility helpers work through FileSystemDriver", "[IO][LocalFileSystem][Async]")
{
    NGIN::IO::LocalFileSystem fs;
    const auto                root    = MakeTempDir(fs);
    const auto                source  = root.Join("source.bin");
    const auto                copied  = root.Join("copied.bin");
    const std::string         payload = "worker-backed async helper payload";

    NGIN::IO::FileSystemDriver driver;
    auto                       ctx = driver.MakeTaskContext();

    auto writeTask = NGIN::IO::WriteAllBytesAsync(
            fs,
            ctx,
            source,
            std::span<const NGIN::Byte>(reinterpret_cast<const NGIN::Byte*>(payload.data()), payload.size()));
    auto writeResult = RunAsyncTask(writeTask, ctx);
    REQUIRE(writeResult.Succeeded());

    auto readTask   = NGIN::IO::ReadAllBytesAsync(fs, ctx, source);
    auto readResult = RunAsyncTask(readTask, ctx);
    REQUIRE(readResult.Succeeded());
    REQUIRE(readResult.Value().Size() == payload.size());
    REQUIRE(std::string(reinterpret_cast<const char*>(readResult.Value().data()), readResult.Value().Size()) == payload);

    auto infoTask   = fs.GetInfoAsync(ctx, source);
    auto infoResult = RunAsyncTask(infoTask, ctx);
    REQUIRE(infoResult.Succeeded());
    REQUIRE(infoResult.Value().type == NGIN::IO::EntryType::File);
    REQUIRE(infoResult.Value().size == payload.size());

    auto copyTask   = fs.CopyFileAsync(ctx, source, copied);
    auto copyResult = RunAsyncTask(copyTask, ctx);
    REQUIRE(copyResult.Succeeded());

    auto copiedText = NGIN::IO::ReadAllText(fs, copied);
    REQUIRE(copiedText.HasValue());
    REQUIRE(std::string(copiedText.Value().Data(), copiedText.Value().Size()) == payload);

    RemoveTempDir(fs, root);
}

TEST_CASE("IO.LocalFileSystem async directory handles scope relative operations", "[IO][LocalFileSystem][Async]")
{
    NGIN::IO::LocalFileSystem fs;
    const auto                root      = MakeTempDir(fs);
    const auto                nestedDir = root.Join("nested");
    const auto                childDir  = nestedDir.Join("child");

    REQUIRE(fs.CreateDirectories(childDir).HasValue());
    REQUIRE(NGIN::IO::WriteAllText(fs, nestedDir.Join("seed.txt"), "seed").HasValue());

    NGIN::IO::FileSystemDriver driver;
    auto                       ctx = driver.MakeTaskContext();

    auto directoryTask = fs.OpenDirectoryAsync(ctx, nestedDir);
    auto directoryOpen = RunAsyncTask(directoryTask, ctx);
    REQUIRE(directoryOpen.Succeeded());

    auto directory = std::move(directoryOpen.Value());
    REQUIRE(directory.IsValid());

    auto existsTask = directory.ExistsAsync(ctx, NGIN::IO::Path {"seed.txt"});
    auto exists     = RunAsyncTask(existsTask, ctx);
    REQUIRE(exists.Succeeded());
    REQUIRE(exists.Value());

    auto infoTask = directory.GetInfoAsync(ctx, NGIN::IO::Path {"seed.txt"});
    auto info     = RunAsyncTask(infoTask, ctx);
    REQUIRE(info.Succeeded());
    REQUIRE(info.Value().exists);

    NGIN::IO::FileOpenOptions options;
    options.access      = NGIN::IO::FileAccess::Read;
    options.disposition = NGIN::IO::FileCreateDisposition::OpenExisting;

    auto fileTask = directory.OpenFileAsync(ctx, NGIN::IO::Path {"seed.txt"}, options);
    auto fileOpen = RunAsyncTask(fileTask, ctx);
    REQUIRE(fileOpen.Succeeded());
    auto file = std::move(fileOpen.Value());
    REQUIRE(file.IsOpen());

    auto childTask = directory.OpenDirectoryAsync(ctx, NGIN::IO::Path {"child"});
    auto childOpen = RunAsyncTask(childTask, ctx);
    REQUIRE(childOpen.Succeeded());
    REQUIRE(childOpen.Value().IsValid());

    auto closeTask   = file.CloseAsync(ctx);
    auto closeResult = RunAsyncTask(closeTask, ctx);
    REQUIRE(closeResult.Succeeded());

    RemoveTempDir(fs, root);
}

TEST_CASE("IO.LocalFileSystem async operations observe cancellation before dispatch", "[IO][LocalFileSystem][Async]")
{
    NGIN::IO::LocalFileSystem fs;
    const auto                root     = MakeTempDir(fs);
    const auto                filePath = root.Join("cancel.txt");

    REQUIRE(NGIN::IO::WriteAllText(fs, filePath, "cancel me").HasValue());

    NGIN::IO::FileSystemDriver      driver;
    NGIN::Async::CancellationSource cancellation;
    cancellation.Cancel();
    auto ctx = driver.MakeTaskContext(cancellation.GetToken());

    NGIN::IO::FileOpenOptions options;
    options.access      = NGIN::IO::FileAccess::Read;
    options.disposition = NGIN::IO::FileCreateDisposition::OpenExisting;

    auto openTask = fs.OpenFileAsync(ctx, filePath, options);
    auto opened   = RunAsyncTask(openTask, ctx);
    REQUIRE(opened.IsCanceled());

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

    auto exists = directory->Exists(NGIN::IO::Path {"seed.txt"});
    REQUIRE(exists.HasValue());
    REQUIRE(exists.Value());

    auto info = directory->GetInfo(NGIN::IO::Path {"seed.txt"});
    REQUIRE(info.HasValue());
    REQUIRE(info.Value().type == NGIN::IO::EntryType::File);

    auto child = directory->OpenDirectory(NGIN::IO::Path {"child"});
    REQUIRE(child.HasValue());
    auto childInfo = child->GetInfo(NGIN::IO::Path {"."});
    REQUIRE(childInfo.HasValue());
    REQUIRE(childInfo.Value().type == NGIN::IO::EntryType::Directory);

    NGIN::IO::FileOpenOptions openOptions;
    openOptions.access      = NGIN::IO::FileAccess::Write;
    openOptions.share       = NGIN::IO::FileShare::Read;
    openOptions.disposition = NGIN::IO::FileCreateDisposition::CreateAlways;

    auto openedFile = directory->OpenFile(NGIN::IO::Path {"from_handle.txt"}, openOptions);
    REQUIRE(openedFile.HasValue());

    const std::string payload     = "dir-handle";
    const auto        writeResult = openedFile->Write({reinterpret_cast<const NGIN::Byte*>(payload.data()), payload.size()});
    REQUIRE(writeResult.HasValue());
    REQUIRE(writeResult.Value() == payload.size());
    openedFile->Close();

    auto writtenText = NGIN::IO::ReadAllText(fs, nestedDir.Join("from_handle.txt"));
    REQUIRE(writtenText.HasValue());
    REQUIRE(std::string(writtenText.Value().Data(), writtenText.Value().Size()) == payload);

    REQUIRE(directory->CreateDirectory(NGIN::IO::Path {"created"}).HasValue());
    REQUIRE(fs.Exists(nestedDir.Join("created")).Value());
    REQUIRE(directory->RemoveDirectory(NGIN::IO::Path {"created"}).HasValue());
    REQUIRE_FALSE(fs.Exists(nestedDir.Join("created")).Value());

    REQUIRE(directory->RemoveFile(NGIN::IO::Path {"from_handle.txt"}).HasValue());
    REQUIRE_FALSE(fs.Exists(nestedDir.Join("from_handle.txt")).Value());

    auto escaped = directory->Exists(NGIN::IO::Path {"../outside.txt"});
    REQUIRE_FALSE(escaped.HasValue());
    REQUIRE(escaped.Error().code == NGIN::IO::IOErrorCode::InvalidPath);

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
    REQUIRE(absolute.Value().View() == filePath.View());

    auto canonical = fs.Canonical(symlinkPath);
    REQUIRE(canonical.HasValue());
    REQUIRE(canonical.Value().View() == filePath.View());

    auto weaklyCanonical = fs.WeaklyCanonical(root.Join("nested/missing/child.txt"));
    REQUIRE(weaklyCanonical.HasValue());
    REQUIRE(weaklyCanonical.Value().View() == root.Join("nested/missing/child.txt").LexicallyNormal().View());

    auto symlinkTarget = fs.ReadSymlink(symlinkPath);
    REQUIRE(symlinkTarget.HasValue());
    REQUIRE(symlinkTarget.Value().View() == filePath.View());

    auto sameFile = fs.SameFile(filePath, hardLinkPath);
    REQUIRE(sameFile.HasValue());
    REQUIRE(sameFile.Value());

    NGIN::IO::FilePermissions readOnlyPermissions;
    readOnlyPermissions.nativeBits = 0444;
    REQUIRE(fs.SetPermissions(filePath, readOnlyPermissions).HasValue());

    auto info = fs.GetInfo(filePath);
    REQUIRE(info.HasValue());
    REQUIRE((info.Value().permissions.nativeBits & 0222u) == 0);

    REQUIRE(fs.CreateDirectories(tempBase).HasValue());
    auto tempDirectory = fs.CreateTempDirectory(tempBase, "dir_");
    REQUIRE(tempDirectory.HasValue());
    auto tempFile = fs.CreateTempFile(tempBase, "file_");
    REQUIRE(tempFile.HasValue());
    REQUIRE(fs.Exists(tempDirectory.Value()).Value());
    REQUIRE(fs.Exists(tempFile.Value()).Value());

    REQUIRE(NGIN::IO::WriteAllText(fs, replacementSrc, "replacement").HasValue());
    REQUIRE(fs.ReplaceFile(replacementSrc, filePath).HasValue());
    auto replaced = NGIN::IO::ReadAllText(fs, filePath);
    REQUIRE(replaced.HasValue());
    REQUIRE(std::string(replaced.Value().Data(), replaced.Value().Size()) == "replacement");

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
    REQUIRE(fileInfo.Value().type == NGIN::IO::EntryType::File);
    REQUIRE(fileInfo.Value().ownership.valid);
    REQUIRE(fileInfo.Value().ownership.userId == static_cast<NGIN::UInt32>(::geteuid()));
    REQUIRE(fileInfo.Value().ownership.groupId == static_cast<NGIN::UInt32>(::getegid()));
    REQUIRE(fileInfo.Value().identity.valid);
    REQUIRE(fileInfo.Value().identity.hardLinkCount >= 2);
    REQUIRE(fileInfo.Value().permissions.nativeBits != 0);
    REQUIRE(fileInfo.Value().changed.valid);

    auto hardLinkInfo = fs.GetInfo(hardLinkPath, noFollow);
    REQUIRE(hardLinkInfo.HasValue());
    REQUIRE(hardLinkInfo.Value().identity.hardLinkCount >= 2);

    auto symlinkInfo = fs.GetInfo(symlinkPath, noFollow);
    REQUIRE(symlinkInfo.HasValue());
    REQUIRE(symlinkInfo.Value().type == NGIN::IO::EntryType::Symlink);
    REQUIRE(symlinkInfo.Value().exists);
    REQUIRE(symlinkInfo.Value().symlinkTargetExists);

    auto followedSymlinkInfo = fs.GetInfo(symlinkPath, follow);
    REQUIRE(followedSymlinkInfo.HasValue());
    REQUIRE(followedSymlinkInfo.Value().type == NGIN::IO::EntryType::File);
    REQUIRE(followedSymlinkInfo.Value().exists);

    auto danglingSymlinkInfo = fs.GetInfo(danglingSymlinkPath, noFollow);
    REQUIRE(danglingSymlinkInfo.HasValue());
    REQUIRE(danglingSymlinkInfo.Value().type == NGIN::IO::EntryType::Symlink);
    REQUIRE(danglingSymlinkInfo.Value().exists);
    REQUIRE_FALSE(danglingSymlinkInfo.Value().symlinkTargetExists);

    auto danglingFollowInfo = fs.GetInfo(danglingSymlinkPath, follow);
    REQUIRE(danglingFollowInfo.HasValue());
    REQUIRE(danglingFollowInfo.Value().type == NGIN::IO::EntryType::Symlink);
    REQUIRE(danglingFollowInfo.Value().exists);
    REQUIRE_FALSE(danglingFollowInfo.Value().symlinkTargetExists);

    auto fifoInfo = fs.GetInfo(fifoPath, noFollow);
    REQUIRE(fifoInfo.HasValue());
    REQUIRE(fifoInfo.Value().type == NGIN::IO::EntryType::Fifo);

    auto socketInfo = fs.GetInfo(socketPath, noFollow);
    REQUIRE(socketInfo.HasValue());
    REQUIRE(socketInfo.Value().type == NGIN::IO::EntryType::Socket);

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
        auto next = enumerator->Next();
        REQUIRE(next.HasValue());
        if (!next.Value())
            break;

        const auto& entry = enumerator->Current();
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

    auto targetPath = directory->ReadSymlink(NGIN::IO::Path {"target.sym"});
    REQUIRE(targetPath.HasValue());
    REQUIRE(targetPath.Value().View() == "target.txt");

    RemoveTempDir(fs, root);
}
#endif
