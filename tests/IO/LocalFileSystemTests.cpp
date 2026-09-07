#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/FileSystemUtilities.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>
#include <NGIN/IO/Runtime.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

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
        REQUIRE(tempDirectory.has_value());

        const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto path        = tempDirectory.value().Join("ngin_base_fs_test_" + std::to_string(uniqueValue));

        REQUIRE(fs.CreateDirectories(path).has_value());
        return path;
    }

    void RemoveTempDir(NGIN::IO::LocalFileSystem& fs, const NGIN::IO::Path& path)
    {
        NGIN::IO::RemoveOptions options;
        options.recursive     = true;
        options.ignoreMissing = true;
        REQUIRE(fs.RemoveDirectory(path, options).has_value());
    }

    [[nodiscard]] std::vector<NGIN::IO::DirectoryEntry> CollectEntries(
            NGIN::IO::LocalFileSystem& fs, const NGIN::IO::Path& path, const NGIN::IO::EnumerateOptions& options)
    {
        auto enumerator = fs.Enumerate(path, options);
        REQUIRE(enumerator.has_value());

        std::vector<NGIN::IO::DirectoryEntry> entries;
        while (true)
        {
            auto next = enumerator->Next();
            REQUIRE(next.has_value());
            if (!next->HasEntry())
                break;
            entries.push_back(next->Entry());
        }
        return entries;
    }

    template<typename T>
    auto RunAsyncTask(NGIN::IO::AsyncTask<T>& task, NGIN::Async::TaskContext& ctx)
            -> NGIN::Async::Completion<T, NGIN::IO::IOError>
    {
        return NGIN::Async::SyncWait(ctx, std::move(task));
    }

    auto RunAsyncTask(NGIN::IO::AsyncTaskVoid& task, NGIN::Async::TaskContext& ctx)
            -> NGIN::Async::Completion<void, NGIN::IO::IOError>
    {
        return NGIN::Async::SyncWait(ctx, std::move(task));
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

    REQUIRE(NGIN::IO::WriteAllText(fs, filePath, "hello world").has_value());

    auto text = NGIN::IO::ReadAllText(fs, filePath);
    REQUIRE(text.has_value());
    REQUIRE(std::string(text.value().Data(), text.value().Size()) == "hello world");

    auto info = fs.GetInfo(filePath);
    REQUIRE(info.has_value());
    REQUIRE(info.value().exists);
    REQUIRE(info.value().type == NGIN::IO::EntryType::File);

    NGIN::IO::EnumerateOptions options;
    options.populateInfo = true;
    auto enumerator      = fs.Enumerate(root, options);
    REQUIRE(enumerator.has_value());

    auto next = enumerator->Next();
    REQUIRE(next.has_value());
    REQUIRE(next->HasEntry());
    REQUIRE(next->Entry().name.View() == "hello.txt");
    REQUIRE(next->Entry().info.has_value());
    REQUIRE(next->Entry().info->type == NGIN::IO::EntryType::File);

    auto end = enumerator->Next();
    REQUIRE(end.has_value());
    REQUIRE_FALSE(end->HasEntry());

    const auto capabilities = fs.GetCapabilities();
    REQUIRE(capabilities.memoryMappedFiles);
    REQUIRE(capabilities.metadataNoFollow);

    RemoveTempDir(fs, root);
}

TEST_CASE("IO.LocalFileSystem no-replace rename preserves conflicts", "[IO][LocalFileSystem]")
{
    NGIN::IO::LocalFileSystem fs;
    const auto                root        = MakeTempDir(fs);
    const auto                source      = root.Join("source.txt");
    const auto                destination = root.Join("destination.txt");
    const auto                renamed     = root.Join("renamed.txt");
    REQUIRE(NGIN::IO::WriteAllText(fs, source, "source").has_value());
    REQUIRE(NGIN::IO::WriteAllText(fs, destination, "destination").has_value());

    REQUIRE(fs.GetCapabilities().atomicRenameNoReplace);
    auto conflict = fs.RenameNoReplace(source, destination);
    REQUIRE_FALSE(conflict.has_value());
    REQUIRE(conflict.error().code == NGIN::IO::IOErrorCode::AlreadyExists);
    REQUIRE(fs.Exists(source).value());
    auto destinationText = NGIN::IO::ReadAllText(fs, destination);
    REQUIRE(destinationText.has_value());
    REQUIRE(std::string(destinationText.value().Data(), destinationText.value().Size()) == "destination");

    REQUIRE(fs.RenameNoReplace(source, renamed).has_value());
    REQUIRE_FALSE(fs.Exists(source).value());
    REQUIRE(fs.Exists(renamed).value());
    RemoveTempDir(fs, root);
}

TEST_CASE("IO.LocalFileSystem reports and applies replacement durability", "[IO][LocalFileSystem]")
{
    NGIN::IO::LocalFileSystem fs;
    const auto                root        = MakeTempDir(fs);
    const auto                source      = root.Join("replacement.txt");
    const auto                destination = root.Join("current.txt");
    REQUIRE(NGIN::IO::WriteAllText(fs, source, "new").has_value());
    REQUIRE(NGIN::IO::WriteAllText(fs, destination, "old").has_value());
    REQUIRE(fs.GetCapabilities().atomicReplace);

    NGIN::IO::ReplaceOptions options;
    options.flushSource          = true;
    options.flushParentDirectory = true;
    auto replaced                = fs.ReplaceFile(source, destination, options);
#if defined(_WIN32)
    REQUIRE_FALSE(fs.GetCapabilities().durableReplace);
    REQUIRE_FALSE(replaced.has_value());
    REQUIRE(replaced.error().code == NGIN::IO::IOErrorCode::Unsupported);
    REQUIRE(fs.Exists(source).value());
    auto current = NGIN::IO::ReadAllText(fs, destination);
    REQUIRE(current.has_value());
    REQUIRE(std::string(current.value().Data(), current.value().Size()) == "old");
#else
    REQUIRE(fs.GetCapabilities().durableReplace);
    REQUIRE(replaced.has_value());
    REQUIRE_FALSE(fs.Exists(source).value());
    auto current = NGIN::IO::ReadAllText(fs, destination);
    REQUIRE(current.has_value());
    REQUIRE(std::string(current.value().Data(), current.value().Size()) == "new");
#endif

    RemoveTempDir(fs, root);
}

TEST_CASE("IO.LocalFileSystem recursive copy applies symlink and cleanup policies", "[IO][LocalFileSystem]")
{
    NGIN::IO::LocalFileSystem fs;
    const auto                root        = MakeTempDir(fs);
    const auto                source      = root.Join("source");
    const auto                destination = root.Join("destination");
    REQUIRE(fs.CreateDirectories(source.Join("nested")).has_value());
    REQUIRE(NGIN::IO::WriteAllText(fs, source.Join("nested/data.txt"), "payload").has_value());
    REQUIRE(fs.CreateSymlink(NGIN::IO::Path {"nested/data.txt"}, source.Join("data.sym")).has_value());
    REQUIRE(fs.CreateSymlink(NGIN::IO::Path {"missing.txt"}, source.Join("dangling.sym")).has_value());

    NGIN::IO::CopyOptions recursive;
    recursive.recursive = true;
    REQUIRE(fs.CopyFile(source, destination, recursive).has_value());
    auto target = fs.ReadSymlink(destination.Join("data.sym"));
    REQUIRE(target.has_value());
    REQUIRE(target.value().View() == "nested/data.txt");
    auto danglingTarget = fs.ReadSymlink(destination.Join("dangling.sym"));
    REQUIRE(danglingTarget.has_value());
    REQUIRE(danglingTarget.value().View() == "missing.txt");

    NGIN::IO::CopyOptions follow;
    follow.symlinks = NGIN::IO::CopySymlinkMode::Follow;
    REQUIRE(fs.CopyFile(source.Join("data.sym"), root.Join("followed.txt"), follow).has_value());
    auto followed = NGIN::IO::ReadAllText(fs, root.Join("followed.txt"));
    REQUIRE(followed.has_value());
    REQUIRE(std::string(followed.value().Data(), followed.value().Size()) == "payload");
    auto danglingFollow = fs.CopyFile(source.Join("dangling.sym"), root.Join("dangling-followed.txt"), follow);
    REQUIRE_FALSE(danglingFollow.has_value());
    REQUIRE_FALSE(fs.Exists(root.Join("dangling-followed.txt")).value());

    NGIN::IO::CopyOptions reject;
    reject.recursive = true;
    reject.symlinks  = NGIN::IO::CopySymlinkMode::Reject;
    auto rejected    = fs.CopyFile(source, root.Join("rejected"), reject);
    REQUIRE_FALSE(rejected.has_value());
    REQUIRE_FALSE(fs.Exists(root.Join("rejected")).value());

    RemoveTempDir(fs, root);
}

TEST_CASE("IO.LocalFileSystem enumeration supports no-info and deterministic sorting", "[IO][LocalFileSystem]")
{
    NGIN::IO::LocalFileSystem fs;
    const auto                root  = MakeTempDir(fs);
    const auto                alpha = root.Join("alpha");
    const auto                beta  = root.Join("beta");

    REQUIRE(fs.CreateDirectories(alpha).has_value());
    REQUIRE(fs.CreateDirectories(beta).has_value());
    REQUIRE(NGIN::IO::WriteAllText(fs, alpha.Join("same.txt"), "alpha").has_value());
    REQUIRE(NGIN::IO::WriteAllText(fs, beta.Join("same.txt"), "beta").has_value());
    REQUIRE(NGIN::IO::WriteAllText(fs, root.Join("zeta.txt"), "zeta").has_value());

    NGIN::IO::EnumerateOptions nameOnlyOptions;
    nameOnlyOptions.includeDirectories = false;
    auto nameOnlyEntries               = CollectEntries(fs, root, nameOnlyOptions);
    REQUIRE(nameOnlyEntries.size() == 1);
    REQUIRE(nameOnlyEntries.front().name.View() == "zeta.txt");
    REQUIRE_FALSE(nameOnlyEntries.front().info.has_value());

    NGIN::IO::EnumerateOptions pathSortOptions;
    pathSortOptions.recursive          = true;
    pathSortOptions.includeDirectories = false;
    pathSortOptions.sortOrder          = NGIN::IO::DirectorySortOrder::LexicalPath;
    auto pathSortedEntries             = CollectEntries(fs, root, pathSortOptions);
    REQUIRE(pathSortedEntries.size() == 3);
    for (std::size_t i = 1; i < pathSortedEntries.size(); ++i)
        REQUIRE(pathSortedEntries[i - 1].path.View() < pathSortedEntries[i].path.View());

    NGIN::IO::EnumerateOptions nameSortOptions;
    nameSortOptions.recursive          = true;
    nameSortOptions.includeDirectories = false;
    nameSortOptions.sortOrder          = NGIN::IO::DirectorySortOrder::LexicalName;
    auto nameSortedEntries             = CollectEntries(fs, root, nameSortOptions);
    REQUIRE(nameSortedEntries.size() == 3);
    for (std::size_t i = 1; i < nameSortedEntries.size(); ++i)
    {
        const auto previousName = nameSortedEntries[i - 1].name.View();
        const auto currentName  = nameSortedEntries[i].name.View();
        if (previousName == currentName)
            REQUIRE(nameSortedEntries[i - 1].path.View() < nameSortedEntries[i].path.View());
        else
            REQUIRE(previousName < currentName);
    }

    RemoveTempDir(fs, root);
}

TEST_CASE("IO.LocalFileSystem atomic text writes replace existing content", "[IO][LocalFileSystem]")
{
    NGIN::IO::LocalFileSystem fs;
    const auto                root     = MakeTempDir(fs);
    const auto                filePath = root.Join("atomic.txt");
    const auto                nested   = root.Join("nested/created.txt");

    REQUIRE(NGIN::IO::WriteAllText(fs, filePath, "old").has_value());
    REQUIRE(NGIN::IO::WriteAllTextAtomic(fs, filePath, "new").has_value());

    auto replaced = NGIN::IO::ReadAllText(fs, filePath);
    REQUIRE(replaced.has_value());
    REQUIRE(std::string(replaced.value().Data(), replaced.value().Size()) == "new");

    NGIN::IO::AtomicWriteOptions options;
    options.createParentDirectories = true;
    REQUIRE(NGIN::IO::WriteAllTextAtomic(fs, nested, "created", options).has_value());

    auto created = NGIN::IO::ReadAllText(fs, nested);
    REQUIRE(created.has_value());
    REQUIRE(std::string(created.value().Data(), created.value().Size()) == "created");

    const auto directoryDestination = root.Join("directory-target");
    REQUIRE(fs.CreateDirectory(directoryDestination).has_value());
    auto failed = NGIN::IO::WriteAllTextAtomic(fs, directoryDestination, "not a file");
    REQUIRE_FALSE(failed.has_value());

    auto directoryInfo = fs.GetInfo(directoryDestination);
    REQUIRE(directoryInfo.has_value());
    REQUIRE(directoryInfo.value().exists);
    REQUIRE(directoryInfo.value().type == NGIN::IO::EntryType::Directory);

    RemoveTempDir(fs, root);
}

TEST_CASE("IO.LocalFileSystem async file operations use value handles", "[IO][LocalFileSystem][Async]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler {1};
    NGIN::IO::Runtime                    runtime;
    NGIN::IO::LocalFileSystem            fs(runtime);
    const auto                           root     = MakeTempDir(fs);
    const auto                           filePath = root.Join("async.bin");

    const std::string payload = "async local filesystem payload";
    REQUIRE(NGIN::IO::WriteAllText(fs, filePath, payload).has_value());

    auto ctx = NGIN::Async::TaskContext(scheduler);

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

TEST_CASE("IO.LocalFileSystem async utility helpers work through IO::Runtime", "[IO][LocalFileSystem][Async]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler {1};
    NGIN::IO::Runtime                    runtime;
    NGIN::IO::LocalFileSystem            fs(runtime);
    const auto                           root    = MakeTempDir(fs);
    const auto                           source  = root.Join("source.bin");
    const auto                           copied  = root.Join("copied.bin");
    const std::string                    payload = "worker-backed async helper payload";

    auto ctx = NGIN::Async::TaskContext(scheduler);

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
    REQUIRE(copiedText.has_value());
    REQUIRE(std::string(copiedText.value().Data(), copiedText.value().Size()) == payload);

    RemoveTempDir(fs, root);
}

TEST_CASE("IO.LocalFileSystem async directory handles scope relative operations", "[IO][LocalFileSystem][Async]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler {1};
    NGIN::IO::Runtime                    runtime;
    NGIN::IO::LocalFileSystem            fs(runtime);
    const auto                           root      = MakeTempDir(fs);
    const auto                           nestedDir = root.Join("nested");
    const auto                           childDir  = nestedDir.Join("child");

    REQUIRE(fs.CreateDirectories(childDir).has_value());
    REQUIRE(NGIN::IO::WriteAllText(fs, nestedDir.Join("seed.txt"), "seed").has_value());

    auto ctx = NGIN::Async::TaskContext(scheduler);

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
    NGIN::Execution::ThreadPoolScheduler scheduler {1};
    NGIN::IO::Runtime                    runtime;
    NGIN::IO::LocalFileSystem            fs(runtime);
    const auto                           root     = MakeTempDir(fs);
    const auto                           filePath = root.Join("cancel.txt");

    REQUIRE(NGIN::IO::WriteAllText(fs, filePath, "cancel me").has_value());

    NGIN::Async::CancellationSource cancellation;
    cancellation.Cancel();
    auto ctx = NGIN::Async::TaskContext(scheduler, cancellation.GetToken());

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

    REQUIRE(fs.CreateDirectories(childDir).has_value());
    REQUIRE(NGIN::IO::WriteAllText(fs, nestedDir.Join("seed.txt"), "seed").has_value());

    auto directory = fs.OpenDirectory(nestedDir);
    REQUIRE(directory.has_value());

    auto exists = directory->Exists(NGIN::IO::Path {"seed.txt"});
    REQUIRE(exists.has_value());
    REQUIRE(exists.value());

    auto info = directory->GetInfo(NGIN::IO::Path {"seed.txt"});
    REQUIRE(info.has_value());
    REQUIRE(info.value().type == NGIN::IO::EntryType::File);

    auto child = directory->OpenDirectory(NGIN::IO::Path {"child"});
    REQUIRE(child.has_value());
    auto childInfo = child->GetInfo(NGIN::IO::Path {"."});
    REQUIRE(childInfo.has_value());
    REQUIRE(childInfo.value().type == NGIN::IO::EntryType::Directory);

    NGIN::IO::FileOpenOptions openOptions;
    openOptions.access      = NGIN::IO::FileAccess::Write;
    openOptions.share       = NGIN::IO::FileShare::Read;
    openOptions.disposition = NGIN::IO::FileCreateDisposition::CreateAlways;

    auto openedFile = directory->OpenFile(NGIN::IO::Path {"from_handle.txt"}, openOptions);
    REQUIRE(openedFile.has_value());

    const std::string payload     = "dir-handle";
    const auto        writeResult = openedFile->Write({reinterpret_cast<const NGIN::Byte*>(payload.data()), payload.size()});
    REQUIRE(writeResult.has_value());
    REQUIRE(writeResult.value() == payload.size());
    openedFile->Close();

    auto writtenText = NGIN::IO::ReadAllText(fs, nestedDir.Join("from_handle.txt"));
    REQUIRE(writtenText.has_value());
    REQUIRE(std::string(writtenText.value().Data(), writtenText.value().Size()) == payload);

    REQUIRE(directory->CreateDirectory(NGIN::IO::Path {"created"}).has_value());
    REQUIRE(fs.Exists(nestedDir.Join("created")).value());
    REQUIRE(directory->RemoveDirectory(NGIN::IO::Path {"created"}).has_value());
    REQUIRE_FALSE(fs.Exists(nestedDir.Join("created")).value());

    REQUIRE(directory->RemoveFile(NGIN::IO::Path {"from_handle.txt"}).has_value());
    REQUIRE_FALSE(fs.Exists(nestedDir.Join("from_handle.txt")).value());

    auto escaped = directory->Exists(NGIN::IO::Path {"../outside.txt"});
    REQUIRE_FALSE(escaped.has_value());
    REQUIRE(escaped.error().code == NGIN::IO::IOErrorCode::InvalidPath);

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

    REQUIRE(fs.CreateDirectories(nestedDir).has_value());
    REQUIRE(NGIN::IO::WriteAllText(fs, filePath, "hello").has_value());
    REQUIRE(fs.CreateSymlink(filePath, symlinkPath).has_value());
    REQUIRE(fs.CreateHardLink(filePath, hardLinkPath).has_value());

    auto absolute = fs.Absolute(NGIN::IO::Path {"file.txt"}, nestedDir);
    REQUIRE(absolute.has_value());
    REQUIRE(absolute.value().View() == filePath.View());

    auto canonical = fs.Canonical(symlinkPath);
    REQUIRE(canonical.has_value());
    REQUIRE(canonical.value().View() == filePath.View());

    auto weaklyCanonical = fs.WeaklyCanonical(root.Join("nested/missing/child.txt"));
    REQUIRE(weaklyCanonical.has_value());
    REQUIRE(weaklyCanonical.value().View() == root.Join("nested/missing/child.txt").LexicallyNormal().View());

    auto symlinkTarget = fs.ReadSymlink(symlinkPath);
    REQUIRE(symlinkTarget.has_value());
    REQUIRE(symlinkTarget.value().View() == filePath.View());

    auto sameFile = fs.SameFile(filePath, hardLinkPath);
    REQUIRE(sameFile.has_value());
    REQUIRE(sameFile.value());

    NGIN::IO::FilePermissions readOnlyPermissions;
    readOnlyPermissions.nativeBits = 0444;
    REQUIRE(fs.SetPermissions(filePath, readOnlyPermissions).has_value());

    auto info = fs.GetInfo(filePath);
    REQUIRE(info.has_value());
    REQUIRE((info.value().permissions.nativeBits & 0222u) == 0);

    REQUIRE(fs.CreateDirectories(tempBase).has_value());
    auto tempDirectory = fs.CreateTempDirectory(tempBase, "dir_");
    REQUIRE(tempDirectory.has_value());
    auto tempFile = fs.CreateTempFile(tempBase, "file_");
    REQUIRE(tempFile.has_value());
    REQUIRE(fs.Exists(tempDirectory.value()).value());
    REQUIRE(fs.Exists(tempFile.value()).value());

    REQUIRE(NGIN::IO::WriteAllText(fs, replacementSrc, "replacement").has_value());
    REQUIRE(fs.ReplaceFile(replacementSrc, filePath).has_value());
    auto replaced = NGIN::IO::ReadAllText(fs, filePath);
    REQUIRE(replaced.has_value());
    REQUIRE(std::string(replaced.value().Data(), replaced.value().Size()) == "replacement");

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

    REQUIRE(NGIN::IO::WriteAllText(fs, filePath, "hello world").has_value());
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
    REQUIRE(fileInfo.has_value());
    REQUIRE(fileInfo.value().type == NGIN::IO::EntryType::File);
    REQUIRE(fileInfo.value().ownership.valid);
    REQUIRE(fileInfo.value().ownership.userId == static_cast<NGIN::UInt32>(::geteuid()));
    REQUIRE(fileInfo.value().ownership.groupId == static_cast<NGIN::UInt32>(::getegid()));
    REQUIRE(fileInfo.value().identity.valid);
    REQUIRE(fileInfo.value().identity.hardLinkCount >= 2);
    REQUIRE(fileInfo.value().permissions.nativeBits != 0);
    REQUIRE(fileInfo.value().changed.valid);

    auto hardLinkInfo = fs.GetInfo(hardLinkPath, noFollow);
    REQUIRE(hardLinkInfo.has_value());
    REQUIRE(hardLinkInfo.value().identity.hardLinkCount >= 2);

    auto symlinkInfo = fs.GetInfo(symlinkPath, noFollow);
    REQUIRE(symlinkInfo.has_value());
    REQUIRE(symlinkInfo.value().type == NGIN::IO::EntryType::Symlink);
    REQUIRE(symlinkInfo.value().exists);
    REQUIRE(symlinkInfo.value().symlinkTargetExists);

    auto followedSymlinkInfo = fs.GetInfo(symlinkPath, follow);
    REQUIRE(followedSymlinkInfo.has_value());
    REQUIRE(followedSymlinkInfo.value().type == NGIN::IO::EntryType::File);
    REQUIRE(followedSymlinkInfo.value().exists);

    auto danglingSymlinkInfo = fs.GetInfo(danglingSymlinkPath, noFollow);
    REQUIRE(danglingSymlinkInfo.has_value());
    REQUIRE(danglingSymlinkInfo.value().type == NGIN::IO::EntryType::Symlink);
    REQUIRE(danglingSymlinkInfo.value().exists);
    REQUIRE_FALSE(danglingSymlinkInfo.value().symlinkTargetExists);

    auto danglingFollowInfo = fs.GetInfo(danglingSymlinkPath, follow);
    REQUIRE(danglingFollowInfo.has_value());
    REQUIRE(danglingFollowInfo.value().type == NGIN::IO::EntryType::Symlink);
    REQUIRE(danglingFollowInfo.value().exists);
    REQUIRE_FALSE(danglingFollowInfo.value().symlinkTargetExists);

    auto fifoInfo = fs.GetInfo(fifoPath, noFollow);
    REQUIRE(fifoInfo.has_value());
    REQUIRE(fifoInfo.value().type == NGIN::IO::EntryType::Fifo);

    auto socketInfo = fs.GetInfo(socketPath, noFollow);
    REQUIRE(socketInfo.has_value());
    REQUIRE(socketInfo.value().type == NGIN::IO::EntryType::Socket);

    NGIN::IO::EnumerateOptions enumerateOptions;
    enumerateOptions.includeSymlinks = true;
    enumerateOptions.populateInfo    = true;
    auto enumerator                  = fs.Enumerate(root, enumerateOptions);
    REQUIRE(enumerator.has_value());

    bool sawFifo    = false;
    bool sawSocket  = false;
    bool sawSymlink = false;
    while (true)
    {
        auto next = enumerator->Next();
        REQUIRE(next.has_value());
        if (!next->HasEntry())
            break;

        const auto& entry = next->Entry();
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

    REQUIRE(fs.CreateDirectories(nestedDir).has_value());
    REQUIRE(NGIN::IO::WriteAllText(fs, target, "hello").has_value());
    REQUIRE(::symlink("target.txt", ToNativeString(linkPath).c_str()) == 0);

    auto directory = fs.OpenDirectory(nestedDir);
    REQUIRE(directory.has_value());

    auto targetPath = directory->ReadSymlink(NGIN::IO::Path {"target.sym"});
    REQUIRE(targetPath.has_value());
    REQUIRE(targetPath.value().View() == "target.txt");

    RemoveTempDir(fs, root);
}
#endif
