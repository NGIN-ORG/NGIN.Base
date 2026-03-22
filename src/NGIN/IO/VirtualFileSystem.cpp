#include <NGIN/IO/VirtualFileSystem.hpp>

#include <NGIN/Utilities/Expected.hpp>

#include <algorithm>

namespace NGIN::IO
{
    namespace
    {
        [[nodiscard]] IOError MakeError(IOErrorCode code, std::string_view message, const Path& path = {}, const Path& secondary = {}) noexcept
        {
            IOError error;
            error.code = code;
            error.path = path;
            error.secondaryPath = secondary;
            error.message = message;
            return error;
        }

        [[nodiscard]] bool PrefixCompare(const MountPoint& a, const MountPoint& b) noexcept
        {
            const auto aLen = a.virtualPrefix.View().size();
            const auto bLen = b.virtualPrefix.View().size();
            if (aLen != bLen)
                return aLen > bLen;
            if (a.priority != b.priority)
                return a.priority > b.priority;
            return a.virtualPrefix.View() < b.virtualPrefix.View();
        }

        [[nodiscard]] FileSystemCapabilities IntersectCapabilities(
                const FileSystemCapabilities& lhs, const FileSystemCapabilities& rhs) noexcept
        {
            FileSystemCapabilities out;
            out.symlinks             = lhs.symlinks && rhs.symlinks;
            out.hardLinks            = lhs.hardLinks && rhs.hardLinks;
            out.blockDevices         = lhs.blockDevices && rhs.blockDevices;
            out.characterDevices     = lhs.characterDevices && rhs.characterDevices;
            out.fifos                = lhs.fifos && rhs.fifos;
            out.sockets              = lhs.sockets && rhs.sockets;
            out.posixModeBits        = lhs.posixModeBits && rhs.posixModeBits;
            out.ownership            = lhs.ownership && rhs.ownership;
            out.setIdBits            = lhs.setIdBits && rhs.setIdBits;
            out.stickyBit            = lhs.stickyBit && rhs.stickyBit;
            out.fileIdentity         = lhs.fileIdentity && rhs.fileIdentity;
            out.hardLinkCount        = lhs.hardLinkCount && rhs.hardLinkCount;
            out.memoryMappedFiles    = lhs.memoryMappedFiles && rhs.memoryMappedFiles;
            out.nanosecondTimestamps = lhs.nanosecondTimestamps && rhs.nanosecondTimestamps;
            out.metadataNoFollow     = lhs.metadataNoFollow && rhs.metadataNoFollow;
            return out;
        }
    }

    LocalMount::LocalMount(Path realRoot, MountPoint mountPoint)
        : m_realRoot(std::move(realRoot))
        , m_mountPoint(std::move(mountPoint))
    {
        m_realRoot.Normalize();
        if (m_mountPoint.virtualPrefix.IsEmpty())
            m_mountPoint.virtualPrefix = Path{"/"};
        m_mountPoint.virtualPrefix.Normalize();
    }

    const MountPoint& LocalMount::GetMountPoint() const noexcept
    {
        return m_mountPoint;
    }

    bool LocalMount::CanResolve(const Path& virtualPath) const noexcept
    {
        Path normalized = virtualPath.LexicallyNormal();
        return normalized.StartsWith(m_mountPoint.virtualPrefix);
    }

    Result<Path> LocalMount::Translate(const Path& virtualPath) noexcept
    {
        Path normalized = virtualPath.LexicallyNormal();
        if (!normalized.StartsWith(m_mountPoint.virtualPrefix))
        {
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidPath, "path does not match mount", virtualPath)));
        }

        const auto full = normalized.View();
        const auto prefix = m_mountPoint.virtualPrefix.View();

        std::string_view suffix {};
        if (full.size() > prefix.size())
        {
            suffix = full.substr(prefix.size());
            while (!suffix.empty() && (suffix.front() == '/' || suffix.front() == '\\'))
                suffix.remove_prefix(1);
        }

        Path out = m_realRoot;
        if (!suffix.empty())
            out.Append(suffix);
        out.Normalize();

        if (!out.StartsWith(m_realRoot))
        {
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidPath, "resolved path escapes mount root", virtualPath)));
        }

        return Result<Path>(std::move(out));
    }

    IFileSystem& LocalMount::GetFileSystem() noexcept
    {
        return m_localFileSystem;
    }

    IAsyncFileSystem* LocalMount::GetAsyncFileSystem() noexcept
    {
        return &m_localFileSystem;
    }

    void VirtualFileSystem::AddMount(std::shared_ptr<IVirtualMount> mount)
    {
        if (!mount)
            return;
        m_mounts.push_back(std::move(mount));
        std::sort(m_mounts.begin(), m_mounts.end(), [](const std::shared_ptr<IVirtualMount>& a, const std::shared_ptr<IVirtualMount>& b) {
            return PrefixCompare(a->GetMountPoint(), b->GetMountPoint());
        });
    }

    void VirtualFileSystem::ClearMounts() noexcept
    {
        m_mounts.clear();
    }

    FileSystemCapabilities VirtualFileSystem::GetCapabilities() const noexcept
    {
        if (m_mounts.empty())
            return {};

        FileSystemCapabilities capabilities = m_mounts.front()->GetFileSystem().GetCapabilities();
        for (std::size_t i = 1; i < m_mounts.size(); ++i)
        {
            capabilities = IntersectCapabilities(capabilities, m_mounts[i]->GetFileSystem().GetCapabilities());
        }
        return capabilities;
    }

    Result<VirtualFileSystem::ResolvedMount> VirtualFileSystem::ResolvePath(const Path& virtualPath) noexcept
    {
        for (auto& mount: m_mounts)
        {
            if (!mount || !mount->CanResolve(virtualPath))
                continue;

            auto translated = mount->Translate(virtualPath);
            if (!translated.HasValue())
            {
                return Result<ResolvedMount>(NGIN::Utilities::Unexpected<IOError>(std::move(translated.ErrorUnsafe())));
            }
            ResolvedMount out;
            out.mount = mount.get();
            out.translatedPath = std::move(translated.ValueUnsafe());
            return Result<ResolvedMount>(std::move(out));
        }
        return Result<ResolvedMount>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "no mount for virtual path", virtualPath)));
    }

    Result<bool> VirtualFileSystem::Exists(const Path& path) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<bool>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.ErrorUnsafe())));
        return resolved.ValueUnsafe().mount->GetFileSystem().Exists(resolved.ValueUnsafe().translatedPath);
    }

    Result<FileInfo> VirtualFileSystem::GetInfo(const Path& path, const MetadataOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<FileInfo>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.ErrorUnsafe())));
        auto info = resolved.ValueUnsafe().mount->GetFileSystem().GetInfo(resolved.ValueUnsafe().translatedPath, options);
        if (info.HasValue())
            info.ValueUnsafe().path = path;
        return info;
    }

    ResultVoid VirtualFileSystem::CreateDirectory(const Path& path, const DirectoryCreateOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.ErrorUnsafe())));
        if (resolved.ValueUnsafe().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.ValueUnsafe().mount->GetFileSystem().CreateDirectory(resolved.ValueUnsafe().translatedPath, options);
    }

    ResultVoid VirtualFileSystem::CreateDirectories(const Path& path, const DirectoryCreateOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.ErrorUnsafe())));
        if (resolved.ValueUnsafe().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.ValueUnsafe().mount->GetFileSystem().CreateDirectories(resolved.ValueUnsafe().translatedPath, options);
    }

    ResultVoid VirtualFileSystem::RemoveFile(const Path& path, const RemoveOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.ErrorUnsafe())));
        if (resolved.ValueUnsafe().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.ValueUnsafe().mount->GetFileSystem().RemoveFile(resolved.ValueUnsafe().translatedPath, options);
    }

    ResultVoid VirtualFileSystem::RemoveDirectory(const Path& path, const RemoveOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.ErrorUnsafe())));
        if (resolved.ValueUnsafe().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.ValueUnsafe().mount->GetFileSystem().RemoveDirectory(resolved.ValueUnsafe().translatedPath, options);
    }

    Result<UInt64> VirtualFileSystem::RemoveAll(const Path& path, const RemoveOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.ErrorUnsafe())));
        if (resolved.ValueUnsafe().mount->GetMountPoint().readOnly)
            return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.ValueUnsafe().mount->GetFileSystem().RemoveAll(resolved.ValueUnsafe().translatedPath, options);
    }

    ResultVoid VirtualFileSystem::Rename(const Path& from, const Path& to) noexcept
    {
        auto fromResolved = ResolvePath(from);
        if (!fromResolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(fromResolved.ErrorUnsafe())));
        auto toResolved = ResolvePath(to);
        if (!toResolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(toResolved.ErrorUnsafe())));
        if (fromResolved.ValueUnsafe().mount != toResolved.ValueUnsafe().mount)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::CrossDevice, "cross-mount rename not supported", from, to)));
        if (fromResolved.ValueUnsafe().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", from, to)));
        return fromResolved.ValueUnsafe().mount->GetFileSystem().Rename(fromResolved.ValueUnsafe().translatedPath, toResolved.ValueUnsafe().translatedPath);
    }

    ResultVoid VirtualFileSystem::CopyFile(const Path& from, const Path& to, const CopyOptions& options) noexcept
    {
        auto fromResolved = ResolvePath(from);
        if (!fromResolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(fromResolved.ErrorUnsafe())));
        auto toResolved = ResolvePath(to);
        if (!toResolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(toResolved.ErrorUnsafe())));
        if (toResolved.ValueUnsafe().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "destination mount is read-only", from, to)));
        if (fromResolved.ValueUnsafe().mount == toResolved.ValueUnsafe().mount)
        {
            return fromResolved.ValueUnsafe().mount->GetFileSystem().CopyFile(fromResolved.ValueUnsafe().translatedPath, toResolved.ValueUnsafe().translatedPath, options);
        }
        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::CrossDevice, "cross-mount copy not implemented in v1", from, to)));
    }

    ResultVoid VirtualFileSystem::Move(const Path& from, const Path& to, const CopyOptions& options) noexcept
    {
        auto fromResolved = ResolvePath(from);
        if (!fromResolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(fromResolved.ErrorUnsafe())));
        auto toResolved = ResolvePath(to);
        if (!toResolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(toResolved.ErrorUnsafe())));
        if (toResolved.ValueUnsafe().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "destination mount is read-only", from, to)));
        if (fromResolved.ValueUnsafe().mount != toResolved.ValueUnsafe().mount)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::CrossDevice, "cross-mount move not implemented in v1", from, to)));
        return fromResolved.ValueUnsafe().mount->GetFileSystem().Move(fromResolved.ValueUnsafe().translatedPath, toResolved.ValueUnsafe().translatedPath, options);
    }

    Result<std::unique_ptr<IFileHandle>> VirtualFileSystem::OpenFile(const Path& path, const FileOpenOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<std::unique_ptr<IFileHandle>>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.ErrorUnsafe())));
        if ((options.access == FileAccess::Write || options.access == FileAccess::ReadWrite || options.access == FileAccess::Append) &&
            resolved.ValueUnsafe().mount->GetMountPoint().readOnly)
        {
            return Result<std::unique_ptr<IFileHandle>>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        }
        return resolved.ValueUnsafe().mount->GetFileSystem().OpenFile(resolved.ValueUnsafe().translatedPath, options);
    }

    Result<FileView> VirtualFileSystem::OpenFileView(const Path& path) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<FileView>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.ErrorUnsafe())));
        return resolved.ValueUnsafe().mount->GetFileSystem().OpenFileView(resolved.ValueUnsafe().translatedPath);
    }

    Result<std::unique_ptr<IDirectoryEnumerator>> VirtualFileSystem::Enumerate(const Path& path, const EnumerateOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<std::unique_ptr<IDirectoryEnumerator>>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.ErrorUnsafe())));
        return resolved.ValueUnsafe().mount->GetFileSystem().Enumerate(resolved.ValueUnsafe().translatedPath, options);
    }

    Result<Path> VirtualFileSystem::CurrentWorkingDirectory() noexcept
    {
        return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::Unsupported, "virtual cwd is not defined")));
    }

    ResultVoid VirtualFileSystem::SetCurrentWorkingDirectory(const Path& path) noexcept
    {
        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::Unsupported, "virtual cwd is not supported", path)));
    }

    Result<Path> VirtualFileSystem::TempDirectory() noexcept
    {
        if (m_mounts.empty())
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "no mounts configured")));
        return m_mounts.front()->GetFileSystem().TempDirectory();
    }

    Result<SpaceInfo> VirtualFileSystem::GetSpaceInfo(const Path& path) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<SpaceInfo>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.ErrorUnsafe())));
        return resolved.ValueUnsafe().mount->GetFileSystem().GetSpaceInfo(resolved.ValueUnsafe().translatedPath);
    }

    AsyncTask<std::unique_ptr<IAsyncFileHandle>> VirtualFileSystem::OpenFileAsync(
            NGIN::Async::TaskContext& ctx, const Path& path, const FileOpenOptions& options)
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
        {
            co_return AsyncResult<std::unique_ptr<IAsyncFileHandle>>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.ErrorUnsafe())));
        }
        auto* asyncFs = resolved.ValueUnsafe().mount->GetAsyncFileSystem();
        if (!asyncFs)
        {
            co_return AsyncResult<std::unique_ptr<IAsyncFileHandle>>(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::Unsupported, "mount has no async filesystem", path)));
        }
        co_return co_await asyncFs->OpenFileAsync(ctx, resolved.ValueUnsafe().translatedPath, options);
    }

    AsyncTask<FileInfo> VirtualFileSystem::GetInfoAsync(
            NGIN::Async::TaskContext& ctx, const Path& path, const MetadataOptions& options)
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
        {
            co_return AsyncResult<FileInfo>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.ErrorUnsafe())));
        }
        auto* asyncFs = resolved.ValueUnsafe().mount->GetAsyncFileSystem();
        if (!asyncFs)
        {
            co_return AsyncResult<FileInfo>(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::Unsupported, "mount has no async filesystem", path)));
        }
        auto awaited = co_await asyncFs->GetInfoAsync(ctx, resolved.ValueUnsafe().translatedPath, options);
        if (!awaited)
        {
            co_await AsyncTask<FileInfo>::ReturnError(awaited.error());
            co_return AsyncResult<FileInfo>(FileInfo {});
        }
        auto info = std::move(*awaited);
        if (info)
            info.ValueUnsafe().path = path;
        co_return std::move(info);
    }

    AsyncTaskVoid VirtualFileSystem::CopyFileAsync(
            NGIN::Async::TaskContext& ctx, const Path& from, const Path& to, const CopyOptions& options)
    {
        auto fromResolved = ResolvePath(from);
        if (!fromResolved.HasValue())
            co_return AsyncResult<void>(NGIN::Utilities::Unexpected<IOError>(std::move(fromResolved.ErrorUnsafe())));
        auto toResolved = ResolvePath(to);
        if (!toResolved.HasValue())
            co_return AsyncResult<void>(NGIN::Utilities::Unexpected<IOError>(std::move(toResolved.ErrorUnsafe())));
        if (fromResolved.ValueUnsafe().mount != toResolved.ValueUnsafe().mount)
            co_return AsyncResult<void>(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::CrossDevice, "cross-mount async copy not implemented", from, to)));
        auto* asyncFs = fromResolved.ValueUnsafe().mount->GetAsyncFileSystem();
        if (!asyncFs)
            co_return AsyncResult<void>(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::Unsupported, "mount has no async filesystem", from, to)));
        co_return co_await asyncFs->CopyFileAsync(ctx, fromResolved.ValueUnsafe().translatedPath, toResolved.ValueUnsafe().translatedPath, options);
    }
}// namespace NGIN::IO
