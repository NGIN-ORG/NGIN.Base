#pragma once

#include <NGIN/IO/IAsyncFileSystem.hpp>
#include <NGIN/IO/IFileSystem.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>

#include <memory>
#include <vector>

namespace NGIN::IO
{
    /// @brief Routing policy for one virtual-filesystem mount.
    struct MountPoint
    {
        Path  virtualPrefix {"/"};
        Int32 priority {0};
        bool  readOnly {false};
        bool  caseSensitive {false};
        bool  allowShadowing {true};
    };

    /// @brief Translation and backend-access contract for a virtual mount.
    class NGIN_IO_API IVirtualMount
    {
    public:
        /// @brief Destroys the mount interface.
        virtual ~IVirtualMount() = default;

        /// @brief Returns the mount's routing policy.
        [[nodiscard]] virtual const MountPoint& GetMountPoint() const noexcept = 0;
        /// @brief Returns whether this mount can resolve a virtual path.
        [[nodiscard]] virtual bool CanResolve(const Path& virtualPath) const noexcept = 0;
        /// @brief Translates a virtual path to the backend's path space.
        virtual Result<Path> Translate(const Path& virtualPath) noexcept = 0;
        /// @brief Translates a backend path into this mount's virtual path space.
        virtual Result<Path> Virtualize(const Path& realPath) noexcept = 0;
        /// @brief Returns the synchronous filesystem serving this mount.
        [[nodiscard]] virtual IFileSystem& GetFileSystem() noexcept = 0;
        /// @brief Returns the asynchronous filesystem serving this mount, or null when unsupported.
        [[nodiscard]] virtual IAsyncFileSystem* GetAsyncFileSystem() noexcept = 0;
    };

    /// @brief Virtual mount backed by a directory in the platform-local filesystem.
    class NGIN_IO_API LocalMount final : public IVirtualMount
    {
    public:
        /// @brief Creates a mount translating @p mountPoint into @p realRoot.
        LocalMount(Path realRoot, MountPoint mountPoint = {});

        /// @copydoc IVirtualMount::GetMountPoint
        [[nodiscard]] const MountPoint& GetMountPoint() const noexcept override;
        /// @copydoc IVirtualMount::CanResolve
        [[nodiscard]] bool CanResolve(const Path& virtualPath) const noexcept override;
        /// @copydoc IVirtualMount::Translate
        Result<Path> Translate(const Path& virtualPath) noexcept override;
        /// @copydoc IVirtualMount::Virtualize
        Result<Path> Virtualize(const Path& realPath) noexcept override;
        /// @copydoc IVirtualMount::GetFileSystem
        [[nodiscard]] IFileSystem& GetFileSystem() noexcept override;
        /// @copydoc IVirtualMount::GetAsyncFileSystem
        [[nodiscard]] IAsyncFileSystem* GetAsyncFileSystem() noexcept override;

    private:
        Path            m_realRoot;
        MountPoint      m_mountPoint;
        LocalFileSystem m_localFileSystem {};
    };

    /// @brief Priority-ordered filesystem router that exposes mounted backends in one virtual namespace.
    class NGIN_IO_API VirtualFileSystem final : public IFileSystem, public IAsyncFileSystem
    {
    public:
        /// @brief Adds a mount and reorders routing by virtual-prefix specificity and priority.
        void AddMount(std::shared_ptr<IVirtualMount> mount);
        /// @brief Removes all mounts; outstanding handles remain owned by their backends.
        void ClearMounts() noexcept;
        /// @brief Returns the number of registered mounts.
        [[nodiscard]] std::size_t MountCount() const noexcept { return m_mounts.size(); }

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
        struct ResolvedMount
        {
            IVirtualMount* mount {nullptr};
            Path           translatedPath {};
        };

        Result<ResolvedMount>                       ResolvePath(const Path& virtualPath) noexcept;
        std::vector<std::shared_ptr<IVirtualMount>> m_mounts {};
    };
}// namespace NGIN::IO
