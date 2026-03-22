#pragma once

#include <NGIN/IO/IAsyncFileSystem.hpp>
#include <NGIN/IO/IFileSystem.hpp>

namespace NGIN::IO
{
    class NGIN_BASE_API LocalFileSystem final : public IFileSystem, public IAsyncFileSystem
    {
    public:
        [[nodiscard]] FileSystemCapabilities GetCapabilities() const noexcept override;
        Result<bool>                         Exists(const Path& path) noexcept override;
        Result<FileInfo>                     GetInfo(const Path& path, const MetadataOptions& options = {}) noexcept override;
        Result<Path>                         Absolute(const Path& path, const Path& base = {}) noexcept override;
        Result<Path>                         Canonical(const Path& path) noexcept override;
        Result<Path>                         WeaklyCanonical(const Path& path) noexcept override;
        Result<bool>                         SameFile(const Path& lhs, const Path& rhs) noexcept override;
        Result<Path>                         ReadSymlink(const Path& path) noexcept override;

        ResultVoid     CreateDirectory(const Path& path, const DirectoryCreateOptions& options = {}) noexcept override;
        ResultVoid     CreateDirectories(const Path& path, const DirectoryCreateOptions& options = {}) noexcept override;
        ResultVoid     CreateSymlink(const Path& target, const Path& linkPath) noexcept override;
        ResultVoid     CreateHardLink(const Path& target, const Path& linkPath) noexcept override;
        ResultVoid     SetPermissions(const Path& path, const FilePermissions& permissions, const SymlinkMode symlinkMode = SymlinkMode::Follow) noexcept override;
        ResultVoid     RemoveFile(const Path& path, const RemoveOptions& options = {}) noexcept override;
        ResultVoid     RemoveDirectory(const Path& path, const RemoveOptions& options = {}) noexcept override;
        Result<UInt64> RemoveAll(const Path& path, const RemoveOptions& options = {}) noexcept override;

        ResultVoid Rename(const Path& from, const Path& to) noexcept override;
        ResultVoid ReplaceFile(const Path& source, const Path& destination) noexcept override;
        ResultVoid CopyFile(const Path& from, const Path& to, const CopyOptions& options = {}) noexcept override;
        ResultVoid Move(const Path& from, const Path& to, const CopyOptions& options = {}) noexcept override;

        Result<std::unique_ptr<IFileHandle>>          OpenFile(const Path& path, const FileOpenOptions& options) noexcept override;
        Result<std::unique_ptr<IDirectoryHandle>>     OpenDirectory(const Path& path) noexcept override;
        Result<FileView>                              OpenFileView(const Path& path) noexcept override;
        Result<std::unique_ptr<IDirectoryEnumerator>> Enumerate(const Path& path, const EnumerateOptions& options = {}) noexcept override;

        Result<Path>      CurrentWorkingDirectory() noexcept override;
        ResultVoid        SetCurrentWorkingDirectory(const Path& path) noexcept override;
        Result<Path>      TempDirectory() noexcept override;
        Result<Path>      CreateTempDirectory(const Path& directory = {}, std::string_view prefix = "ngin") noexcept override;
        Result<Path>      CreateTempFile(const Path& directory = {}, std::string_view prefix = "ngin") noexcept override;
        Result<SpaceInfo> GetSpaceInfo(const Path& path) noexcept override;

        AsyncTask<std::unique_ptr<IAsyncFileHandle>> OpenFileAsync(
                NGIN::Async::TaskContext& ctx, const Path& path, const FileOpenOptions& options) override;
        AsyncTask<FileInfo> GetInfoAsync(
                NGIN::Async::TaskContext& ctx, const Path& path, const MetadataOptions& options = {}) override;
        AsyncTaskVoid CopyFileAsync(NGIN::Async::TaskContext& ctx, const Path& from, const Path& to, const CopyOptions& options = {}) override;
    };
}// namespace NGIN::IO
