#include <NGIN/IO/VirtualFileSystem.hpp>

#include <NGIN/Utilities/Expected.hpp>

#include <algorithm>
#include <array>
#include <new>
#include <vector>

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
            out.symlinks              = lhs.symlinks && rhs.symlinks;
            out.hardLinks             = lhs.hardLinks && rhs.hardLinks;
            out.blockDevices          = lhs.blockDevices && rhs.blockDevices;
            out.characterDevices      = lhs.characterDevices && rhs.characterDevices;
            out.fifos                 = lhs.fifos && rhs.fifos;
            out.sockets               = lhs.sockets && rhs.sockets;
            out.posixModeBits         = lhs.posixModeBits && rhs.posixModeBits;
            out.ownership             = lhs.ownership && rhs.ownership;
            out.setIdBits             = lhs.setIdBits && rhs.setIdBits;
            out.stickyBit             = lhs.stickyBit && rhs.stickyBit;
            out.fileIdentity          = lhs.fileIdentity && rhs.fileIdentity;
            out.hardLinkCount         = lhs.hardLinkCount && rhs.hardLinkCount;
            out.memoryMappedFiles     = lhs.memoryMappedFiles && rhs.memoryMappedFiles;
            out.nanosecondTimestamps  = lhs.nanosecondTimestamps && rhs.nanosecondTimestamps;
            out.metadataNoFollow      = lhs.metadataNoFollow && rhs.metadataNoFollow;
            out.atomicRenameNoReplace = lhs.atomicRenameNoReplace && rhs.atomicRenameNoReplace;
            out.atomicReplace         = lhs.atomicReplace && rhs.atomicReplace;
            out.durableReplace        = lhs.durableReplace && rhs.durableReplace;
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

        ResultVoid RemoveCopiedPath(IFileSystem& fileSystem, const Path& path) noexcept
        {
            auto info = fileSystem.GetInfo(path, MetadataOptions {.symlinkMode = SymlinkMode::DoNotFollow});
            if (!info.has_value())
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(info.error())));
            if (!info.value().exists)
                return {};
            if (info.value().type == EntryType::Directory)
                return fileSystem.RemoveDirectory(path, RemoveOptions {.recursive = true, .ignoreMissing = true});
            return fileSystem.RemoveFile(path, RemoveOptions {.ignoreMissing = true});
        }

        struct CopyCleanup
        {
            IFileSystem* system {nullptr};
            Path         path {};
            bool         active {false};

            ~CopyCleanup()
            {
                if (active && system != nullptr)
                    (void) RemoveCopiedPath(*system, path);
            }

            void Release() noexcept { active = false; }
        };

        ResultVoid CopyAcrossFileSystems(
                IFileSystem&               sourceSystem,
                const Path&                sourcePath,
                IFileSystem&               destinationSystem,
                const Path&                destinationPath,
                const CopyOptions&         options,
                std::vector<FileIdentity>& activeDirectories)
        {
            auto sourceInfo = sourceSystem.GetInfo(sourcePath, MetadataOptions {.symlinkMode = SymlinkMode::DoNotFollow});
            if (!sourceInfo.has_value())
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(sourceInfo.error())));
            if (!sourceInfo.value().exists)
            {
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                        MakeError(IOErrorCode::NotFound, "cross-mount copy source not found", sourcePath, destinationPath)));
            }

            const bool isSymlink = sourceInfo.value().type == EntryType::Symlink;
            if (isSymlink && options.symlinks == CopySymlinkMode::Reject)
            {
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                        MakeError(IOErrorCode::NotSupported, "cross-mount copy rejected a symbolic link", sourcePath, destinationPath)));
            }

            auto destinationInfo = destinationSystem.GetInfo(
                    destinationPath, MetadataOptions {.symlinkMode = SymlinkMode::DoNotFollow});
            if (!destinationInfo.has_value())
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(destinationInfo.error())));

            if (isSymlink && options.symlinks == CopySymlinkMode::Preserve)
            {
                if (destinationInfo.value().exists)
                {
                    if (!options.overwriteExisting)
                    {
                        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                                MakeError(IOErrorCode::AlreadyExists, "destination exists", destinationPath, sourcePath)));
                    }
                    auto removed = RemoveCopiedPath(destinationSystem, destinationPath);
                    if (!removed.has_value())
                        return removed;
                }

                auto target = sourceSystem.ReadSymlink(sourcePath);
                if (!target.has_value())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(target.error())));
                return destinationSystem.CreateSymlink(target.value(), destinationPath);
            }

            if (isSymlink)
            {
                sourceInfo = sourceSystem.GetInfo(sourcePath, MetadataOptions {.symlinkMode = SymlinkMode::Follow});
                if (!sourceInfo.has_value())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(sourceInfo.error())));
                if (!sourceInfo.value().exists || sourceInfo.value().type == EntryType::Symlink)
                {
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                            MakeError(IOErrorCode::NotFound, "symbolic-link target does not exist", sourcePath, destinationPath)));
                }
            }

            if (sourceInfo.value().type == EntryType::Directory)
            {
                if (!options.recursive)
                {
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                            MakeError(IOErrorCode::NotSupported, "directory copy requires recursive option", sourcePath, destinationPath)));
                }

                if (sourceInfo.value().identity.valid)
                {
                    const auto duplicate = std::find_if(activeDirectories.begin(), activeDirectories.end(), [&](const FileIdentity& identity) {
                        return identity.device == sourceInfo.value().identity.device && identity.inode == sourceInfo.value().identity.inode;
                    });
                    if (duplicate != activeDirectories.end())
                    {
                        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(
                                IOErrorCode::InvalidPath,
                                "symbolic-link cycle detected during cross-mount copy",
                                sourcePath,
                                destinationPath)));
                    }
                    activeDirectories.push_back(sourceInfo.value().identity);
                }

                bool createdDestination = false;
                if (destinationInfo.value().exists && destinationInfo.value().type != EntryType::Directory)
                {
                    if (!options.overwriteExisting)
                    {
                        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                                MakeError(IOErrorCode::AlreadyExists, "destination exists", destinationPath, sourcePath)));
                    }
                    auto removed = RemoveCopiedPath(destinationSystem, destinationPath);
                    if (!removed.has_value())
                        return removed;
                    destinationInfo.value().exists = false;
                }
                if (!destinationInfo.value().exists)
                {
                    auto created = destinationSystem.CreateDirectory(
                            destinationPath, DirectoryCreateOptions {.recursive = false, .ignoreIfExists = false});
                    if (!created.has_value())
                        return created;
                    createdDestination = true;
                }

                auto enumerator = sourceSystem.Enumerate(
                        sourcePath,
                        EnumerateOptions {
                                .recursive          = false,
                                .includeFiles       = true,
                                .includeDirectories = true,
                                .includeSymlinks    = true,
                        });
                ResultVoid copied {};
                if (!enumerator.has_value())
                {
                    copied = ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(enumerator.error())));
                }
                else
                {
                    for (;;)
                    {
                        auto next = enumerator.value().Next();
                        if (!next.has_value())
                        {
                            copied = ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(next.error())));
                            break;
                        }
                        if (!next.value().HasEntry())
                            break;
                        const auto& entry = next.value().Entry();
                        copied            = CopyAcrossFileSystems(
                                sourceSystem,
                                entry.path,
                                destinationSystem,
                                destinationPath.Join(entry.name.View()),
                                options,
                                activeDirectories);
                        if (!copied.has_value())
                            break;
                    }
                }

                if (sourceInfo.value().identity.valid)
                    activeDirectories.pop_back();
                if (!copied.has_value())
                {
                    const IOError error = std::move(copied.error());
                    if (createdDestination && options.cleanupOnFailure)
                        (void) RemoveCopiedPath(destinationSystem, destinationPath);
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(error)));
                }

                if (options.preservePermissions)
                {
                    auto permissions = destinationSystem.SetPermissions(
                            destinationPath, sourceInfo.value().permissions, SymlinkMode::Follow);
                    if (!permissions.has_value())
                    {
                        const IOError error = std::move(permissions.error());
                        if (createdDestination && options.cleanupOnFailure)
                            (void) RemoveCopiedPath(destinationSystem, destinationPath);
                        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(error)));
                    }
                }
                return {};
            }

            if (sourceInfo.value().type != EntryType::File)
            {
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                        MakeError(IOErrorCode::NotSupported, "cross-mount copy is unsupported for this entry type", sourcePath, destinationPath)));
            }

            FileOpenOptions sourceOpen;
            sourceOpen.access      = FileAccess::Read;
            sourceOpen.share       = FileShare::Read;
            sourceOpen.disposition = FileCreateDisposition::OpenExisting;
            auto source            = sourceSystem.OpenFile(sourcePath, sourceOpen);
            if (!source.has_value())
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(source.error())));

            FileOpenOptions destinationOpen;
            destinationOpen.access        = FileAccess::Write;
            destinationOpen.share         = FileShare::Read;
            destinationOpen.disposition   = options.overwriteExisting ? FileCreateDisposition::CreateAlways : FileCreateDisposition::CreateNew;
            const bool destinationExisted = destinationInfo.value().exists;
            auto       destination        = destinationSystem.OpenFile(destinationPath, destinationOpen);
            if (!destination.has_value())
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(destination.error())));

            std::array<Byte, 64 * 1024> buffer {};
            ResultVoid                  copied {};
            for (;;)
            {
                auto read = source.value().Read(buffer);
                if (!read.has_value())
                {
                    copied = ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(read.error())));
                    break;
                }
                if (read.value() == 0)
                    break;

                UIntSize written = 0;
                while (written < read.value())
                {
                    auto write = destination.value().Write(
                            std::span<const Byte>(buffer.data() + written, read.value() - written));
                    if (!write.has_value())
                    {
                        copied = ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(write.error())));
                        break;
                    }
                    if (write.value() == 0)
                    {
                        copied = ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(
                                IOErrorCode::EndOfStream, "zero-byte write during cross-mount copy", sourcePath, destinationPath)));
                        break;
                    }
                    written += write.value();
                }
                if (!copied.has_value())
                    break;
            }
            source.value().Close();
            destination.value().Close();

            if (!copied.has_value())
            {
                const IOError error = std::move(copied.error());
                if (!destinationExisted && options.cleanupOnFailure)
                    (void) RemoveCopiedPath(destinationSystem, destinationPath);
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(error)));
            }
            if (options.preservePermissions)
                return destinationSystem.SetPermissions(destinationPath, sourceInfo.value().permissions, SymlinkMode::Follow);
            return {};
        }

        class VirtualDirectoryHandle final : public IDirectoryHandle
        {
        public:
            static Result<std::unique_ptr<VirtualDirectoryHandle>> Open(VirtualFileSystem& fileSystem, const Path& path) noexcept
            {
                auto info = fileSystem.GetInfo(path);
                if (!info.has_value())
                {
                    return Result<std::unique_ptr<VirtualDirectoryHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(std::move(info.error())));
                }
                if (!info.value().exists)
                {
                    return Result<std::unique_ptr<VirtualDirectoryHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "directory not found", path)));
                }
                if (info.value().type != EntryType::Directory)
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
                if (!normalized.has_value())
                    return Result<bool>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.error())));
                return m_system->Exists(JoinHandlePath(m_path, normalized.value()));
            }

            Result<FileInfo> GetInfo(const Path& path, const MetadataOptions& options) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.has_value())
                    return Result<FileInfo>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.error())));
                return m_system->GetInfo(JoinHandlePath(m_path, normalized.value()), options);
            }

            Result<FileHandle> OpenFile(const Path& path, const FileOpenOptions& options) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.has_value())
                {
                    return Result<FileHandle>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.error())));
                }
                return m_system->OpenFile(JoinHandlePath(m_path, normalized.value()), options);
            }

            Result<DirectoryHandle> OpenDirectory(const Path& path) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.has_value())
                {
                    return Result<DirectoryHandle>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.error())));
                }

                auto opened = Open(*m_system, JoinHandlePath(m_path, normalized.value()));
                if (!opened.has_value())
                {
                    return Result<DirectoryHandle>(NGIN::Utilities::Unexpected<IOError>(std::move(opened.error())));
                }

                return Result<DirectoryHandle>(DirectoryHandle(std::move(opened).value()));
            }

            ResultVoid CreateDirectory(const Path& path, const DirectoryCreateOptions& options = {}) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.has_value())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.error())));

                const Path resolvedPath = JoinHandlePath(m_path, normalized.value());
                if (options.recursive)
                    return m_system->CreateDirectories(resolvedPath, options);
                return m_system->CreateDirectory(resolvedPath, options);
            }

            ResultVoid RemoveFile(const Path& path, const RemoveOptions& options = {}) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.has_value())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.error())));
                return m_system->RemoveFile(JoinHandlePath(m_path, normalized.value()), options);
            }

            ResultVoid RemoveDirectory(const Path& path, const RemoveOptions& options = {}) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.has_value())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.error())));
                return m_system->RemoveDirectory(JoinHandlePath(m_path, normalized.value()), options);
            }

            Result<Path> ReadSymlink(const Path& path) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.has_value())
                    return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.error())));
                return m_system->ReadSymlink(JoinHandlePath(m_path, normalized.value()));
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

    LocalMount::LocalMount(Runtime& runtime, Path realRoot, MountPoint mountPoint)
        : m_realRoot(std::move(realRoot)), m_mountPoint(std::move(mountPoint)), m_localFileSystem(runtime)
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
        Path root       = m_realRoot;
        if (!normalized.StartsWith(root))
        {
            auto canonicalPath = m_localFileSystem.WeaklyCanonical(normalized);
            if (!canonicalPath.has_value())
            {
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(canonicalPath.error())));
            }

            auto canonicalRoot = m_localFileSystem.WeaklyCanonical(root);
            if (!canonicalRoot.has_value())
            {
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(canonicalRoot.error())));
            }

            normalized = std::move(canonicalPath.value());
            root       = std::move(canonicalRoot.value());
            if (!normalized.StartsWith(root))
            {
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(
                        MakeError(IOErrorCode::InvalidPath, "real path is outside mount root", realPath)));
            }
        }

        const auto full   = normalized.View();
        const auto prefix = root.View();

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
            if (!translated.has_value())
            {
                return Result<ResolvedMount>(NGIN::Utilities::Unexpected<IOError>(std::move(translated.error())));
            }
            ResolvedMount out;
            out.mount          = mount.get();
            out.translatedPath = std::move(translated.value());
            return Result<ResolvedMount>(std::move(out));
        }
        return Result<ResolvedMount>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "no mount for virtual path", virtualPath)));
    }

    Result<bool> VirtualFileSystem::Exists(const Path& path) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
            return Result<bool>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));
        return resolved.value().mount->GetFileSystem().Exists(resolved.value().translatedPath);
    }

    Result<FileInfo> VirtualFileSystem::GetInfo(const Path& path, const MetadataOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
            return Result<FileInfo>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));
        auto info = resolved.value().mount->GetFileSystem().GetInfo(resolved.value().translatedPath, options);
        if (info.has_value())
            info.value().path = path;
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
        if (!resolved.has_value())
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));

        auto canonical = resolved.value().mount->GetFileSystem().Canonical(resolved.value().translatedPath);
        if (!canonical.has_value())
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(canonical.error())));

        return resolved.value().mount->Virtualize(canonical.value());
    }

    Result<Path> VirtualFileSystem::WeaklyCanonical(const Path& path) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));

        auto canonical = resolved.value().mount->GetFileSystem().WeaklyCanonical(resolved.value().translatedPath);
        if (!canonical.has_value())
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(canonical.error())));

        return resolved.value().mount->Virtualize(canonical.value());
    }

    Result<bool> VirtualFileSystem::SameFile(const Path& lhs, const Path& rhs) noexcept
    {
        auto lhsResolved = ResolvePath(lhs);
        if (!lhsResolved.has_value())
            return Result<bool>(NGIN::Utilities::Unexpected<IOError>(std::move(lhsResolved.error())));

        auto rhsResolved = ResolvePath(rhs);
        if (!rhsResolved.has_value())
            return Result<bool>(NGIN::Utilities::Unexpected<IOError>(std::move(rhsResolved.error())));

        if (lhsResolved.value().mount != rhsResolved.value().mount)
            return Result<bool>(false);

        return lhsResolved.value().mount->GetFileSystem().SameFile(
                lhsResolved.value().translatedPath, rhsResolved.value().translatedPath);
    }

    Result<Path> VirtualFileSystem::ReadSymlink(const Path& path) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));

        auto target = resolved.value().mount->GetFileSystem().ReadSymlink(resolved.value().translatedPath);
        if (!target.has_value())
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(target.error())));

        if (target.value().IsAbsolute())
        {
            auto virtualized = resolved.value().mount->Virtualize(target.value());
            if (virtualized.has_value())
                return virtualized;
        }
        return target;
    }

    ResultVoid VirtualFileSystem::CreateDirectory(const Path& path, const DirectoryCreateOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));
        if (resolved.value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.value().mount->GetFileSystem().CreateDirectory(resolved.value().translatedPath, options);
    }

    ResultVoid VirtualFileSystem::CreateDirectories(const Path& path, const DirectoryCreateOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));
        if (resolved.value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.value().mount->GetFileSystem().CreateDirectories(resolved.value().translatedPath, options);
    }

    ResultVoid VirtualFileSystem::CreateSymlink(const Path& target, const Path& linkPath) noexcept
    {
        auto resolved = ResolvePath(linkPath);
        if (!resolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));
        if (resolved.value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", linkPath)));

        Path translatedTarget = target;
        if (target.IsAbsolute())
        {
            auto targetResolved = ResolvePath(target);
            if (!targetResolved.has_value())
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(targetResolved.error())));
            if (targetResolved.value().mount != resolved.value().mount)
            {
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                        MakeError(IOErrorCode::CrossDevice, "cross-mount symlink target is not supported", target, linkPath)));
            }
            translatedTarget = targetResolved.value().translatedPath;
        }

        return resolved.value().mount->GetFileSystem().CreateSymlink(translatedTarget, resolved.value().translatedPath);
    }

    ResultVoid VirtualFileSystem::CreateHardLink(const Path& target, const Path& linkPath) noexcept
    {
        auto targetResolved = ResolvePath(target);
        if (!targetResolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(targetResolved.error())));
        auto linkResolved = ResolvePath(linkPath);
        if (!linkResolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(linkResolved.error())));
        if (linkResolved.value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", linkPath)));
        if (targetResolved.value().mount != linkResolved.value().mount)
        {
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::CrossDevice, "cross-mount hard link is not supported", target, linkPath)));
        }
        return targetResolved.value().mount->GetFileSystem().CreateHardLink(
                targetResolved.value().translatedPath, linkResolved.value().translatedPath);
    }

    ResultVoid VirtualFileSystem::SetPermissions(const Path& path, const FilePermissions& permissions, const SymlinkMode symlinkMode) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));
        if (resolved.value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.value().mount->GetFileSystem().SetPermissions(resolved.value().translatedPath, permissions, symlinkMode);
    }

    ResultVoid VirtualFileSystem::RemoveFile(const Path& path, const RemoveOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));
        if (resolved.value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.value().mount->GetFileSystem().RemoveFile(resolved.value().translatedPath, options);
    }

    ResultVoid VirtualFileSystem::RemoveDirectory(const Path& path, const RemoveOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));
        if (resolved.value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.value().mount->GetFileSystem().RemoveDirectory(resolved.value().translatedPath, options);
    }

    Result<UInt64> VirtualFileSystem::RemoveAll(const Path& path, const RemoveOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
            return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));
        if (resolved.value().mount->GetMountPoint().readOnly)
            return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        return resolved.value().mount->GetFileSystem().RemoveAll(resolved.value().translatedPath, options);
    }

    ResultVoid VirtualFileSystem::Rename(const Path& from, const Path& to) noexcept
    {
        auto fromResolved = ResolvePath(from);
        if (!fromResolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(fromResolved.error())));
        auto toResolved = ResolvePath(to);
        if (!toResolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(toResolved.error())));
        if (fromResolved.value().mount != toResolved.value().mount)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::CrossDevice, "cross-mount rename not supported", from, to)));
        if (fromResolved.value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", from, to)));
        return fromResolved.value().mount->GetFileSystem().Rename(fromResolved.value().translatedPath, toResolved.value().translatedPath);
    }

    ResultVoid VirtualFileSystem::RenameNoReplace(const Path& from, const Path& to) noexcept
    {
        auto fromResolved = ResolvePath(from);
        if (!fromResolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(fromResolved.error())));
        auto toResolved = ResolvePath(to);
        if (!toResolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(toResolved.error())));
        if (fromResolved.value().mount != toResolved.value().mount)
        {
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::CrossDevice, "cross-mount no-replace rename is not supported", from, to)));
        }
        if (toResolved.value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", from, to)));
        return fromResolved.value().mount->GetFileSystem().RenameNoReplace(
                fromResolved.value().translatedPath, toResolved.value().translatedPath);
    }

    ResultVoid VirtualFileSystem::ReplaceFile(
            const Path& source, const Path& destination, const ReplaceOptions& options) noexcept
    {
        auto sourceResolved = ResolvePath(source);
        if (!sourceResolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(sourceResolved.error())));
        auto destinationResolved = ResolvePath(destination);
        if (!destinationResolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(destinationResolved.error())));
        if (sourceResolved.value().mount != destinationResolved.value().mount)
        {
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::CrossDevice, "cross-mount replace is not supported", source, destination)));
        }
        if (destinationResolved.value().mount->GetMountPoint().readOnly)
        {
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::PermissionDenied, "destination mount is read-only", source, destination)));
        }
        return sourceResolved.value().mount->GetFileSystem().ReplaceFile(
                sourceResolved.value().translatedPath, destinationResolved.value().translatedPath, options);
    }

    ResultVoid VirtualFileSystem::CopyFile(const Path& from, const Path& to, const CopyOptions& options) noexcept
    {
        auto fromResolved = ResolvePath(from);
        if (!fromResolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(fromResolved.error())));
        auto toResolved = ResolvePath(to);
        if (!toResolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(toResolved.error())));
        if (toResolved.value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "destination mount is read-only", from, to)));
        if (fromResolved.value().mount == toResolved.value().mount)
        {
            return fromResolved.value().mount->GetFileSystem().CopyFile(fromResolved.value().translatedPath, toResolved.value().translatedPath, options);
        }
        try
        {
            std::vector<FileIdentity> activeDirectories;
            return CopyAcrossFileSystems(
                    fromResolved.value().mount->GetFileSystem(),
                    fromResolved.value().translatedPath,
                    toResolved.value().mount->GetFileSystem(),
                    toResolved.value().translatedPath,
                    options,
                    activeDirectories);
        } catch (const std::bad_alloc&)
        {
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                    MakeError(IOErrorCode::SystemError, "allocation failed during cross-mount copy", from, to)));
        }
    }

    ResultVoid VirtualFileSystem::Move(const Path& from, const Path& to, const CopyOptions& options) noexcept
    {
        auto fromResolved = ResolvePath(from);
        if (!fromResolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(fromResolved.error())));
        auto toResolved = ResolvePath(to);
        if (!toResolved.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(toResolved.error())));
        if (toResolved.value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "destination mount is read-only", from, to)));
        if (fromResolved.value().mount == toResolved.value().mount)
            return fromResolved.value().mount->GetFileSystem().Move(fromResolved.value().translatedPath, toResolved.value().translatedPath, options);
        if (fromResolved.value().mount->GetMountPoint().readOnly)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "source mount is read-only", from, to)));

        auto copied = CopyFile(from, to, options);
        if (!copied.has_value())
            return copied;

        auto sourceInfo = fromResolved.value().mount->GetFileSystem().GetInfo(
                fromResolved.value().translatedPath, MetadataOptions {.symlinkMode = SymlinkMode::DoNotFollow});
        if (!sourceInfo.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(sourceInfo.error())));
        if (sourceInfo.value().type == EntryType::Directory)
        {
            return fromResolved.value().mount->GetFileSystem().RemoveDirectory(
                    fromResolved.value().translatedPath, RemoveOptions {.recursive = true});
        }
        return fromResolved.value().mount->GetFileSystem().RemoveFile(fromResolved.value().translatedPath);
    }

    Result<FileHandle> VirtualFileSystem::OpenFile(const Path& path, const FileOpenOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
            return Result<FileHandle>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));
        if ((options.access == FileAccess::Write || options.access == FileAccess::ReadWrite || options.access == FileAccess::Append) &&
            resolved.value().mount->GetMountPoint().readOnly)
        {
            return Result<FileHandle>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", path)));
        }
        return resolved.value().mount->GetFileSystem().OpenFile(resolved.value().translatedPath, options);
    }

    Result<DirectoryHandle> VirtualFileSystem::OpenDirectory(const Path& path) noexcept
    {
        auto opened = VirtualDirectoryHandle::Open(*this, path);
        if (!opened.has_value())
            return Result<DirectoryHandle>(NGIN::Utilities::Unexpected<IOError>(std::move(opened.error())));
        return Result<DirectoryHandle>(DirectoryHandle(std::move(opened).value()));
    }

    Result<FileView> VirtualFileSystem::OpenFileView(const Path& path) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
            return Result<FileView>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));
        return resolved.value().mount->GetFileSystem().OpenFileView(resolved.value().translatedPath);
    }

    Result<DirectoryEnumerator> VirtualFileSystem::Enumerate(const Path& path, const EnumerateOptions& options) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
            return Result<DirectoryEnumerator>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));
        return resolved.value().mount->GetFileSystem().Enumerate(resolved.value().translatedPath, options);
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
            if (!resolved.has_value())
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));
            if (resolved.value().mount->GetMountPoint().readOnly)
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", directory)));
            auto created = resolved.value().mount->GetFileSystem().CreateTempDirectory(resolved.value().translatedPath, prefix);
            if (!created.has_value())
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(created.error())));
            return resolved.value().mount->Virtualize(created.value());
        }

        for (auto& mount: m_mounts)
        {
            if (!mount || mount->GetMountPoint().readOnly)
                continue;
            auto translatedRoot = mount->Translate(mount->GetMountPoint().virtualPrefix);
            if (!translatedRoot.has_value())
                continue;
            auto created = mount->GetFileSystem().CreateTempDirectory(translatedRoot.value(), prefix);
            if (!created.has_value())
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(created.error())));
            return mount->Virtualize(created.value());
        }

        return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "no writable mounts configured")));
    }

    Result<Path> VirtualFileSystem::CreateTempFile(const Path& directory, std::string_view prefix) noexcept
    {
        if (!directory.IsEmpty())
        {
            auto resolved = ResolvePath(directory);
            if (!resolved.has_value())
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));
            if (resolved.value().mount->GetMountPoint().readOnly)
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::PermissionDenied, "mount is read-only", directory)));
            auto created = resolved.value().mount->GetFileSystem().CreateTempFile(resolved.value().translatedPath, prefix);
            if (!created.has_value())
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(created.error())));
            return resolved.value().mount->Virtualize(created.value());
        }

        for (auto& mount: m_mounts)
        {
            if (!mount || mount->GetMountPoint().readOnly)
                continue;
            auto translatedRoot = mount->Translate(mount->GetMountPoint().virtualPrefix);
            if (!translatedRoot.has_value())
                continue;
            auto created = mount->GetFileSystem().CreateTempFile(translatedRoot.value(), prefix);
            if (!created.has_value())
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(created.error())));
            return mount->Virtualize(created.value());
        }

        return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "no writable mounts configured")));
    }

    Result<SpaceInfo> VirtualFileSystem::GetSpaceInfo(const Path& path) noexcept
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
            return Result<SpaceInfo>(NGIN::Utilities::Unexpected<IOError>(std::move(resolved.error())));
        return resolved.value().mount->GetFileSystem().GetSpaceInfo(resolved.value().translatedPath);
    }

    AsyncTask<AsyncFileHandle> VirtualFileSystem::OpenFileAsync(
            NGIN::Async::TaskContext& ctx, Path path, FileOpenOptions options)
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
        {
            co_return std::move(resolved).error();
        }
        auto* asyncFs = resolved.value().mount->GetAsyncFileSystem();
        if (!asyncFs)
        {
            co_return MakeError(IOErrorCode::Unsupported, "mount has no async filesystem", path);
        }
        co_return co_await asyncFs->OpenFileAsync(
                ctx, std::move(resolved.value().translatedPath), std::move(options));
    }

    AsyncTask<AsyncDirectoryHandle> VirtualFileSystem::OpenDirectoryAsync(
            NGIN::Async::TaskContext& ctx, Path path)
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
        {
            co_return std::move(resolved).error();
        }
        auto* asyncFs = resolved.value().mount->GetAsyncFileSystem();
        if (!asyncFs)
        {
            co_return MakeError(IOErrorCode::Unsupported, "mount has no async filesystem", path);
        }
        co_return co_await asyncFs->OpenDirectoryAsync(ctx, std::move(resolved.value().translatedPath));
    }

    AsyncTask<FileInfo> VirtualFileSystem::GetInfoAsync(
            NGIN::Async::TaskContext& ctx, Path path, MetadataOptions options)
    {
        auto resolved = ResolvePath(path);
        if (!resolved.has_value())
        {
            co_return std::move(resolved).error();
        }
        auto* asyncFs = resolved.value().mount->GetAsyncFileSystem();
        if (!asyncFs)
        {
            co_return MakeError(IOErrorCode::Unsupported, "mount has no async filesystem", path);
        }
        auto info = co_await asyncFs->GetInfoAsync(
                ctx, std::move(resolved.value().translatedPath), std::move(options));
        info.path = path;
        co_return info;
    }

    AsyncTaskVoid VirtualFileSystem::CopyFileAsync(
            NGIN::Async::TaskContext& ctx, Path from, Path to, CopyOptions options)
    {
        auto fromResolved = ResolvePath(from);
        if (!fromResolved.has_value())
        {
            co_await NGIN::Async::DomainFailure(std::move(fromResolved).error());
            co_return;
        }
        auto toResolved = ResolvePath(to);
        if (!toResolved.has_value())
        {
            co_await NGIN::Async::DomainFailure(std::move(toResolved).error());
            co_return;
        }
        if (toResolved.value().mount->GetMountPoint().readOnly)
        {
            co_await NGIN::Async::DomainFailure(MakeError(IOErrorCode::PermissionDenied, "destination mount is read-only", from, to));
            co_return;
        }
        if (fromResolved.value().mount == toResolved.value().mount)
        {
            auto* asyncFs = fromResolved.value().mount->GetAsyncFileSystem();
            if (!asyncFs)
            {
                co_await NGIN::Async::DomainFailure(MakeError(IOErrorCode::Unsupported, "mount has no async filesystem", from, to));
                co_return;
            }
            co_await asyncFs->CopyFileAsync(
                    ctx,
                    std::move(fromResolved.value().translatedPath),
                    std::move(toResolved.value().translatedPath),
                    std::move(options));
            co_return;
        }

        auto* sourceAsync      = fromResolved.value().mount->GetAsyncFileSystem();
        auto* destinationAsync = toResolved.value().mount->GetAsyncFileSystem();
        if (!sourceAsync || !destinationAsync)
        {
            co_await NGIN::Async::DomainFailure(MakeError(IOErrorCode::Unsupported, "cross-mount copy requires async filesystems", from, to));
            co_return;
        }

        auto& sourceSystem      = fromResolved.value().mount->GetFileSystem();
        auto& destinationSystem = toResolved.value().mount->GetFileSystem();
        auto  sourceInfo        = sourceSystem.GetInfo(
                fromResolved.value().translatedPath, MetadataOptions {.symlinkMode = SymlinkMode::DoNotFollow});
        if (!sourceInfo.has_value())
        {
            co_await NGIN::Async::DomainFailure(std::move(sourceInfo.error()));
            co_return;
        }
        if (!sourceInfo.value().exists)
        {
            co_await NGIN::Async::DomainFailure(MakeError(IOErrorCode::NotFound, "source not found", from, to));
            co_return;
        }
        if (ctx.IsCancellationRequested())
        {
            co_await NGIN::Async::Canceled();
            co_return;
        }

        if (sourceInfo.value().type == EntryType::Symlink)
        {
            auto copied = CopyFile(from, to, options);
            if (!copied.has_value())
            {
                co_await NGIN::Async::DomainFailure(std::move(copied.error()));
                co_return;
            }
            co_return;
        }

        auto destinationInfo = destinationSystem.GetInfo(
                toResolved.value().translatedPath, MetadataOptions {.symlinkMode = SymlinkMode::DoNotFollow});
        if (!destinationInfo.has_value())
        {
            co_await NGIN::Async::DomainFailure(std::move(destinationInfo.error()));
            co_return;
        }

        if (sourceInfo.value().type == EntryType::Directory)
        {
            if (!options.recursive)
            {
                co_await NGIN::Async::DomainFailure(
                        MakeError(IOErrorCode::NotSupported, "directory copy requires recursive option", from, to));
                co_return;
            }

            if (destinationInfo.value().exists && destinationInfo.value().type != EntryType::Directory)
            {
                if (!options.overwriteExisting)
                {
                    co_await NGIN::Async::DomainFailure(MakeError(IOErrorCode::AlreadyExists, "destination exists", from, to));
                    co_return;
                }
                auto removed = RemoveCopiedPath(destinationSystem, toResolved.value().translatedPath);
                if (!removed.has_value())
                {
                    co_await NGIN::Async::DomainFailure(std::move(removed.error()));
                    co_return;
                }
                destinationInfo.value().exists = false;
            }

            CopyCleanup cleanup {
                    .system = &destinationSystem,
                    .path   = toResolved.value().translatedPath,
                    .active = !destinationInfo.value().exists && options.cleanupOnFailure,
            };
            if (!destinationInfo.value().exists)
            {
                auto created = destinationSystem.CreateDirectory(
                        toResolved.value().translatedPath, DirectoryCreateOptions {.ignoreIfExists = false});
                if (!created.has_value())
                {
                    co_await NGIN::Async::DomainFailure(std::move(created.error()));
                    co_return;
                }
            }

            auto enumerator = sourceSystem.Enumerate(
                    fromResolved.value().translatedPath,
                    EnumerateOptions {
                            .includeFiles       = true,
                            .includeDirectories = true,
                            .includeSymlinks    = true,
                    });
            if (!enumerator.has_value())
            {
                co_await NGIN::Async::DomainFailure(std::move(enumerator.error()));
                co_return;
            }
            for (;;)
            {
                if (ctx.IsCancellationRequested())
                {
                    co_await NGIN::Async::Canceled();
                    co_return;
                }
                auto next = enumerator.value().Next();
                if (!next.has_value())
                {
                    co_await NGIN::Async::DomainFailure(std::move(next.error()));
                    co_return;
                }
                if (!next.value().HasEntry())
                    break;
                const auto name = next.value().Entry().name;
                co_await CopyFileAsync(ctx, from.Join(name.View()), to.Join(name.View()), options);
            }

            if (options.preservePermissions)
            {
                auto permissions = destinationSystem.SetPermissions(
                        toResolved.value().translatedPath, sourceInfo.value().permissions, SymlinkMode::Follow);
                if (!permissions.has_value())
                {
                    co_await NGIN::Async::DomainFailure(std::move(permissions.error()));
                    co_return;
                }
            }
            cleanup.Release();
            co_return;
        }

        if (sourceInfo.value().type != EntryType::File)
        {
            co_await NGIN::Async::DomainFailure(
                    MakeError(IOErrorCode::NotSupported, "cross-mount async copy is unsupported for this entry type", from, to));
            co_return;
        }

        FileOpenOptions sourceOptions;
        sourceOptions.access      = FileAccess::Read;
        sourceOptions.share       = FileShare::Read;
        sourceOptions.disposition = FileCreateDisposition::OpenExisting;

        FileOpenOptions destinationOptions;
        destinationOptions.access      = FileAccess::Write;
        destinationOptions.share       = FileShare::Read;
        destinationOptions.disposition = options.overwriteExisting ? FileCreateDisposition::CreateAlways : FileCreateDisposition::CreateNew;

        auto ioContext   = ctx.WithCancellationToken({});
        auto source      = co_await sourceAsync->OpenFileAsync(ioContext, fromResolved.value().translatedPath, sourceOptions);
        auto destination = co_await destinationAsync->OpenFileAsync(
                ioContext, toResolved.value().translatedPath, destinationOptions);
        CopyCleanup cleanup {
                .system = &destinationSystem,
                .path   = toResolved.value().translatedPath,
                .active = !destinationInfo.value().exists && options.cleanupOnFailure,
        };

        std::array<Byte, 64 * 1024> buffer {};
        for (;;)
        {
            if (ctx.IsCancellationRequested())
            {
                auto closeSource      = source.CloseAsync(ioContext);
                auto closeDestination = destination.CloseAsync(ioContext);
                co_await closeSource;
                co_await closeDestination;
                co_await NGIN::Async::Canceled();
                co_return;
            }

            const auto read = co_await source.ReadAsync(ioContext, buffer);
            if (read == 0)
                break;
            UIntSize written = 0;
            while (written < read)
            {
                const auto count = co_await destination.WriteAsync(
                        ioContext, std::span<const Byte>(buffer.data() + written, read - written));
                if (count == 0)
                {
                    co_await NGIN::Async::DomainFailure(
                            MakeError(IOErrorCode::EndOfStream, "zero-byte write during cross-mount async copy", from, to));
                    co_return;
                }
                written += count;
            }
        }

        auto closeSource      = source.CloseAsync(ioContext);
        auto closeDestination = destination.CloseAsync(ioContext);
        co_await closeSource;
        co_await closeDestination;
        if (options.preservePermissions)
        {
            auto permissions = destinationSystem.SetPermissions(
                    toResolved.value().translatedPath, sourceInfo.value().permissions, SymlinkMode::Follow);
            if (!permissions.has_value())
            {
                co_await NGIN::Async::DomainFailure(std::move(permissions.error()));
                co_return;
            }
        }
        cleanup.Release();
        co_return;
    }
}// namespace NGIN::IO
