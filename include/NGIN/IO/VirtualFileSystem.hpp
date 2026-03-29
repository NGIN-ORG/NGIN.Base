#pragma once

#include <NGIN/IO/IAsyncFileSystem.hpp>
#include <NGIN/IO/IFileSystem.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>

#include <memory>
#include <vector>

namespace NGIN::IO
{
    struct MountPoint
    {
        Path  virtualPrefix {"/"};
        Int32 priority {0};
        bool  readOnly {false};
        bool  caseSensitive {false};
        bool  allowShadowing {true};
    };

    class NGIN_BASE_API IVirtualMount
    {
    public:
        virtual ~IVirtualMount() = default;

        [[nodiscard]] virtual const MountPoint& GetMountPoint() const noexcept                     = 0;
        [[nodiscard]] virtual bool              CanResolve(const Path& virtualPath) const noexcept = 0;
        virtual Result<Path>                    Translate(const Path& virtualPath) noexcept        = 0;
        virtual Result<Path>                    Virtualize(const Path& realPath) noexcept          = 0;
        [[nodiscard]] virtual IFileSystem&      GetFileSystem() noexcept                           = 0;
        [[nodiscard]] virtual IAsyncFileSystem* GetAsyncFileSystem() noexcept                      = 0;
    };

    class NGIN_BASE_API LocalMount final : public IVirtualMount
    {
    public:
        LocalMount(Path realRoot, MountPoint mountPoint = {});

        [[nodiscard]] const MountPoint& GetMountPoint() const noexcept override;
        [[nodiscard]] bool              CanResolve(const Path& virtualPath) const noexcept override;
        Result<Path>                    Translate(const Path& virtualPath) noexcept override;
        Result<Path>                    Virtualize(const Path& realPath) noexcept override;
        [[nodiscard]] IFileSystem&      GetFileSystem() noexcept override;
        [[nodiscard]] IAsyncFileSystem* GetAsyncFileSystem() noexcept override;

    private:
        Path            m_realRoot;
        MountPoint      m_mountPoint;
        LocalFileSystem m_localFileSystem {};
    };

    class NGIN_BASE_API VirtualFileSystem final : public IFileSystem, public IAsyncFileSystem
    {
    public:
        void                      AddMount(std::shared_ptr<IVirtualMount> mount);
        void                      ClearMounts() noexcept;
        [[nodiscard]] std::size_t MountCount() const noexcept { return m_mounts.size(); }

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

        Result<FileHandle>          OpenFile(const Path& path, const FileOpenOptions& options) noexcept override;
        Result<DirectoryHandle>     OpenDirectory(const Path& path) noexcept override;
        Result<FileView>            OpenFileView(const Path& path) noexcept override;
        Result<DirectoryEnumerator> Enumerate(const Path& path, const EnumerateOptions& options = {}) noexcept override;

        Result<Path>      CurrentWorkingDirectory() noexcept override;
        ResultVoid        SetCurrentWorkingDirectory(const Path& path) noexcept override;
        Result<Path>      TempDirectory() noexcept override;
        Result<Path>      CreateTempDirectory(const Path& directory = {}, std::string_view prefix = "ngin") noexcept override;
        Result<Path>      CreateTempFile(const Path& directory = {}, std::string_view prefix = "ngin") noexcept override;
        Result<SpaceInfo> GetSpaceInfo(const Path& path) noexcept override;

        AsyncTask<AsyncFileHandle> OpenFileAsync(
                NGIN::Async::TaskContext& ctx, const Path& path, const FileOpenOptions& options) override;
        AsyncTask<AsyncDirectoryHandle> OpenDirectoryAsync(
                NGIN::Async::TaskContext& ctx, const Path& path) override;
        AsyncTask<FileInfo> GetInfoAsync(
                NGIN::Async::TaskContext& ctx, const Path& path, const MetadataOptions& options = {}) override;
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
