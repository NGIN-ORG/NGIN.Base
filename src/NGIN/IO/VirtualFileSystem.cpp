#include <NGIN/IO/VirtualFileSystem.hpp>

#include <NGIN/Utilities/Expected.hpp>

#include <algorithm>
#include <new>

namespace NGIN::IO
{
    namespace
    {
        [[nodiscard]] IOError MakeError(IOErrorCode code, std::string_view message, const Path& path = {}, const Path& secondary = {}) noexcept
        {
            IOError error;
            error.code          = code;
            error.path          = path;
            error.secondaryPath = secondary;
            error.message       = message;
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

        [[nodiscard]] Result<Path> NormalizeRelativeHandlePath(const Path& path) noexcept
        {
            Path normalized = path.LexicallyNormal();
            if (normalized.IsAbsolute())
            {
                return Result<Path>(
                        NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidPath, "directory handle path must be relative", path)));
            }

            if (normalized.IsEmpty())
                return Result<Path>(Path {"."});
            if (normalized.StartsWith(Path {".."}))
            {
                return Result<Path>(
                        NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidPath, "directory handle path escapes handle root", path)));
            }

            return Result<Path>(std::move(normalized));
        }

        [[nodiscard]] Path JoinHandlePath(const Path& base, const Path& relativePath)
        {
            if (relativePath.View() == ".")
                return base;
            Path joined = base.Join(relativePath.View());
            joined.Normalize();
            return joined;
        }

        class VirtualDirectoryHandle final : public IDirectoryHandle
        {
        public:
            static Result<std::unique_ptr<VirtualDirectoryHandle>> Open(VirtualFileSystem& fileSystem, const Path& path) noexcept
            {
                auto info = fileSystem.GetInfo(path);
                if (!info.HasValue())
                {
                    return Result<std::unique_ptr<VirtualDirectoryHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(std::move(info.Error())));
                }
                if (!info.Value().exists)
                {
                    return Result<std::unique_ptr<VirtualDirectoryHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "directory not found", path)));
                }
                if (info.Value().type != EntryType::Directory)
                {
                    return Result<std::unique_ptr<VirtualDirectoryHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotDirectory, "path is not a directory", path)));
                }

                try
                {
                    auto handle      = std::unique_ptr<VirtualDirectoryHandle>(new VirtualDirectoryHandle());
                    handle->m_path   = path.LexicallyNormal();
                    handle->m_system = &fileSystem;
                    return Result<std::unique_ptr<VirtualDirectoryHandle>>(std::move(handle));
                } catch (const std::bad_alloc&)
                {
                    return Result<std::unique_ptr<VirtualDirectoryHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "allocation failed", path)));
                }
            }

            Result<bool> Exists(const Path& path) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return Result<bool>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));
                return m_system->Exists(JoinHandlePath(m_path, normalized.Value()));
            }

            Result<FileInfo> GetInfo(const Path& path, const MetadataOptions& options) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return Result<FileInfo>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));
                return m_system->GetInfo(JoinHandlePath(m_path, normalized.Value()), options);
            }

            Result<FileHandle> OpenFile(const Path& path, const FileOpenOptions& options) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                {
                    return Result<FileHandle>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));
                }
                return m_system->OpenFile(JoinHandlePath(m_path, normalized.Value()), options);
            }

            Result<DirectoryHandle> OpenDirectory(const Path& path) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                {
                    return Result<DirectoryHandle>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));
                }

                auto opened = Open(*m_system, JoinHandlePath(m_path, normalized.Value()));
                if (!opened.HasValue())
                {
                    return Result<DirectoryHandle>(NGIN::Utilities::Unexpected<IOError>(std::move(opened.Error())));
                }

                return Result<DirectoryHandle>(DirectoryHandle(std::move(opened).TakeValue()));
            }

            ResultVoid CreateDirectory(const Path& path, const DirectoryCreateOptions& options = {}) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));

                const Path resolvedPath = JoinHandlePath(m_path, normalized.Value());
                if (options.recursive)
                    return m_system->CreateDirectories(resolvedPath, options);
                return m_system->CreateDirectory(resolvedPath, options);
            }

            ResultVoid RemoveFile(const Path& path, const RemoveOptions& options = {}) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));
                return m_system->RemoveFile(JoinHandlePath(m_path, normalized.Value()), options);
            }

            ResultVoid RemoveDirectory(const Path& path, const RemoveOptions& options = {}) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));
                return m_system->RemoveDirectory(JoinHandlePath(m_path, normalized.Value()), options);
            }

            Result<Path> ReadSymlink(const Path& path) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));
                return m_system->ReadSymlink(JoinHandlePath(m_path, normalized.Value()));
            }

        private:
            Path               m_path {};
            VirtualFileSystem* m_system {nullptr};
        };
    }// namespace

    LocalMount::LocalMount(Path realRoot, MountPoint mountPoint)
        : m_realRoot(std::move(realRoot)), m_mountPoint(std::move(mountPoint))
    {
        m_realRoot.Normalize();
        if (m_mountPoint.virtualPrefix.IsEmpty())
            m_mountPoint.virtualPrefix = Path {"/"};
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

        const auto full   = normalized.View();
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

    Result<Path> LocalMount::Virtualize(const Path& realPath) noexcept
    {
        Path normalized = realPath.LexicallyNormal();
        if (!normalized.StartsWith(m_realRoot))
        {
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::InvalidPath, "real path is outside mount root", realPath)));
        }

        const auto full   = normalized.View();
        const auto prefix = m_realRoot.View();

        std::string_view suffix {};
        if (full.size() > prefix.size())
        {
            suffix = full.substr(prefix.size());
            while (!suffix.empty() && (suffix.front() == '/' || suffix.front() == '\\'))
                suffix.remove_prefix(1);
        }

        Path out = m_mountPoint.virtualPrefix;
        if (!suffix.empty())
            out.Append(suffix);
        out.Normalize();
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
                return Result<ResolvedMount>(NGIN::Utilities::Unexpected<IOError>(std::move(translated.Error())));
            }
            ResolvedMount out;
            out.mount          = mount.get();
            out.translatedPath = std::move(translated.Value());
            return Result<ResolvedMount>(std::move(out));
        }
        return Result<ResolvedMount>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "no mount for virtual path", virtualPath)));
    }

    Result<bool> VirtualFileSystem::Exists(const Path& path) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<bool>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
        return resolved.Value().mount->GetFileSystem().Exists(resolved.Value().translatedPath);
    }

    Result<FileInfo> VirtualFileSystem::GetInfo(const Path& path, const MetadataOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<FileInfo>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
        auto info = resolved.Value().mount->GetFileSystem().GetInfo(resolved.Value().translatedPath, options);
        if (info.HasValue())
            info.Value().path = path;
        return info;
    }

    Result<Path> VirtualFileSystem::Absolute(const Path& path, const Path& base) noexcept
    {
        if (path.IsAbsolute())
            return Result<Path>(path.LexicallyNormal());
        if (base.IsEmpty())
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::Unsupported, "virtual cwd is not defined", path)));

        Path absolute = base.Join(path.View());
        absolute.Normalize();
        return Result<Path>(std::move(absolute));
    }

    Result<Path> VirtualFileSystem::Canonical(const Path& path) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));

        auto canonical = resolved.Value().mount->GetFileSystem().Canonical(resolved.Value().translatedPath);
        if (!canonical.HasValue())
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(canonical.Error())));

        return resolved.Value().mount->Virtualize(canonical.Value());
    }

    Result<Path> VirtualFileSystem::WeaklyCanonical(const Path& path) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));

        auto canonical = resolved.Value().mount->GetFileSystem().WeaklyCanonical(resolved.Value().translatedPath);
        if (!canonical.HasValue())
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(canonical.Error())));

        return resolved.Value().mount->Virtualize(canonical.Value());
    }

    Result<bool> VirtualFileSystem::SameFile(const Path& lhs, const Path& rhs) noexcept
    {
        auto lhsResolved = ResolvePath(lhs);
        if (!lhsResolved.HasValue())
            return Result<bool>(NGIN::Utilities::Unexpected<IOError>(std::move(lhsResolved.Error())));

        auto rhsResolved = ResolvePath(rhs);
        if (!rhsResolved.HasValue())
            return Result<bool>(NGIN::Utilities::Unexpected<IOError>(std::move(rhsResolved.Error())));

        if (lhsResolved.Value().mount != rhsResolved.Value().mount)
            return Result<bool>(false);

        return lhsResolved.Value().mount->GetFileSystem().SameFile(
                lhsResolved.Value().translatedPath, rhsResolved.Value().translatedPath);
    }

    Result<Path> VirtualFileSystem::ReadSymlink(const Path& path) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));

        auto target = resolved.Value().mount->GetFileSystem().ReadSymlink(resolved.Value().translatedPath);
        if (!target.HasValue())
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(target.Error())));

        if (target.Value().IsAbsolute())
        {
            auto virtualized = resolved.Value().mount->Virtualize(target.Value());
            if (virtualized.HasValue())
                return virtualized;
        }
        return target;
    }

    ResultVoid VirtualFileSystem::CreateDirectory(const Path& path, const DirectoryCreateOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
        if (resolved.Value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.Value().mount->GetFileSystem().CreateDirectory(resolved.Value().translatedPath, options);
    }

    ResultVoid VirtualFileSystem::CreateDirectories(const Path& path, const DirectoryCreateOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
        if (resolved.Value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.Value().mount->GetFileSystem().CreateDirectories(resolved.Value().translatedPath, options);
    }

    ResultVoid VirtualFileSystem::CreateSymlink(const Path& target, const Path& linkPath) noexcept
    {
        auto resolved = ResolvePath(linkPath);
        if (!resolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
        if (resolved.Value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", linkPath)));

        Path translatedTarget = target;
        if (target.IsAbsolute())
        {
            auto targetResolved = ResolvePath(target);
            if (!targetResolved.HasValue())
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(targetResolved.Error())));
            if (targetResolved.Value().mount != resolved.Value().mount)
            {
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                        MakeError(IOErrorCode::CrossDevice, "cross-mount symlink target is not supported", target, linkPath)));
            }
            translatedTarget = targetResolved.Value().translatedPath;
        }

        return resolved.Value().mount->GetFileSystem().CreateSymlink(translatedTarget, resolved.Value().translatedPath);
    }

    ResultVoid VirtualFileSystem::CreateHardLink(const Path& target, const Path& linkPath) noexcept
    {
        auto targetResolved = ResolvePath(target);
        if (!targetResolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(targetResolved.Error())));
        auto linkResolved = ResolvePath(linkPath);
        if (!linkResolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(linkResolved.Error())));
        if (linkResolved.Value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", linkPath)));
        if (targetResolved.Value().mount != linkResolved.Value().mount)
        {
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::CrossDevice, "cross-mount hard link is not supported", target, linkPath)));
        }
        return targetResolved.Value().mount->GetFileSystem().CreateHardLink(
                targetResolved.Value().translatedPath, linkResolved.Value().translatedPath);
    }

    ResultVoid VirtualFileSystem::SetPermissions(const Path& path, const FilePermissions& permissions, const SymlinkMode symlinkMode) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
        if (resolved.Value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.Value().mount->GetFileSystem().SetPermissions(resolved.Value().translatedPath, permissions, symlinkMode);
    }

    ResultVoid VirtualFileSystem::RemoveFile(const Path& path, const RemoveOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
        if (resolved.Value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.Value().mount->GetFileSystem().RemoveFile(resolved.Value().translatedPath, options);
    }

    ResultVoid VirtualFileSystem::RemoveDirectory(const Path& path, const RemoveOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
        if (resolved.Value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.Value().mount->GetFileSystem().RemoveDirectory(resolved.Value().translatedPath, options);
    }

    Result<UInt64> VirtualFileSystem::RemoveAll(const Path& path, const RemoveOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
        if (resolved.Value().mount->GetMountPoint().readOnly)
            return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.Value().mount->GetFileSystem().RemoveAll(resolved.Value().translatedPath, options);
    }

    ResultVoid VirtualFileSystem::Rename(const Path& from, const Path& to) noexcept
    {
        auto fromResolved = ResolvePath(from);
        if (!fromResolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(fromResolved.Error())));
        auto toResolved = ResolvePath(to);
        if (!toResolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(toResolved.Error())));
        if (fromResolved.Value().mount != toResolved.Value().mount)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::CrossDevice, "cross-mount rename not supported", from, to)));
        if (fromResolved.Value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", from, to)));
        return fromResolved.Value().mount->GetFileSystem().Rename(fromResolved.Value().translatedPath, toResolved.Value().translatedPath);
    }

    ResultVoid VirtualFileSystem::ReplaceFile(const Path& source, const Path& destination) noexcept
    {
        auto sourceResolved = ResolvePath(source);
        if (!sourceResolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(sourceResolved.Error())));
        auto destinationResolved = ResolvePath(destination);
        if (!destinationResolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(destinationResolved.Error())));
        if (sourceResolved.Value().mount != destinationResolved.Value().mount)
        {
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::CrossDevice, "cross-mount replace is not supported", source, destination)));
        }
        if (destinationResolved.Value().mount->GetMountPoint().readOnly)
        {
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::PermissionDenied, "destination mount is read-only", source, destination)));
        }
        return sourceResolved.Value().mount->GetFileSystem().ReplaceFile(
                sourceResolved.Value().translatedPath, destinationResolved.Value().translatedPath);
    }

    ResultVoid VirtualFileSystem::CopyFile(const Path& from, const Path& to, const CopyOptions& options) noexcept
    {
        auto fromResolved = ResolvePath(from);
        if (!fromResolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(fromResolved.Error())));
        auto toResolved = ResolvePath(to);
        if (!toResolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(toResolved.Error())));
        if (toResolved.Value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "destination mount is read-only", from, to)));
        if (fromResolved.Value().mount == toResolved.Value().mount)
        {
            return fromResolved.Value().mount->GetFileSystem().CopyFile(fromResolved.Value().translatedPath, toResolved.Value().translatedPath, options);
        }
        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::CrossDevice, "cross-mount copy not implemented in v1", from, to)));
    }

    ResultVoid VirtualFileSystem::Move(const Path& from, const Path& to, const CopyOptions& options) noexcept
    {
        auto fromResolved = ResolvePath(from);
        if (!fromResolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(fromResolved.Error())));
        auto toResolved = ResolvePath(to);
        if (!toResolved.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(toResolved.Error())));
        if (toResolved.Value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "destination mount is read-only", from, to)));
        if (fromResolved.Value().mount != toResolved.Value().mount)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::CrossDevice, "cross-mount move not implemented in v1", from, to)));
        return fromResolved.Value().mount->GetFileSystem().Move(fromResolved.Value().translatedPath, toResolved.Value().translatedPath, options);
    }

    Result<FileHandle> VirtualFileSystem::OpenFile(const Path& path, const FileOpenOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<FileHandle>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
        if ((options.access == FileAccess::Write || options.access == FileAccess::ReadWrite || options.access == FileAccess::Append) &&
            resolved.Value().mount->GetMountPoint().readOnly)
        {
            return Result<FileHandle>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        }
        return resolved.Value().mount->GetFileSystem().OpenFile(resolved.Value().translatedPath, options);
    }

    Result<DirectoryHandle> VirtualFileSystem::OpenDirectory(const Path& path) noexcept
    {
        auto opened = VirtualDirectoryHandle::Open(*this, path);
        if (!opened.HasValue())
            return Result<DirectoryHandle>(NGIN::Utilities::Unexpected<IOError>(std::move(opened.Error())));
        return Result<DirectoryHandle>(DirectoryHandle(std::move(opened).TakeValue()));
    }

    Result<FileView> VirtualFileSystem::OpenFileView(const Path& path) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<FileView>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
        return resolved.Value().mount->GetFileSystem().OpenFileView(resolved.Value().translatedPath);
    }

    Result<DirectoryEnumerator> VirtualFileSystem::Enumerate(const Path& path, const EnumerateOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<DirectoryEnumerator>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
        return resolved.Value().mount->GetFileSystem().Enumerate(resolved.Value().translatedPath, options);
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

    Result<Path> VirtualFileSystem::CreateTempDirectory(const Path& directory, std::string_view prefix) noexcept
    {
        if (!directory.IsEmpty())
        {
            auto resolved = ResolvePath(directory);
            if (!resolved.HasValue())
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
            if (resolved.Value().mount->GetMountPoint().readOnly)
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", directory)));
            auto created = resolved.Value().mount->GetFileSystem().CreateTempDirectory(resolved.Value().translatedPath, prefix);
            if (!created.HasValue())
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(created.Error())));
            return resolved.Value().mount->Virtualize(created.Value());
        }

        for (auto& mount: m_mounts)
        {
            if (!mount || mount->GetMountPoint().readOnly)
                continue;
            auto translatedRoot = mount->Translate(mount->GetMountPoint().virtualPrefix);
            if (!translatedRoot.HasValue())
                continue;
            auto created = mount->GetFileSystem().CreateTempDirectory(translatedRoot.Value(), prefix);
            if (!created.HasValue())
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(created.Error())));
            return mount->Virtualize(created.Value());
        }

        return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "no writable mounts configured")));
    }

    Result<Path> VirtualFileSystem::CreateTempFile(const Path& directory, std::string_view prefix) noexcept
    {
        if (!directory.IsEmpty())
        {
            auto resolved = ResolvePath(directory);
            if (!resolved.HasValue())
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
            if (resolved.Value().mount->GetMountPoint().readOnly)
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", directory)));
            auto created = resolved.Value().mount->GetFileSystem().CreateTempFile(resolved.Value().translatedPath, prefix);
            if (!created.HasValue())
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(created.Error())));
            return resolved.Value().mount->Virtualize(created.Value());
        }

        for (auto& mount: m_mounts)
        {
            if (!mount || mount->GetMountPoint().readOnly)
                continue;
            auto translatedRoot = mount->Translate(mount->GetMountPoint().virtualPrefix);
            if (!translatedRoot.HasValue())
                continue;
            auto created = mount->GetFileSystem().CreateTempFile(translatedRoot.Value(), prefix);
            if (!created.HasValue())
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(created.Error())));
            return mount->Virtualize(created.Value());
        }

        return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "no writable mounts configured")));
    }

    Result<SpaceInfo> VirtualFileSystem::GetSpaceInfo(const Path& path) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
            return Result<SpaceInfo>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
        return resolved.Value().mount->GetFileSystem().GetSpaceInfo(resolved.Value().translatedPath);
    }

    AsyncTask<std::unique_ptr<IAsyncFileHandle>> VirtualFileSystem::OpenFileAsync(
            NGIN::Async::TaskContext& ctx, const Path& path, const FileOpenOptions& options)
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
        {
            co_return AsyncResult<std::unique_ptr<IAsyncFileHandle>>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
        }
        auto* asyncFs = resolved.Value().mount->GetAsyncFileSystem();
        if (!asyncFs)
        {
            co_return AsyncResult<std::unique_ptr<IAsyncFileHandle>>(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::Unsupported, "mount has no async filesystem", path)));
        }
        co_return co_await asyncFs->OpenFileAsync(ctx, resolved.Value().translatedPath, options);
    }

    AsyncTask<FileInfo> VirtualFileSystem::GetInfoAsync(
            NGIN::Async::TaskContext& ctx, const Path& path, const MetadataOptions& options)
    {
        auto resolved = ResolvePath(path);
        if (!resolved.HasValue())
        {
            co_return AsyncResult<FileInfo>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.Error())));
        }
        auto* asyncFs = resolved.Value().mount->GetAsyncFileSystem();
        if (!asyncFs)
        {
            co_return AsyncResult<FileInfo>(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::Unsupported, "mount has no async filesystem", path)));
        }
        auto awaited = co_await asyncFs->GetInfoAsync(ctx, resolved.Value().translatedPath, options);
        if (!awaited)
        {
            co_await AsyncTask<FileInfo>::ReturnError(awaited.Error());
            co_return AsyncResult<FileInfo>(FileInfo {});
        }
        auto info = std::move(*awaited);
        if (info)
            info.Value().path = path;
        co_return std::move(info);
    }

    AsyncTaskVoid VirtualFileSystem::CopyFileAsync(
            NGIN::Async::TaskContext& ctx, const Path& from, const Path& to, const CopyOptions& options)
    {
        auto fromResolved = ResolvePath(from);
        if (!fromResolved.HasValue())
            co_return AsyncResult<void>(NGIN::Utilities::Unexpected<IOError>(std::move(fromResolved.Error())));
        auto toResolved = ResolvePath(to);
        if (!toResolved.HasValue())
            co_return AsyncResult<void>(NGIN::Utilities::Unexpected<IOError>(std::move(toResolved.Error())));
        if (fromResolved.Value().mount != toResolved.Value().mount)
            co_return AsyncResult<void>(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::CrossDevice, "cross-mount async copy not implemented", from, to)));
        auto* asyncFs = fromResolved.Value().mount->GetAsyncFileSystem();
        if (!asyncFs)
            co_return AsyncResult<void>(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::Unsupported, "mount has no async filesystem", from, to)));
        co_return co_await asyncFs->CopyFileAsync(ctx, fromResolved.Value().translatedPath, toResolved.Value().translatedPath, options);
    }
}// namespace NGIN::IO
