#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/FileSystemUtilities.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>
#include <NGIN/IO/Runtime.hpp>
#include <NGIN/IO/VirtualFileSystem.hpp>

#include <array>
#include <chrono>
#include <memory>
#include <string>

namespace
{
    [[nodiscard]] NGIN::IO::Path MakeTempDir(NGIN::IO::LocalFileSystem& fs)
    {
        auto tempDirectory = fs.TempDirectory();
        REQUIRE(tempDirectory.has_value());

        const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto path        = tempDirectory.value().Join("ngin_base_vfs_test_" + std::to_string(uniqueValue));

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

    REQUIRE(NGIN::IO::WriteAllText(vfs, virtualFile, "hello").has_value());
    REQUIRE(vfs.CreateSymlink(virtualFile, virtualLink).has_value());
    REQUIRE(vfs.CreateHardLink(virtualFile, virtualHard).has_value());

    auto canonical = vfs.Canonical(virtualLink);
    REQUIRE(canonical.has_value());
    REQUIRE(canonical.value().View() == virtualFile.View());

    auto weaklyCanonical = vfs.WeaklyCanonical(virtualRoot.Join("missing/child.txt"));
    REQUIRE(weaklyCanonical.has_value());
    REQUIRE(weaklyCanonical.value().View() == virtualRoot.Join("missing/child.txt").LexicallyNormal().View());

    auto symlinkTarget = vfs.ReadSymlink(virtualLink);
    REQUIRE(symlinkTarget.has_value());
    REQUIRE(symlinkTarget.value().View() == virtualFile.View());

    auto sameFile = vfs.SameFile(virtualFile, virtualHard);
    REQUIRE(sameFile.has_value());
    REQUIRE(sameFile.value());

    auto tempDirectory = vfs.CreateTempDirectory(virtualRoot, "dir_");
    REQUIRE(tempDirectory.has_value());
    auto tempFile = vfs.CreateTempFile(virtualRoot, "file_");
    REQUIRE(tempFile.has_value());
    REQUIRE(vfs.Exists(tempDirectory.value()).value());
    REQUIRE(vfs.Exists(tempFile.value()).value());

    REQUIRE(NGIN::IO::WriteAllText(vfs, virtualReplace, "replacement").has_value());
    REQUIRE(vfs.ReplaceFile(virtualReplace, virtualFile).has_value());
    auto text = NGIN::IO::ReadAllText(vfs, virtualFile);
    REQUIRE(text.has_value());
    REQUIRE(std::string(text.value().Data(), text.value().Size()) == "replacement");

    RemoveTempDir(backingFs, realRoot);
}

TEST_CASE("IO.VirtualFileSystem copies and moves across mounts", "[IO][VirtualFileSystem]")
{
    NGIN::IO::LocalFileSystem backingFs;
    const auto                root       = MakeTempDir(backingFs);
    const auto                sourceRoot = root.Join("source");
    const auto                targetRoot = root.Join("target");
    REQUIRE(backingFs.CreateDirectories(sourceRoot).has_value());
    REQUIRE(backingFs.CreateDirectories(targetRoot).has_value());

    NGIN::IO::VirtualFileSystem vfs;
    vfs.AddMount(std::make_shared<NGIN::IO::LocalMount>(
            sourceRoot, NGIN::IO::MountPoint {.virtualPrefix = NGIN::IO::Path {"/source"}}));
    vfs.AddMount(std::make_shared<NGIN::IO::LocalMount>(
            targetRoot, NGIN::IO::MountPoint {.virtualPrefix = NGIN::IO::Path {"/target"}}));

    const NGIN::IO::Path sourceDirectory {"/source/tree"};
    const NGIN::IO::Path copiedDirectory {"/target/copied"};
    REQUIRE(vfs.CreateDirectories(sourceDirectory.Join("nested")).has_value());
    REQUIRE(NGIN::IO::WriteAllText(vfs, sourceDirectory.Join("nested/data.txt"), "cross-mount").has_value());
    REQUIRE(vfs.CreateSymlink(NGIN::IO::Path {"nested/data.txt"}, sourceDirectory.Join("data.sym")).has_value());

    NGIN::IO::CopyOptions recursive;
    recursive.recursive = true;
    REQUIRE(vfs.CopyFile(sourceDirectory, copiedDirectory, recursive).has_value());
    auto copied = NGIN::IO::ReadAllText(vfs, copiedDirectory.Join("nested/data.txt"));
    REQUIRE(copied.has_value());
    REQUIRE(std::string(copied.value().Data(), copied.value().Size()) == "cross-mount");
    auto copiedTarget = vfs.ReadSymlink(copiedDirectory.Join("data.sym"));
    REQUIRE(copiedTarget.has_value());
    REQUIRE(copiedTarget.value().View() == "nested/data.txt");

    const NGIN::IO::Path moveSource {"/source/move.txt"};
    const NGIN::IO::Path moveTarget {"/target/move.txt"};
    REQUIRE(NGIN::IO::WriteAllText(vfs, moveSource, "source").has_value());
    REQUIRE(NGIN::IO::WriteAllText(vfs, moveTarget, "conflict").has_value());
    auto conflict = vfs.Move(moveSource, moveTarget);
    REQUIRE_FALSE(conflict.has_value());
    REQUIRE(conflict.error().code == NGIN::IO::IOErrorCode::AlreadyExists);
    REQUIRE(vfs.Exists(moveSource).value());

    NGIN::IO::CopyOptions overwrite;
    overwrite.overwriteExisting = true;
    REQUIRE(vfs.Move(moveSource, moveTarget, overwrite).has_value());
    REQUIRE_FALSE(vfs.Exists(moveSource).value());
    auto moved = NGIN::IO::ReadAllText(vfs, moveTarget);
    REQUIRE(moved.has_value());
    REQUIRE(std::string(moved.value().Data(), moved.value().Size()) == "source");

    RemoveTempDir(backingFs, root);
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

    REQUIRE(vfs.CreateDirectories(childDir).has_value());
    REQUIRE(NGIN::IO::WriteAllText(vfs, nestedDir.Join("seed.txt"), "seed").has_value());

    auto directory = vfs.OpenDirectory(nestedDir);
    REQUIRE(directory.has_value());

    auto exists = directory->Exists(NGIN::IO::Path {"seed.txt"});
    REQUIRE(exists.has_value());
    REQUIRE(exists.value());

    auto child = directory->OpenDirectory(NGIN::IO::Path {"child"});
    REQUIRE(child.has_value());

    NGIN::IO::FileOpenOptions openOptions;
    openOptions.access      = NGIN::IO::FileAccess::Write;
    openOptions.share       = NGIN::IO::FileShare::Read;
    openOptions.disposition = NGIN::IO::FileCreateDisposition::CreateAlways;

    auto openedFile = directory->OpenFile(NGIN::IO::Path {"from_handle.txt"}, openOptions);
    REQUIRE(openedFile.has_value());

    const std::string payload     = "virtual-dir-handle";
    const auto        writeResult = openedFile->Write({reinterpret_cast<const NGIN::Byte*>(payload.data()), payload.size()});
    REQUIRE(writeResult.has_value());
    REQUIRE(writeResult.value() == payload.size());
    openedFile->Close();

    auto writtenText = NGIN::IO::ReadAllText(vfs, nestedDir.Join("from_handle.txt"));
    REQUIRE(writtenText.has_value());
    REQUIRE(std::string(writtenText.value().Data(), writtenText.value().Size()) == payload);

    REQUIRE(directory->CreateDirectory(NGIN::IO::Path {"created"}).has_value());
    REQUIRE(vfs.Exists(nestedDir.Join("created")).value());
    REQUIRE(directory->RemoveDirectory(NGIN::IO::Path {"created"}).has_value());
    REQUIRE_FALSE(vfs.Exists(nestedDir.Join("created")).value());

    REQUIRE(directory->RemoveFile(NGIN::IO::Path {"from_handle.txt"}).has_value());
    REQUIRE_FALSE(vfs.Exists(nestedDir.Join("from_handle.txt")).value());

    auto escaped = directory->Exists(NGIN::IO::Path {"../outside.txt"});
    REQUIRE_FALSE(escaped.has_value());
    REQUIRE(escaped.error().code == NGIN::IO::IOErrorCode::InvalidPath);

    RemoveTempDir(backingFs, realRoot);
}

TEST_CASE("IO.VirtualFileSystem async file operations use value handles", "[IO][VirtualFileSystem][Async]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler {1};
    NGIN::IO::Runtime                    runtime;
    NGIN::IO::LocalFileSystem            backingFs(runtime);
    const auto                           realRoot = MakeTempDir(backingFs);

    auto mount = std::make_shared<NGIN::IO::LocalMount>(runtime, realRoot, NGIN::IO::MountPoint {.virtualPrefix = NGIN::IO::Path {"/v"}});

    NGIN::IO::VirtualFileSystem vfs;
    vfs.AddMount(mount);

    const auto        virtualFile = NGIN::IO::Path {"/v"}.Join("async.txt");
    const std::string payload     = "virtual async payload";
    REQUIRE(NGIN::IO::WriteAllText(vfs, virtualFile, payload).has_value());

    auto ctx = NGIN::Async::TaskContext(scheduler);

    NGIN::IO::FileOpenOptions options;
    options.access      = NGIN::IO::FileAccess::Read;
    options.disposition = NGIN::IO::FileCreateDisposition::OpenExisting;

    auto openTask = vfs.OpenFileAsync(ctx, virtualFile, options);
    auto opened   = RunAsyncTask(openTask, ctx);
    REQUIRE(opened.Succeeded());
    auto file = std::move(opened.Value());
    REQUIRE(file.IsOpen());

    std::array<NGIN::Byte, 64> buffer {};
    auto                       readTask   = file.ReadAsync(ctx, std::span<NGIN::Byte>(buffer.data(), buffer.size()));
    auto                       readResult = RunAsyncTask(readTask, ctx);
    REQUIRE(readResult.Succeeded());
    REQUIRE(readResult.Value() == payload.size());
    REQUIRE(std::string(reinterpret_cast<const char*>(buffer.data()), readResult.Value()) == payload);

    auto closeTask   = file.CloseAsync(ctx);
    auto closeResult = RunAsyncTask(closeTask, ctx);
    REQUIRE(closeResult.Succeeded());

    auto infoTask   = vfs.GetInfoAsync(ctx, virtualFile);
    auto infoResult = RunAsyncTask(infoTask, ctx);
    REQUIRE(infoResult.Succeeded());
    REQUIRE(infoResult.Value().path.View() == virtualFile.View());
    REQUIRE(infoResult.Value().type == NGIN::IO::EntryType::File);

    const auto copiedVirtual = NGIN::IO::Path {"/v"}.Join("copied.txt");
    auto       copyTask      = vfs.CopyFileAsync(ctx, virtualFile, copiedVirtual);
    auto       copyResult    = RunAsyncTask(copyTask, ctx);
    REQUIRE(copyResult.Succeeded());

    auto copiedText = NGIN::IO::ReadAllText(vfs, copiedVirtual);
    REQUIRE(copiedText.has_value());
    REQUIRE(std::string(copiedText.value().Data(), copiedText.value().Size()) == payload);

    RemoveTempDir(backingFs, realRoot);
}

TEST_CASE("IO.VirtualFileSystem async directory handles stay mount scoped", "[IO][VirtualFileSystem][Async]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler {1};
    NGIN::IO::Runtime                    runtime;
    NGIN::IO::LocalFileSystem            backingFs(runtime);
    const auto                           realRoot = MakeTempDir(backingFs);

    auto mount = std::make_shared<NGIN::IO::LocalMount>(runtime, realRoot, NGIN::IO::MountPoint {.virtualPrefix = NGIN::IO::Path {"/v"}});

    NGIN::IO::VirtualFileSystem vfs;
    vfs.AddMount(mount);

    const auto virtualDir  = NGIN::IO::Path {"/v"}.Join("nested");
    const auto virtualFile = virtualDir.Join("inside.txt");

    REQUIRE(backingFs.CreateDirectories(realRoot.Join("nested")).has_value());
    REQUIRE(NGIN::IO::WriteAllText(vfs, virtualFile, "inside").has_value());

    auto ctx = NGIN::Async::TaskContext(scheduler);

    auto directoryTask = vfs.OpenDirectoryAsync(ctx, virtualDir);
    auto directoryOpen = RunAsyncTask(directoryTask, ctx);
    REQUIRE(directoryOpen.Succeeded());

    auto directory  = std::move(directoryOpen.Value());
    auto existsTask = directory.ExistsAsync(ctx, NGIN::IO::Path {"inside.txt"});
    auto exists     = RunAsyncTask(existsTask, ctx);
    REQUIRE(exists.Succeeded());
    REQUIRE(exists.Value());

    NGIN::IO::FileOpenOptions options;
    options.access      = NGIN::IO::FileAccess::Read;
    options.disposition = NGIN::IO::FileCreateDisposition::OpenExisting;

    auto fileTask = directory.OpenFileAsync(ctx, NGIN::IO::Path {"inside.txt"}, options);
    auto fileOpen = RunAsyncTask(fileTask, ctx);
    REQUIRE(fileOpen.Succeeded());
    auto file = std::move(fileOpen.Value());
    REQUIRE(file.IsOpen());

    auto closeTask   = file.CloseAsync(ctx);
    auto closeResult = RunAsyncTask(closeTask, ctx);
    REQUIRE(closeResult.Succeeded());

    RemoveTempDir(backingFs, realRoot);
}

TEST_CASE("IO.VirtualFileSystem async copy crosses mounts and cleans canceled destinations", "[IO][VirtualFileSystem][Async]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler {1};
    NGIN::IO::Runtime                    runtime;
    NGIN::IO::LocalFileSystem            backingFs(runtime);
    const auto                           root       = MakeTempDir(backingFs);
    const auto                           sourceRoot = root.Join("async-source");
    const auto                           targetRoot = root.Join("async-target");
    REQUIRE(backingFs.CreateDirectories(sourceRoot).has_value());
    REQUIRE(backingFs.CreateDirectories(targetRoot).has_value());

    NGIN::IO::VirtualFileSystem vfs;
    vfs.AddMount(std::make_shared<NGIN::IO::LocalMount>(runtime,
                                                        sourceRoot, NGIN::IO::MountPoint {.virtualPrefix = NGIN::IO::Path {"/source"}}));
    vfs.AddMount(std::make_shared<NGIN::IO::LocalMount>(runtime,
                                                        targetRoot, NGIN::IO::MountPoint {.virtualPrefix = NGIN::IO::Path {"/target"}}));

    REQUIRE(vfs.CreateDirectories(NGIN::IO::Path {"/source/tree/nested"}).has_value());
    REQUIRE(NGIN::IO::WriteAllText(vfs, NGIN::IO::Path {"/source/tree/nested/data.txt"}, "async-cross-mount").has_value());

    auto                  ctx = NGIN::Async::TaskContext(scheduler);
    NGIN::IO::CopyOptions recursive;
    recursive.recursive = true;
    auto copyTask       = vfs.CopyFileAsync(
            ctx, NGIN::IO::Path {"/source/tree"}, NGIN::IO::Path {"/target/tree"}, recursive);
    auto copied = RunAsyncTask(copyTask, ctx);
    REQUIRE(copied.Succeeded());
    auto text = NGIN::IO::ReadAllText(vfs, NGIN::IO::Path {"/target/tree/nested/data.txt"});
    REQUIRE(text.has_value());
    REQUIRE(std::string(text.value().Data(), text.value().Size()) == "async-cross-mount");

    NGIN::Async::CancellationSource cancellation;
    cancellation.Cancel();
    auto canceledContext = NGIN::Async::TaskContext(scheduler, cancellation.GetToken());
    auto canceledTask    = vfs.CopyFileAsync(
            canceledContext,
            NGIN::IO::Path {"/source/tree"},
            NGIN::IO::Path {"/target/canceled"},
            recursive);
    auto canceled = RunAsyncTask(canceledTask, canceledContext);
    REQUIRE(canceled.IsCanceled());
    REQUIRE_FALSE(vfs.Exists(NGIN::IO::Path {"/target/canceled"}).value());

    RemoveTempDir(backingFs, root);
}
