#pragma once

#include <NGIN/IO/IAsyncFileSystem.hpp>
#include <NGIN/IO/IFileSystem.hpp>

namespace NGIN::IO
{
    class NGIN_BASE_API LocalFileSystem final : public IFileSystem, public IAsyncFileSystem
    {
    public:
        Result<bool> Exists(const Path& path) noexcept override;
        Result<FileInfo> GetInfo(const Path& path) noexcept override;

        ResultVoid CreateDirectory(const Path& path, const DirectoryCreateOptions& options = {}) noexcept override;
        ResultVoid CreateDirectories(const Path& path, const DirectoryCreateOptions& options = {}) noexcept override;
        ResultVoid RemoveFile(const Path& path, const RemoveOptions& options = {}) noexcept override;
        ResultVoid RemoveDirectory(const Path& path, const RemoveOptions& options = {}) noexcept override;
        Result<UInt64> RemoveAll(const Path& path, const RemoveOptions& options = {}) noexcept override;

        ResultVoid Rename(const Path& from, const Path& to) noexcept override;
        ResultVoid CopyFile(const Path& from, const Path& to, const CopyOptions& options = {}) noexcept override;
        ResultVoid Move(const Path& from, const Path& to, const CopyOptions& options = {}) noexcept override;

        Result<std::unique_ptr<IFileHandle>> OpenFile(const Path& path, const FileOpenOptions& options) noexcept override;
        Result<FileView> OpenFileView(const Path& path) noexcept override;
        Result<std::unique_ptr<IDirectoryEnumerator>> Enumerate(const Path& path, const EnumerateOptions& options = {}) noexcept override;

        Result<Path> CurrentWorkingDirectory() noexcept override;
        ResultVoid   SetCurrentWorkingDirectory(const Path& path) noexcept override;
        Result<Path> TempDirectory() noexcept override;
        Result<SpaceInfo> GetSpaceInfo(const Path& path) noexcept override;

        AsyncTask<std::unique_ptr<IAsyncFileHandle>> OpenFileAsync(
                NGIN::Async::TaskContext& ctx, const Path& path, const FileOpenOptions& options) override;
        AsyncTask<FileInfo>  GetInfoAsync(NGIN::Async::TaskContext& ctx, const Path& path) override;
        AsyncTaskVoid        CopyFileAsync(NGIN::Async::TaskContext& ctx, const Path& from, const Path& to, const CopyOptions& options = {}) override;
    };
}// namespace NGIN::IO

