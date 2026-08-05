#pragma once

#include <NGIN/IO/FileSystemDriver.hpp>
#include <NGIN/IO/IAsyncFileSystem.hpp>
#include <NGIN/IO/IFileSystem.hpp>

#include <memory>

namespace NGIN::IO
{
    /// @brief Platform-local implementation of the synchronous and asynchronous filesystem contracts.
    class NGIN_IO_API LocalFileSystem final : public IFileSystem, public IAsyncFileSystem
    {
    public:
        /// @brief Constructs a local filesystem with no asynchronous driver bound.
        LocalFileSystem();
        /// @brief Constructs a local filesystem bound to an asynchronous driver.
        explicit LocalFileSystem(std::shared_ptr<FileSystemDriver> asyncDriver);

        /// @brief Replaces the driver used by subsequently created asynchronous operations.
        void BindAsyncDriver(std::shared_ptr<FileSystemDriver> asyncDriver) noexcept;
        /// @brief Returns the currently bound asynchronous driver, if any.
        [[nodiscard]] const std::shared_ptr<FileSystemDriver>& GetAsyncDriver() const noexcept { return m_asyncDriver; }

        /// @copydoc IFileSystem::GetCapabilities
        [[nodiscard]] FileSystemCapabilities GetCapabilities() const noexcept override;
        /// @copydoc IFileSystem::Exists
        Result<bool> Exists(const Path& path) noexcept override;
        /// @copydoc IFileSystem::GetInfo
        Result<FileInfo> GetInfo(const Path& path, const MetadataOptions& options = {}) noexcept override;
        /// @copydoc IFileSystem::Absolute
        Result<Path> Absolute(const Path& path, const Path& base = {}) noexcept override;
        /// @copydoc IFileSystem::Canonical
        Result<Path> Canonical(const Path& path) noexcept override;
        /// @copydoc IFileSystem::WeaklyCanonical
        Result<Path> WeaklyCanonical(const Path& path) noexcept override;
        /// @copydoc IFileSystem::SameFile
        Result<bool> SameFile(const Path& lhs, const Path& rhs) noexcept override;
        /// @copydoc IFileSystem::ReadSymlink
        Result<Path> ReadSymlink(const Path& path) noexcept override;

        /// @copydoc IFileSystem::CreateDirectory
        ResultVoid CreateDirectory(const Path& path, const DirectoryCreateOptions& options = {}) noexcept override;
        /// @copydoc IFileSystem::CreateDirectories
        ResultVoid CreateDirectories(const Path& path, const DirectoryCreateOptions& options = {}) noexcept override;
        /// @copydoc IFileSystem::CreateSymlink
        ResultVoid CreateSymlink(const Path& target, const Path& linkPath) noexcept override;
        /// @copydoc IFileSystem::CreateHardLink
        ResultVoid CreateHardLink(const Path& target, const Path& linkPath) noexcept override;
        /// @copydoc IFileSystem::SetPermissions
        ResultVoid SetPermissions(const Path& path, const FilePermissions& permissions, const SymlinkMode symlinkMode = SymlinkMode::Follow) noexcept override;
        /// @copydoc IFileSystem::RemoveFile
        ResultVoid RemoveFile(const Path& path, const RemoveOptions& options = {}) noexcept override;
        /// @copydoc IFileSystem::RemoveDirectory
        ResultVoid RemoveDirectory(const Path& path, const RemoveOptions& options = {}) noexcept override;
        /// @copydoc IFileSystem::RemoveAll
        Result<UInt64> RemoveAll(const Path& path, const RemoveOptions& options = {}) noexcept override;

        /// @copydoc IFileSystem::Rename
        ResultVoid Rename(const Path& from, const Path& to) noexcept override;
        /// @copydoc IFileSystem::RenameNoReplace
        ResultVoid RenameNoReplace(const Path& from, const Path& to) noexcept override;
        /// @copydoc IFileSystem::ReplaceFile
        ResultVoid ReplaceFile(const Path& source, const Path& destination, const ReplaceOptions& options = {}) noexcept override;
        /// @copydoc IFileSystem::CopyFile
        ResultVoid CopyFile(const Path& from, const Path& to, const CopyOptions& options = {}) noexcept override;
        /// @copydoc IFileSystem::Move
        ResultVoid Move(const Path& from, const Path& to, const CopyOptions& options = {}) noexcept override;

        /// @copydoc IFileSystem::OpenFile
        Result<FileHandle> OpenFile(const Path& path, const FileOpenOptions& options) noexcept override;
        /// @copydoc IFileSystem::OpenDirectory
        Result<DirectoryHandle> OpenDirectory(const Path& path) noexcept override;
        /// @copydoc IFileSystem::OpenFileView
        Result<FileView> OpenFileView(const Path& path) noexcept override;
        /// @copydoc IFileSystem::Enumerate
        Result<DirectoryEnumerator> Enumerate(const Path& path, const EnumerateOptions& options = {}) noexcept override;

        /// @copydoc IFileSystem::CurrentWorkingDirectory
        Result<Path> CurrentWorkingDirectory() noexcept override;
        /// @copydoc IFileSystem::SetCurrentWorkingDirectory
        ResultVoid SetCurrentWorkingDirectory(const Path& path) noexcept override;
        /// @copydoc IFileSystem::TempDirectory
        Result<Path> TempDirectory() noexcept override;
        /// @copydoc IFileSystem::CreateTempDirectory
        Result<Path> CreateTempDirectory(const Path& directory = {}, std::string_view prefix = "ngin") noexcept override;
        /// @copydoc IFileSystem::CreateTempFile
        Result<Path> CreateTempFile(const Path& directory = {}, std::string_view prefix = "ngin") noexcept override;
        /// @copydoc IFileSystem::GetSpaceInfo
        Result<SpaceInfo> GetSpaceInfo(const Path& path) noexcept override;

        /// @copydoc IAsyncFileSystem::OpenFileAsync
        AsyncTask<AsyncFileHandle> OpenFileAsync(
                NGIN::Async::TaskContext& ctx, const Path& path, const FileOpenOptions& options) override;
        /// @copydoc IAsyncFileSystem::OpenDirectoryAsync
        AsyncTask<AsyncDirectoryHandle> OpenDirectoryAsync(
                NGIN::Async::TaskContext& ctx, const Path& path) override;
        /// @copydoc IAsyncFileSystem::GetInfoAsync
        AsyncTask<FileInfo> GetInfoAsync(
                NGIN::Async::TaskContext& ctx, const Path& path, const MetadataOptions& options = {}) override;
        /// @copydoc IAsyncFileSystem::CopyFileAsync
        AsyncTaskVoid CopyFileAsync(NGIN::Async::TaskContext& ctx, const Path& from, const Path& to, const CopyOptions& options = {}) override;

    private:
        std::shared_ptr<FileSystemDriver> m_asyncDriver;
    };
}// namespace NGIN::IO
