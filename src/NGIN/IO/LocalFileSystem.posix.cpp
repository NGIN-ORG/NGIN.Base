#include <NGIN/IO/LocalFileSystem.hpp>

#include <NGIN/Utilities/Expected.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <new>
#include <string_view>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace NGIN::IO
{
    namespace
    {
        using NativePath = NGIN::Text::String;

        [[nodiscard]] IOError MakeError(IOErrorCode code, std::string_view message, const Path& path = {}, const Path& secondary = {}) noexcept
        {
            IOError error;
            error.code          = code;
            error.path          = path;
            error.secondaryPath = secondary;
            error.message       = message;
            return error;
        }

        [[nodiscard]] IOErrorCode MapErrnoCode(const int code) noexcept
        {
            switch (code)
            {
                case 0:
                    return IOErrorCode::None;
                case ENOENT:
                    return IOErrorCode::NotFound;
                case EEXIST:
                    return IOErrorCode::AlreadyExists;
                case EACCES:
                case EPERM:
                    return IOErrorCode::PermissionDenied;
                case EISDIR:
                    return IOErrorCode::IsDirectory;
                case ENOTDIR:
                    return IOErrorCode::NotDirectory;
                case ENOTEMPTY:
                    return IOErrorCode::DirectoryNotEmpty;
                case ENAMETOOLONG:
                    return IOErrorCode::PathTooLong;
                case EXDEV:
                    return IOErrorCode::CrossDevice;
                case EBUSY:
                    return IOErrorCode::Busy;
                default:
                    return IOErrorCode::SystemError;
            }
        }

        [[nodiscard]] IOError MakeErrnoError(const int code, std::string_view message, const Path& path = {}, const Path& secondary = {}) noexcept
        {
            IOError error;
            error.code          = MapErrnoCode(code);
            error.systemCode    = code;
            error.path          = path;
            error.secondaryPath = secondary;
            error.message       = message;
            return error;
        }

        [[nodiscard]] NativePath ToNativePath(const Path& path) noexcept
        {
            return path.ToNative();
        }

        [[nodiscard]] Path FromNativePath(std::string_view nativePath)
        {
            Path path {nativePath};
            path.Normalize();
            return path;
        }

        [[nodiscard]] FileTime ToFileTime(const timespec& ts) noexcept
        {
            FileTime out;
            out.unixNanoseconds = static_cast<Int64>(ts.tv_sec) * 1'000'000'000LL + static_cast<Int64>(ts.tv_nsec);
            out.valid           = true;
            return out;
        }

        [[nodiscard]] FileTime ToAccessTime(const struct stat& st) noexcept
        {
#if defined(__APPLE__)
            return ToFileTime(st.st_atimespec);
#else
            return ToFileTime(st.st_atim);
#endif
        }

        [[nodiscard]] FileTime ToModifyTime(const struct stat& st) noexcept
        {
#if defined(__APPLE__)
            return ToFileTime(st.st_mtimespec);
#else
            return ToFileTime(st.st_mtim);
#endif
        }

        [[nodiscard]] FileTime ToChangeTime(const struct stat& st) noexcept
        {
#if defined(__APPLE__)
            return ToFileTime(st.st_ctimespec);
#else
            return ToFileTime(st.st_ctim);
#endif
        }

        [[nodiscard]] FilePermissions ToPermissions(const mode_t mode) noexcept
        {
            FilePermissions out;
            out.nativeBits = static_cast<UInt32>(mode);
            out.readable   = (mode & (S_IRUSR | S_IRGRP | S_IROTH)) != 0;
            out.writable   = (mode & (S_IWUSR | S_IWGRP | S_IWOTH)) != 0;
            out.executable = (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
            out.setUserId  = (mode & S_ISUID) != 0;
            out.setGroupId = (mode & S_ISGID) != 0;
            out.sticky     = (mode & S_ISVTX) != 0;
            return out;
        }

        [[nodiscard]] EntryType ToEntryType(const mode_t mode) noexcept
        {
            if (S_ISREG(mode))
                return EntryType::File;
            if (S_ISDIR(mode))
                return EntryType::Directory;
            if (S_ISLNK(mode))
                return EntryType::Symlink;
            if (S_ISBLK(mode))
                return EntryType::BlockDevice;
            if (S_ISCHR(mode))
                return EntryType::CharacterDevice;
            if (S_ISFIFO(mode))
                return EntryType::Fifo;
            if (S_ISSOCK(mode))
                return EntryType::Socket;
            return EntryType::Other;
        }

        [[nodiscard]] bool IsSizedEntryType(const EntryType type) noexcept
        {
            switch (type)
            {
                case EntryType::File:
                case EntryType::BlockDevice:
                case EntryType::CharacterDevice:
                case EntryType::Fifo:
                case EntryType::Socket:
                    return true;
                default:
                    return false;
            }
        }

        [[nodiscard]] bool IncludeEntry(const DirectoryEntry& entry, const EnumerateOptions& options) noexcept
        {
            switch (entry.type)
            {
                case EntryType::File:
                case EntryType::BlockDevice:
                case EntryType::CharacterDevice:
                case EntryType::Fifo:
                case EntryType::Socket:
                    return options.includeFiles;
                case EntryType::Directory:
                    return options.includeDirectories;
                case EntryType::Symlink:
                    return options.includeSymlinks;
                default:
                    return true;
            }
        }

        [[nodiscard]] Result<FileInfo> BuildFileInfo(const Path& path, const MetadataOptions& options) noexcept
        {
            const NativePath nativePath = ToNativePath(path);
            FileInfo         info;
            info.path = path;

            struct stat st {};
            const int   rc = options.symlinkMode == SymlinkMode::Follow ? ::stat(nativePath.CStr(), &st) : ::lstat(nativePath.CStr(), &st);
            if (rc != 0)
            {
                if ((errno == ENOENT || errno == ENOTDIR) && options.symlinkMode == SymlinkMode::Follow)
                {
                    struct stat linkStat {};
                    if (::lstat(nativePath.CStr(), &linkStat) == 0 && S_ISLNK(linkStat.st_mode))
                    {
                        info.exists                 = true;
                        info.type                   = EntryType::Symlink;
                        info.permissions            = ToPermissions(linkStat.st_mode);
                        info.ownership.userId       = static_cast<UInt32>(linkStat.st_uid);
                        info.ownership.groupId      = static_cast<UInt32>(linkStat.st_gid);
                        info.ownership.valid        = true;
                        info.identity.device        = static_cast<UInt64>(linkStat.st_dev);
                        info.identity.inode         = static_cast<UInt64>(linkStat.st_ino);
                        info.identity.hardLinkCount = static_cast<UInt64>(linkStat.st_nlink);
                        info.identity.valid         = true;
                        info.accessed               = ToAccessTime(linkStat);
                        info.modified               = ToModifyTime(linkStat);
                        info.changed                = ToChangeTime(linkStat);
                        info.symlinkTargetExists    = false;
                        return Result<FileInfo>(std::move(info));
                    }
                }

                if (errno == ENOENT || errno == ENOTDIR)
                {
                    info.exists = false;
                    info.type   = EntryType::None;
                    return Result<FileInfo>(std::move(info));
                }

                return Result<FileInfo>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "stat failed", path)));
            }

            info.exists                 = true;
            info.type                   = ToEntryType(st.st_mode);
            info.size                   = IsSizedEntryType(info.type) ? static_cast<UInt64>(st.st_size) : 0;
            info.permissions            = ToPermissions(st.st_mode);
            info.ownership.userId       = static_cast<UInt32>(st.st_uid);
            info.ownership.groupId      = static_cast<UInt32>(st.st_gid);
            info.ownership.valid        = true;
            info.identity.device        = static_cast<UInt64>(st.st_dev);
            info.identity.inode         = static_cast<UInt64>(st.st_ino);
            info.identity.hardLinkCount = static_cast<UInt64>(st.st_nlink);
            info.identity.valid         = true;
            info.accessed               = ToAccessTime(st);
            info.modified               = ToModifyTime(st);
            info.changed                = ToChangeTime(st);
            info.symlinkTargetExists    = true;

            if (options.symlinkMode == SymlinkMode::DoNotFollow && info.type == EntryType::Symlink)
            {
                struct stat target {};
                info.symlinkTargetExists = ::stat(nativePath.CStr(), &target) == 0;
            }

            return Result<FileInfo>(std::move(info));
        }

        [[nodiscard]] mode_t ToModeBits(const FilePermissions& permissions) noexcept
        {
            if (permissions.nativeBits != 0)
                return static_cast<mode_t>(permissions.nativeBits);

            mode_t mode = 0;
            if (permissions.readable)
                mode |= static_cast<mode_t>(S_IRUSR | S_IRGRP | S_IROTH);
            if (permissions.writable)
                mode |= static_cast<mode_t>(S_IWUSR | S_IWGRP | S_IWOTH);
            if (permissions.executable)
                mode |= static_cast<mode_t>(S_IXUSR | S_IXGRP | S_IXOTH);
            if (permissions.setUserId)
                mode |= static_cast<mode_t>(S_ISUID);
            if (permissions.setGroupId)
                mode |= static_cast<mode_t>(S_ISGID);
            if (permissions.sticky)
                mode |= static_cast<mode_t>(S_ISVTX);
            return mode;
        }

        [[nodiscard]] Result<Path> CanonicalizeExistingPath(const Path& path) noexcept
        {
            const auto nativePath = ToNativePath(path);
            char*      resolved   = ::realpath(nativePath.CStr(), nullptr);
            if (resolved == nullptr)
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "realpath failed", path)));

            Path output = FromNativePath(std::string_view(resolved));
            std::free(resolved);
            return Result<Path>(std::move(output));
        }

        [[nodiscard]] Result<Path> MakeAbsolutePath(const Path& path, const Path& base) noexcept
        {
            Path absolute = path;
            if (absolute.IsAbsolute())
            {
                absolute.Normalize();
                return Result<Path>(std::move(absolute));
            }

            Path resolvedBase = base;
            if (resolvedBase.IsEmpty())
            {
                std::vector<char> buffer(256, '\0');
                for (;;)
                {
                    if (::getcwd(buffer.data(), buffer.size()) != nullptr)
                    {
                        resolvedBase = FromNativePath(std::string_view(buffer.data()));
                        break;
                    }
                    if (errno != ERANGE)
                        return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "getcwd failed")));
                    buffer.resize(buffer.size() * 2);
                }
            }
            else if (resolvedBase.IsRelative())
            {
                auto absoluteBase = MakeAbsolutePath(resolvedBase, {});
                if (!absoluteBase.HasValue())
                    return absoluteBase;
                resolvedBase = std::move(absoluteBase.Value());
            }

            absolute = resolvedBase.Join(path.View());
            absolute.Normalize();
            return Result<Path>(std::move(absolute));
        }

        [[nodiscard]] Result<Path> MakeWeaklyCanonicalPath(const Path& path) noexcept
        {
            auto canonical = CanonicalizeExistingPath(path);
            if (canonical.HasValue())
                return canonical;

            auto absolute = MakeAbsolutePath(path, {});
            if (!absolute.HasValue())
                return absolute;

            Path                     current = absolute.Value();
            std::vector<std::string> suffixes;

            while (!current.IsEmpty())
            {
                auto currentInfo = BuildFileInfo(current, MetadataOptions {.symlinkMode = SymlinkMode::Follow});
                if (currentInfo.HasValue() && currentInfo.Value().exists)
                    break;

                const auto filename = current.Filename();
                if (filename.empty())
                    break;
                suffixes.emplace_back(filename);
                current = current.Parent();
            }

            if (current.IsEmpty())
                return absolute;

            auto baseCanonical = CanonicalizeExistingPath(current);
            if (!baseCanonical.HasValue())
                return baseCanonical;

            Path output = std::move(baseCanonical.Value());
            for (auto it = suffixes.rbegin(); it != suffixes.rend(); ++it)
                output.Append(*it);
            output.Normalize();
            return Result<Path>(std::move(output));
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

        [[nodiscard]] int BuildOpenFlags(const FileOpenOptions& options) noexcept
        {
            int openFlags = 0;
            switch (options.access)
            {
                case FileAccess::Read:
                    openFlags |= O_RDONLY;
                    break;
                case FileAccess::Write:
                    openFlags |= O_WRONLY;
                    break;
                case FileAccess::ReadWrite:
                    openFlags |= O_RDWR;
                    break;
                case FileAccess::Append:
                    openFlags |= O_WRONLY | O_APPEND;
                    break;
            }

            switch (options.disposition)
            {
                case FileCreateDisposition::OpenExisting:
                    break;
                case FileCreateDisposition::CreateAlways:
                    openFlags |= O_CREAT | O_TRUNC;
                    break;
                case FileCreateDisposition::CreateNew:
                    openFlags |= O_CREAT | O_EXCL;
                    break;
                case FileCreateDisposition::OpenAlways:
                    openFlags |= O_CREAT;
                    break;
                case FileCreateDisposition::TruncateExisting:
                    openFlags |= O_TRUNC;
                    break;
            }

#if defined(O_CLOEXEC)
            openFlags |= O_CLOEXEC;
#endif
#if defined(O_SYNC)
            if ((options.flags & FileOpenFlags::WriteThrough) == FileOpenFlags::WriteThrough)
                openFlags |= O_SYNC;
#endif
            return openFlags;
        }

        [[nodiscard]] Result<FileInfo> BuildFileInfoAt(
                const int directoryFd, const Path& resolvedPath, const Path& relativePath, const MetadataOptions& options) noexcept
        {
            auto relativeResult = NormalizeRelativeHandlePath(relativePath);
            if (!relativeResult.HasValue())
                return Result<FileInfo>(NGIN::Utilities::Unexpected<IOError>(std::move(relativeResult.Error())));

            const auto nativeRelative = relativeResult.Value().ToNative();
            FileInfo   info;
            info.path = resolvedPath;

            struct stat st {};
            const int   flags = options.symlinkMode == SymlinkMode::DoNotFollow ? AT_SYMLINK_NOFOLLOW : 0;
            if (::fstatat(directoryFd, nativeRelative.CStr(), &st, flags) != 0)
            {
                if ((errno == ENOENT || errno == ENOTDIR) && options.symlinkMode == SymlinkMode::Follow)
                {
                    struct stat linkStat {};
                    if (::fstatat(directoryFd, nativeRelative.CStr(), &linkStat, AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(linkStat.st_mode))
                    {
                        info.exists                 = true;
                        info.type                   = EntryType::Symlink;
                        info.permissions            = ToPermissions(linkStat.st_mode);
                        info.ownership.userId       = static_cast<UInt32>(linkStat.st_uid);
                        info.ownership.groupId      = static_cast<UInt32>(linkStat.st_gid);
                        info.ownership.valid        = true;
                        info.identity.device        = static_cast<UInt64>(linkStat.st_dev);
                        info.identity.inode         = static_cast<UInt64>(linkStat.st_ino);
                        info.identity.hardLinkCount = static_cast<UInt64>(linkStat.st_nlink);
                        info.identity.valid         = true;
                        info.accessed               = ToAccessTime(linkStat);
                        info.modified               = ToModifyTime(linkStat);
                        info.changed                = ToChangeTime(linkStat);
                        info.symlinkTargetExists    = false;
                        return Result<FileInfo>(std::move(info));
                    }
                }

                if (errno == ENOENT || errno == ENOTDIR)
                {
                    info.exists = false;
                    info.type   = EntryType::None;
                    return Result<FileInfo>(std::move(info));
                }

                return Result<FileInfo>(
                        NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "fstatat failed", resolvedPath)));
            }

            info.exists                 = true;
            info.type                   = ToEntryType(st.st_mode);
            info.size                   = IsSizedEntryType(info.type) ? static_cast<UInt64>(st.st_size) : 0;
            info.permissions            = ToPermissions(st.st_mode);
            info.ownership.userId       = static_cast<UInt32>(st.st_uid);
            info.ownership.groupId      = static_cast<UInt32>(st.st_gid);
            info.ownership.valid        = true;
            info.identity.device        = static_cast<UInt64>(st.st_dev);
            info.identity.inode         = static_cast<UInt64>(st.st_ino);
            info.identity.hardLinkCount = static_cast<UInt64>(st.st_nlink);
            info.identity.valid         = true;
            info.accessed               = ToAccessTime(st);
            info.modified               = ToModifyTime(st);
            info.changed                = ToChangeTime(st);
            info.symlinkTargetExists    = true;

            if (options.symlinkMode == SymlinkMode::DoNotFollow && info.type == EntryType::Symlink)
            {
                struct stat target {};
                info.symlinkTargetExists = ::fstatat(directoryFd, nativeRelative.CStr(), &target, 0) == 0;
            }

            return Result<FileInfo>(std::move(info));
        }

        class VectorDirectoryEnumerator final : public IDirectoryEnumerator
        {
        public:
            explicit VectorDirectoryEnumerator(std::vector<DirectoryEntry> entries)
                : m_entries(std::move(entries))
            {
            }

            Result<bool> Next() noexcept override
            {
                if (m_index >= m_entries.size())
                    return Result<bool>(false);
                m_current = m_index;
                ++m_index;
                return Result<bool>(true);
            }

            [[nodiscard]] const DirectoryEntry& Current() const noexcept override
            {
                static const DirectoryEntry empty {};
                if (m_current >= m_entries.size())
                    return empty;
                return m_entries[m_current];
            }

        private:
            std::vector<DirectoryEntry> m_entries {};
            std::size_t                 m_index {0};
            std::size_t                 m_current {static_cast<std::size_t>(-1)};
        };

        class LocalFileHandle final : public IFileHandle, public IAsyncFileHandle
        {
        public:
            static Result<std::unique_ptr<LocalFileHandle>> Open(const Path& path, const FileOpenOptions& options) noexcept
            {
                try
                {
                    auto handle = std::unique_ptr<LocalFileHandle>(new LocalFileHandle());
                    auto result = handle->OpenImpl(path, options);
                    if (!result.HasValue())
                    {
                        return Result<std::unique_ptr<LocalFileHandle>>(
                                NGIN::Utilities::Unexpected<IOError>(std::move(result.Error())));
                    }
                    return Result<std::unique_ptr<LocalFileHandle>>(std::move(handle));
                } catch (const std::bad_alloc&)
                {
                    return Result<std::unique_ptr<LocalFileHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "allocation failed", path)));
                }
            }

            static Result<std::unique_ptr<LocalFileHandle>> OpenAt(
                    const int directoryFd, const Path& basePath, const Path& relativePath, const FileOpenOptions& options) noexcept
            {
                auto normalizedRelative = NormalizeRelativeHandlePath(relativePath);
                if (!normalizedRelative.HasValue())
                {
                    return Result<std::unique_ptr<LocalFileHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(std::move(normalizedRelative.Error())));
                }

                const Path resolvedPath = JoinHandlePath(basePath, normalizedRelative.Value());
                try
                {
                    auto handle = std::unique_ptr<LocalFileHandle>(new LocalFileHandle());
                    auto result = handle->OpenAtImpl(directoryFd, resolvedPath, normalizedRelative.Value(), options);
                    if (!result.HasValue())
                    {
                        return Result<std::unique_ptr<LocalFileHandle>>(
                                NGIN::Utilities::Unexpected<IOError>(std::move(result.Error())));
                    }
                    return Result<std::unique_ptr<LocalFileHandle>>(std::move(handle));
                } catch (const std::bad_alloc&)
                {
                    return Result<std::unique_ptr<LocalFileHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "allocation failed", resolvedPath)));
                }
            }

            ~LocalFileHandle() override
            {
                Close();
            }

            Result<UIntSize> Read(std::span<NGIN::Byte> destination) noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!IsOpenUnlocked())
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));
                if (!m_canRead)
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotSupported, "file not opened for read", m_path)));
                if (destination.empty())
                    return Result<UIntSize>(UIntSize {0});

                for (;;)
                {
                    const ssize_t bytesRead = ::read(m_fd, destination.data(), destination.size());
                    if (bytesRead >= 0)
                        return Result<UIntSize>(static_cast<UIntSize>(bytesRead));
                    if (errno == EINTR)
                        continue;
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "read failed", m_path)));
                }
            }

            Result<UIntSize> Write(std::span<const NGIN::Byte> source) noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!IsOpenUnlocked())
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));
                if (!m_canWrite)
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotSupported, "file not opened for write", m_path)));
                if (source.empty())
                    return Result<UIntSize>(UIntSize {0});

                for (;;)
                {
                    const ssize_t bytesWritten = ::write(m_fd, source.data(), source.size());
                    if (bytesWritten >= 0)
                        return Result<UIntSize>(static_cast<UIntSize>(bytesWritten));
                    if (errno == EINTR)
                        continue;
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "write failed", m_path)));
                }
            }

            Result<UIntSize> ReadAt(UInt64 offset, std::span<NGIN::Byte> destination) noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!IsOpenUnlocked())
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));
                if (!m_canRead)
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotSupported, "file not opened for read", m_path)));
                if (destination.empty())
                    return Result<UIntSize>(UIntSize {0});

                for (;;)
                {
                    const ssize_t bytesRead = ::pread(m_fd, destination.data(), destination.size(), static_cast<off_t>(offset));
                    if (bytesRead >= 0)
                        return Result<UIntSize>(static_cast<UIntSize>(bytesRead));
                    if (errno == EINTR)
                        continue;
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "pread failed", m_path)));
                }
            }

            Result<UIntSize> WriteAt(UInt64 offset, std::span<const NGIN::Byte> source) noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!IsOpenUnlocked())
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));
                if (!m_canWrite)
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotSupported, "file not opened for write", m_path)));
                if (source.empty())
                    return Result<UIntSize>(UIntSize {0});

                for (;;)
                {
                    const ssize_t bytesWritten = ::pwrite(m_fd, source.data(), source.size(), static_cast<off_t>(offset));
                    if (bytesWritten >= 0)
                        return Result<UIntSize>(static_cast<UIntSize>(bytesWritten));
                    if (errno == EINTR)
                        continue;
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "pwrite failed", m_path)));
                }
            }

            ResultVoid Flush() noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!IsOpenUnlocked())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));
                if (::fsync(m_fd) != 0)
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "fsync failed", m_path)));
                return {};
            }

            ResultVoid Seek(Int64 offset, SeekOrigin origin) noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!IsOpenUnlocked())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));

                int whence = SEEK_SET;
                if (origin == SeekOrigin::Current)
                    whence = SEEK_CUR;
                else if (origin == SeekOrigin::End)
                    whence = SEEK_END;

                if (::lseek(m_fd, static_cast<off_t>(offset), whence) == static_cast<off_t>(-1))
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "lseek failed", m_path)));
                return {};
            }

            Result<UInt64> Tell() const noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!IsOpenUnlocked())
                    return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));

                const off_t position = ::lseek(m_fd, 0, SEEK_CUR);
                if (position == static_cast<off_t>(-1))
                    return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "lseek failed", m_path)));
                return Result<UInt64>(static_cast<UInt64>(position));
            }

            Result<UInt64> Size() const noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!IsOpenUnlocked())
                    return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));

                struct stat st {};
                if (::fstat(m_fd, &st) != 0)
                    return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "fstat failed", m_path)));
                return Result<UInt64>(static_cast<UInt64>(st.st_size));
            }

            ResultVoid SetSize(UInt64 size) noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!IsOpenUnlocked())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));
                if (::ftruncate(m_fd, static_cast<off_t>(size)) != 0)
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "ftruncate failed", m_path)));
                return {};
            }

            void Close() noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (m_fd >= 0)
                {
                    ::close(m_fd);
                    m_fd = -1;
                }
            }

            [[nodiscard]] bool IsOpen() const noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                return IsOpenUnlocked();
            }

            AsyncTask<UIntSize> ReadAsync(NGIN::Async::TaskContext& ctx, std::span<NGIN::Byte> destination) override
            {
                co_await ctx.YieldNow();
                co_return ToAsyncResult(Read(destination));
            }

            AsyncTask<UIntSize> WriteAsync(NGIN::Async::TaskContext& ctx, std::span<const NGIN::Byte> source) override
            {
                co_await ctx.YieldNow();
                co_return ToAsyncResult(Write(source));
            }

            AsyncTask<UIntSize> ReadAtAsync(NGIN::Async::TaskContext& ctx, UInt64 offset, std::span<NGIN::Byte> destination) override
            {
                co_await ctx.YieldNow();
                co_return ToAsyncResult(ReadAt(offset, destination));
            }

            AsyncTask<UIntSize> WriteAtAsync(NGIN::Async::TaskContext& ctx, UInt64 offset, std::span<const NGIN::Byte> source) override
            {
                co_await ctx.YieldNow();
                co_return ToAsyncResult(WriteAt(offset, source));
            }

            AsyncTaskVoid FlushAsync(NGIN::Async::TaskContext& ctx) override
            {
                co_await ctx.YieldNow();
                auto flushed = Flush();
                if (!flushed)
                {
                    co_await AsyncTaskVoid::ReturnError(std::move(flushed).TakeError());
                    co_return;
                }
                co_return;
            }

            AsyncTaskVoid CloseAsync(NGIN::Async::TaskContext& ctx) override
            {
                co_await ctx.YieldNow();
                Close();
                co_return;
            }

        private:
            [[nodiscard]] bool IsOpenUnlocked() const noexcept
            {
                return m_fd >= 0;
            }

            ResultVoid OpenImpl(const Path& path, const FileOpenOptions& options) noexcept
            {
                m_path     = path;
                m_canRead  = options.access == FileAccess::Read || options.access == FileAccess::ReadWrite;
                m_canWrite = options.access == FileAccess::Write || options.access == FileAccess::ReadWrite || options.access == FileAccess::Append;
                m_fd       = ::open(ToNativePath(path).CStr(), BuildOpenFlags(options), 0666);
                if (m_fd < 0)
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "open failed", m_path)));
                return {};
            }

            ResultVoid OpenAtImpl(const int directoryFd, const Path& resolvedPath, const Path& relativePath, const FileOpenOptions& options) noexcept
            {
                m_path     = resolvedPath;
                m_canRead  = options.access == FileAccess::Read || options.access == FileAccess::ReadWrite;
                m_canWrite = options.access == FileAccess::Write || options.access == FileAccess::ReadWrite || options.access == FileAccess::Append;

                const auto nativeRelative = relativePath.ToNative();
                m_fd                      = ::openat(directoryFd, nativeRelative.CStr(), BuildOpenFlags(options), 0666);
                if (m_fd < 0)
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "openat failed", m_path)));
                return {};
            }

            mutable std::mutex m_mutex {};
            Path               m_path {};
            bool               m_canRead {false};
            bool               m_canWrite {false};
            int                m_fd {-1};
        };

        class LocalDirectoryHandle final : public IDirectoryHandle
        {
        public:
            static Result<std::unique_ptr<LocalDirectoryHandle>> Open(const Path& path) noexcept
            {
                try
                {
                    auto handle = std::unique_ptr<LocalDirectoryHandle>(new LocalDirectoryHandle());
                    auto result = handle->OpenImpl(path);
                    if (!result.HasValue())
                    {
                        return Result<std::unique_ptr<LocalDirectoryHandle>>(
                                NGIN::Utilities::Unexpected<IOError>(std::move(result.Error())));
                    }
                    return Result<std::unique_ptr<LocalDirectoryHandle>>(std::move(handle));
                } catch (const std::bad_alloc&)
                {
                    return Result<std::unique_ptr<LocalDirectoryHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "allocation failed", path)));
                }
            }

            static Result<std::unique_ptr<LocalDirectoryHandle>> OpenAt(const int directoryFd, const Path& basePath, const Path& relativePath) noexcept
            {
                auto normalized = NormalizeRelativeHandlePath(relativePath);
                if (!normalized.HasValue())
                {
                    return Result<std::unique_ptr<LocalDirectoryHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));
                }

                const Path resolvedPath = JoinHandlePath(basePath, normalized.Value());
                try
                {
                    auto handle = std::unique_ptr<LocalDirectoryHandle>(new LocalDirectoryHandle());
                    auto result = handle->OpenAtImpl(directoryFd, resolvedPath, normalized.Value());
                    if (!result.HasValue())
                    {
                        return Result<std::unique_ptr<LocalDirectoryHandle>>(
                                NGIN::Utilities::Unexpected<IOError>(std::move(result.Error())));
                    }
                    return Result<std::unique_ptr<LocalDirectoryHandle>>(std::move(handle));
                } catch (const std::bad_alloc&)
                {
                    return Result<std::unique_ptr<LocalDirectoryHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "allocation failed", resolvedPath)));
                }
            }

            ~LocalDirectoryHandle() override
            {
                if (m_directoryFd >= 0)
                    ::close(m_directoryFd);
            }

            Result<bool> Exists(const Path& path) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return Result<bool>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));

                struct stat st {};
                const auto  nativeRelative = normalized.Value().ToNative();
                if (::fstatat(m_directoryFd, nativeRelative.CStr(), &st, AT_SYMLINK_NOFOLLOW) == 0)
                    return Result<bool>(true);
                if (errno == ENOENT || errno == ENOTDIR)
                    return Result<bool>(false);
                return Result<bool>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "fstatat failed", JoinHandlePath(m_path, normalized.Value()))));
            }

            Result<FileInfo> GetInfo(const Path& path, const MetadataOptions& options = {}) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return Result<FileInfo>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));
                return BuildFileInfoAt(m_directoryFd, JoinHandlePath(m_path, normalized.Value()), normalized.Value(), options);
            }

            Result<FileHandle> OpenFile(const Path& path, const FileOpenOptions& options) noexcept override
            {
                auto opened = LocalFileHandle::OpenAt(m_directoryFd, m_path, path, options);
                if (!opened.HasValue())
                    return Result<FileHandle>(NGIN::Utilities::Unexpected<IOError>(std::move(opened.Error())));
                return Result<FileHandle>(FileHandle(std::move(opened).TakeValue()));
            }

            Result<DirectoryHandle> OpenDirectory(const Path& path) noexcept override
            {
                auto opened = OpenAt(m_directoryFd, m_path, path);
                if (!opened.HasValue())
                    return Result<DirectoryHandle>(NGIN::Utilities::Unexpected<IOError>(std::move(opened.Error())));
                return Result<DirectoryHandle>(DirectoryHandle(std::move(opened).TakeValue()));
            }

            ResultVoid CreateDirectory(const Path& path, const DirectoryCreateOptions& options = {}) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));

                if (options.recursive)
                    return m_fileSystem.CreateDirectories(JoinHandlePath(m_path, normalized.Value()), options);

                const auto nativeRelative = normalized.Value().ToNative();
                if (::mkdirat(m_directoryFd, nativeRelative.CStr(), 0777) == 0)
                    return {};
                if (options.ignoreIfExists && errno == EEXIST)
                {
                    auto existing = BuildFileInfoAt(m_directoryFd, JoinHandlePath(m_path, normalized.Value()), normalized.Value(), {});
                    if (existing.HasValue() && existing.Value().exists && existing.Value().type == EntryType::Directory)
                        return {};
                }
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "mkdirat failed", JoinHandlePath(m_path, normalized.Value()))));
            }

            ResultVoid RemoveFile(const Path& path, const RemoveOptions& options = {}) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));

                const auto nativeRelative = normalized.Value().ToNative();
                if (::unlinkat(m_directoryFd, nativeRelative.CStr(), 0) == 0)
                    return {};
                if (options.ignoreMissing && (errno == ENOENT || errno == ENOTDIR))
                    return {};
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "unlinkat failed", JoinHandlePath(m_path, normalized.Value()))));
            }

            ResultVoid RemoveDirectory(const Path& path, const RemoveOptions& options = {}) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));

                if (options.recursive)
                    return m_fileSystem.RemoveDirectory(JoinHandlePath(m_path, normalized.Value()), options);

                const auto nativeRelative = normalized.Value().ToNative();
                if (::unlinkat(m_directoryFd, nativeRelative.CStr(), AT_REMOVEDIR) == 0)
                    return {};
                if (options.ignoreMissing && (errno == ENOENT || errno == ENOTDIR))
                    return {};
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "unlinkat directory failed", JoinHandlePath(m_path, normalized.Value()))));
            }

            Result<Path> ReadSymlink(const Path& path) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));

                const auto        nativeRelative = normalized.Value().ToNative();
                std::vector<char> buffer(256, '\0');
                for (;;)
                {
                    const ssize_t count = ::readlinkat(m_directoryFd, nativeRelative.CStr(), buffer.data(), buffer.size());
                    if (count < 0)
                    {
                        return Result<Path>(NGIN::Utilities::Unexpected<IOError>(
                                MakeErrnoError(errno, "readlinkat failed", JoinHandlePath(m_path, normalized.Value()))));
                    }
                    if (static_cast<std::size_t>(count) < buffer.size())
                        return Result<Path>(Path::FromNative(std::string_view(buffer.data(), static_cast<std::size_t>(count))));
                    buffer.resize(buffer.size() * 2);
                }
            }

        private:
            ResultVoid OpenImpl(const Path& path) noexcept
            {
                m_path        = path.LexicallyNormal();
                m_directoryFd = ::open(ToNativePath(m_path).CStr(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
                if (m_directoryFd < 0)
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "open directory failed", m_path)));
                return {};
            }

            ResultVoid OpenAtImpl(const int directoryFd, const Path& resolvedPath, const Path& relativePath) noexcept
            {
                m_path                    = resolvedPath;
                const auto nativeRelative = relativePath.ToNative();
                m_directoryFd             = ::openat(directoryFd, nativeRelative.CStr(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
                if (m_directoryFd < 0)
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "openat directory failed", m_path)));
                return {};
            }

            Path            m_path {};
            int             m_directoryFd {-1};
            LocalFileSystem m_fileSystem {};
        };

        [[nodiscard]] Result<bool> ExistsNative(const Path& path) noexcept
        {
            const auto  nativePath = ToNativePath(path);
            struct stat st {};
            if (::lstat(nativePath.CStr(), &st) == 0)
                return Result<bool>(true);
            if (errno == ENOENT || errno == ENOTDIR)
                return Result<bool>(false);
            return Result<bool>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "lstat failed", path)));
        }

        ResultVoid CreateDirectoryNative(const Path& path, const DirectoryCreateOptions& options) noexcept
        {
            const auto nativePath = ToNativePath(path);
            if (::mkdir(nativePath.CStr(), 0777) == 0)
                return {};
            if (options.ignoreIfExists && errno == EEXIST)
            {
                MetadataOptions metadataOptions;
                metadataOptions.symlinkMode = SymlinkMode::Follow;
                auto existing               = BuildFileInfo(path, metadataOptions);
                if (existing.HasValue() && existing.Value().exists && existing.Value().type == EntryType::Directory)
                    return {};
            }
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "mkdir failed", path)));
        }

        ResultVoid CreateDirectoriesNative(const Path& path, const DirectoryCreateOptions& options) noexcept
        {
            Path normalized = path.LexicallyNormal();
            if (normalized.IsEmpty() || normalized.IsRoot())
                return {};

            auto parent = normalized.Parent();
            if (!parent.IsEmpty() && parent.View() != normalized.View())
            {
                auto parentCreated = CreateDirectoriesNative(parent, options);
                if (!parentCreated.HasValue())
                    return parentCreated;
            }

            return CreateDirectoryNative(normalized, options);
        }

        ResultVoid RemoveFileNative(const Path& path, const RemoveOptions& options) noexcept
        {
            const auto nativePath = ToNativePath(path);
            if (::unlink(nativePath.CStr()) == 0)
                return {};
            if (options.ignoreMissing && (errno == ENOENT || errno == ENOTDIR))
                return {};
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "unlink failed", path)));
        }

        ResultVoid     RemoveDirectoryNative(const Path& path, const RemoveOptions& options) noexcept;
        Result<UInt64> RemoveAllNative(const Path& path, const RemoveOptions& options) noexcept;

        [[nodiscard]] Result<std::vector<DirectoryEntry>> EnumerateEntries(const Path& path, const EnumerateOptions& options) noexcept
        {
            std::vector<DirectoryEntry> entries;

            const auto collect = [&](const auto& self, const Path& directoryPath) -> ResultVoid {
                const auto nativePath = ToNativePath(directoryPath);
                DIR*       directory  = ::opendir(nativePath.CStr());
                if (directory == nullptr)
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "opendir failed", directoryPath)));

                while (dirent* record = ::readdir(directory))
                {
                    const std::string_view nameView {record->d_name};
                    if (nameView == "." || nameView == "..")
                        continue;

                    Path            childPath = directoryPath.Join(nameView);
                    MetadataOptions metadataOptions;
                    metadataOptions.symlinkMode = options.followSymlinks ? SymlinkMode::Follow : SymlinkMode::DoNotFollow;
                    auto infoResult             = BuildFileInfo(childPath, metadataOptions);
                    if (!infoResult.HasValue())
                    {
                        const IOError error = std::move(infoResult.Error());
                        ::closedir(directory);
                        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(error)));
                    }

                    DirectoryEntry entry;
                    entry.path = childPath;
                    entry.name = Path {nameView};
                    entry.type = infoResult.Value().type;
                    if (options.populateInfo)
                        entry.info = std::move(infoResult.Value());

                    if (IncludeEntry(entry, options))
                        entries.push_back(std::move(entry));

                    if (options.recursive && infoResult.Value().type == EntryType::Directory)
                    {
                        auto childResult = self(self, childPath);
                        if (!childResult.HasValue())
                        {
                            const IOError error = std::move(childResult.Error());
                            ::closedir(directory);
                            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(error)));
                        }
                    }
                }

                if (::closedir(directory) != 0)
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "closedir failed", directoryPath)));
                return {};
            };

            auto collectResult = collect(collect, path);
            if (!collectResult.HasValue())
                return Result<std::vector<DirectoryEntry>>(NGIN::Utilities::Unexpected<IOError>(std::move(collectResult.Error())));

            if (options.stableSort)
            {
                std::sort(entries.begin(), entries.end(), [](const DirectoryEntry& left, const DirectoryEntry& right) {
                    return left.path.View() < right.path.View();
                });
            }

            return Result<std::vector<DirectoryEntry>>(std::move(entries));
        }

        Result<UInt64> RemoveAllNative(const Path& path, const RemoveOptions& options) noexcept
        {
            MetadataOptions metadataOptions;
            metadataOptions.symlinkMode = SymlinkMode::DoNotFollow;
            auto infoResult             = BuildFileInfo(path, metadataOptions);
            if (!infoResult.HasValue())
                return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(std::move(infoResult.Error())));

            auto& info = infoResult.Value();
            if (!info.exists)
            {
                if (options.ignoreMissing)
                    return Result<UInt64>(UInt64 {0});
                return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "path not found", path)));
            }

            if (info.type != EntryType::Directory)
            {
                auto removed = RemoveFileNative(path, options);
                if (!removed.HasValue())
                    return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(std::move(removed.Error())));
                return Result<UInt64>(UInt64 {1});
            }

            const auto nativePath = ToNativePath(path);
            DIR*       directory  = ::opendir(nativePath.CStr());
            if (directory == nullptr)
                return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "opendir failed", path)));

            UInt64 removedCount = 0;
            while (dirent* record = ::readdir(directory))
            {
                const std::string_view nameView {record->d_name};
                if (nameView == "." || nameView == "..")
                    continue;

                Path child        = path.Join(nameView);
                auto childRemoved = RemoveAllNative(child, options);
                if (!childRemoved.HasValue())
                {
                    const IOError error = std::move(childRemoved.Error());
                    ::closedir(directory);
                    return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(std::move(error)));
                }
                removedCount += childRemoved.Value();
            }

            if (::closedir(directory) != 0)
                return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "closedir failed", path)));
            if (::rmdir(nativePath.CStr()) != 0)
                return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "rmdir failed", path)));

            return Result<UInt64>(removedCount + 1);
        }

        ResultVoid RemoveDirectoryNative(const Path& path, const RemoveOptions& options) noexcept
        {
            if (options.recursive)
            {
                auto removed = RemoveAllNative(path, options);
                if (!removed.HasValue())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(removed.Error())));
                return {};
            }

            if (::rmdir(ToNativePath(path).CStr()) == 0)
                return {};
            if (options.ignoreMissing && (errno == ENOENT || errno == ENOTDIR))
                return {};
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "rmdir failed", path)));
        }

        [[nodiscard]] Result<NGIN::Text::String> ReadSymlinkTargetString(const Path& path) noexcept
        {
            const auto        nativePath = ToNativePath(path);
            std::vector<char> buffer(256, '\0');

            for (;;)
            {
                const ssize_t count = ::readlink(nativePath.CStr(), buffer.data(), buffer.size());
                if (count < 0)
                    return Result<NGIN::Text::String>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "readlink failed", path)));
                if (static_cast<std::size_t>(count) < buffer.size())
                    return Result<NGIN::Text::String>(NGIN::Text::String(std::string_view(buffer.data(), static_cast<std::size_t>(count))));
                buffer.resize(buffer.size() * 2);
            }
        }

        ResultVoid CopyRegularFile(const Path& from, const Path& to, const CopyOptions& options) noexcept
        {
            MetadataOptions metadataOptions;
            metadataOptions.symlinkMode = SymlinkMode::Follow;
            auto sourceInfo             = BuildFileInfo(from, metadataOptions);
            if (!sourceInfo.HasValue())
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(sourceInfo.Error())));

            const int sourceFd = ::open(ToNativePath(from).CStr(), O_RDONLY);
            if (sourceFd < 0)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "open source failed", from)));

            int destinationFlags = O_WRONLY | O_CREAT;
            if (options.overwriteExisting)
                destinationFlags |= O_TRUNC;
            else
                destinationFlags |= O_EXCL;

            const int destinationFd = ::open(
                    ToNativePath(to).CStr(),
                    destinationFlags,
                    static_cast<mode_t>(sourceInfo.Value().permissions.nativeBits & 0777u));
            if (destinationFd < 0)
            {
                const int error = errno;
                ::close(sourceFd);
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(error, "open destination failed", to, from)));
            }

            std::vector<NGIN::Byte> buffer(64 * 1024);
            for (;;)
            {
                ssize_t readCount = 0;
                do
                {
                    readCount = ::read(sourceFd, buffer.data(), buffer.size());
                } while (readCount < 0 && errno == EINTR);

                if (readCount < 0)
                {
                    const int error = errno;
                    ::close(destinationFd);
                    ::close(sourceFd);
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(error, "read failed during copy", from, to)));
                }
                if (readCount == 0)
                    break;

                ssize_t writtenTotal = 0;
                while (writtenTotal < readCount)
                {
                    ssize_t writeCount = 0;
                    do
                    {
                        writeCount = ::write(destinationFd, buffer.data() + writtenTotal, static_cast<std::size_t>(readCount - writtenTotal));
                    } while (writeCount < 0 && errno == EINTR);

                    if (writeCount < 0)
                    {
                        const int error = errno;
                        ::close(destinationFd);
                        ::close(sourceFd);
                        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(error, "write failed during copy", to, from)));
                    }
                    writtenTotal += writeCount;
                }
            }

            (void) ::close(destinationFd);
            (void) ::close(sourceFd);
            return {};
        }

        ResultVoid CopyPathNative(const Path& from, const Path& to, const CopyOptions& options) noexcept
        {
            MetadataOptions metadataOptions;
            metadataOptions.symlinkMode = SymlinkMode::DoNotFollow;
            auto sourceInfo             = BuildFileInfo(from, metadataOptions);
            if (!sourceInfo.HasValue())
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(sourceInfo.Error())));

            const auto& info = sourceInfo.Value();
            if (!info.exists)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "source not found", from, to)));

            if (info.type == EntryType::Directory)
            {
                if (!options.recursive)
                {
                    return ResultVoid(
                            NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotSupported, "directory copy requires recursive option", from, to)));
                }

                DirectoryCreateOptions directoryOptions;
                directoryOptions.ignoreIfExists = true;
                auto createResult               = CreateDirectoryNative(to, directoryOptions);
                if (!createResult.HasValue())
                    return createResult;

                const auto nativePath = ToNativePath(from);
                DIR*       directory  = ::opendir(nativePath.CStr());
                if (directory == nullptr)
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "opendir failed during copy", from, to)));

                while (dirent* record = ::readdir(directory))
                {
                    const std::string_view nameView {record->d_name};
                    if (nameView == "." || nameView == "..")
                        continue;

                    auto childCopy = CopyPathNative(from.Join(nameView), to.Join(nameView), options);
                    if (!childCopy.HasValue())
                    {
                        const IOError error = std::move(childCopy.Error());
                        ::closedir(directory);
                        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(error)));
                    }
                }

                if (::closedir(directory) != 0)
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "closedir failed during copy", from, to)));
                return {};
            }

            if (info.type == EntryType::Symlink)
            {
                auto targetResult = ReadSymlinkTargetString(from);
                if (!targetResult.HasValue())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(targetResult.Error())));

                if (options.overwriteExisting)
                {
                    RemoveOptions removeOptions;
                    removeOptions.recursive     = true;
                    removeOptions.ignoreMissing = true;
                    auto removed                = RemoveAllNative(to, removeOptions);
                    if (!removed.HasValue())
                        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(removed.Error())));
                }

                if (::symlink(targetResult.Value().CStr(), ToNativePath(to).CStr()) != 0)
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "symlink copy failed", to, from)));
                return {};
            }

            if (info.type != EntryType::File)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotSupported, "copy not supported for entry type", from, to)));

            return CopyRegularFile(from, to, options);
        }
    }// namespace

    FileSystemCapabilities LocalFileSystem::GetCapabilities() const noexcept
    {
        FileSystemCapabilities capabilities;
        capabilities.symlinks             = true;
        capabilities.hardLinks            = true;
        capabilities.blockDevices         = true;
        capabilities.characterDevices     = true;
        capabilities.fifos                = true;
        capabilities.sockets              = true;
        capabilities.posixModeBits        = true;
        capabilities.ownership            = true;
        capabilities.setIdBits            = true;
        capabilities.stickyBit            = true;
        capabilities.fileIdentity         = true;
        capabilities.hardLinkCount        = true;
        capabilities.memoryMappedFiles    = true;
        capabilities.nanosecondTimestamps = true;
        capabilities.metadataNoFollow     = true;
        return capabilities;
    }

    Result<bool> LocalFileSystem::Exists(const Path& path) noexcept
    {
        return ExistsNative(path);
    }

    Result<FileInfo> LocalFileSystem::GetInfo(const Path& path, const MetadataOptions& options) noexcept
    {
        return BuildFileInfo(path, options);
    }

    Result<Path> LocalFileSystem::Absolute(const Path& path, const Path& base) noexcept
    {
        return MakeAbsolutePath(path, base);
    }

    Result<Path> LocalFileSystem::Canonical(const Path& path) noexcept
    {
        return CanonicalizeExistingPath(path);
    }

    Result<Path> LocalFileSystem::WeaklyCanonical(const Path& path) noexcept
    {
        return MakeWeaklyCanonicalPath(path);
    }

    Result<bool> LocalFileSystem::SameFile(const Path& lhs, const Path& rhs) noexcept
    {
        MetadataOptions options;
        options.symlinkMode = SymlinkMode::Follow;

        auto lhsInfo = GetInfo(lhs, options);
        if (!lhsInfo.HasValue())
            return Result<bool>(NGIN::Utilities::Unexpected<IOError>(std::move(lhsInfo.Error())));

        auto rhsInfo = GetInfo(rhs, options);
        if (!rhsInfo.HasValue())
            return Result<bool>(NGIN::Utilities::Unexpected<IOError>(std::move(rhsInfo.Error())));

        if (!lhsInfo.Value().exists || !rhsInfo.Value().exists)
            return Result<bool>(false);
        if (!lhsInfo.Value().identity.valid || !rhsInfo.Value().identity.valid)
            return Result<bool>(false);

        return Result<bool>(
                lhsInfo.Value().identity.device == rhsInfo.Value().identity.device &&
                lhsInfo.Value().identity.inode == rhsInfo.Value().identity.inode);
    }

    Result<Path> LocalFileSystem::ReadSymlink(const Path& path) noexcept
    {
        auto target = ReadSymlinkTargetString(path);
        if (!target.HasValue())
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(target.Error())));

        return Result<Path>(Path::FromNative(std::string_view(target.Value().Data(), target.Value().Size())));
    }

    ResultVoid LocalFileSystem::CreateDirectory(const Path& path, const DirectoryCreateOptions& options) noexcept
    {
        return CreateDirectoryNative(path, options);
    }

    ResultVoid LocalFileSystem::CreateDirectories(const Path& path, const DirectoryCreateOptions& options) noexcept
    {
        return CreateDirectoriesNative(path, options);
    }

    ResultVoid LocalFileSystem::CreateSymlink(const Path& target, const Path& linkPath) noexcept
    {
        const auto targetNative   = target.ToNative();
        const auto linkPathNative = ToNativePath(linkPath);
        if (::symlink(targetNative.CStr(), linkPathNative.CStr()) == 0)
            return {};
        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "symlink failed", linkPath, target)));
    }

    ResultVoid LocalFileSystem::CreateHardLink(const Path& target, const Path& linkPath) noexcept
    {
        if (::link(ToNativePath(target).CStr(), ToNativePath(linkPath).CStr()) == 0)
            return {};
        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "link failed", linkPath, target)));
    }

    ResultVoid LocalFileSystem::SetPermissions(const Path& path, const FilePermissions& permissions, const SymlinkMode symlinkMode) noexcept
    {
        const mode_t mode       = ToModeBits(permissions);
        const auto   nativePath = ToNativePath(path);

#if defined(AT_SYMLINK_NOFOLLOW)
        if (symlinkMode == SymlinkMode::DoNotFollow)
        {
            if (::fchmodat(AT_FDCWD, nativePath.CStr(), mode, AT_SYMLINK_NOFOLLOW) == 0)
                return {};
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "fchmodat failed", path)));
        }
#endif

        if (symlinkMode == SymlinkMode::DoNotFollow)
        {
            return ResultVoid(
                    NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::Unsupported, "symlink no-follow chmod is unsupported on this platform", path)));
        }

        if (::chmod(nativePath.CStr(), mode) == 0)
            return {};
        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "chmod failed", path)));
    }

    ResultVoid LocalFileSystem::RemoveFile(const Path& path, const RemoveOptions& options) noexcept
    {
        return RemoveFileNative(path, options);
    }

    ResultVoid LocalFileSystem::RemoveDirectory(const Path& path, const RemoveOptions& options) noexcept
    {
        return RemoveDirectoryNative(path, options);
    }

    Result<UInt64> LocalFileSystem::RemoveAll(const Path& path, const RemoveOptions& options) noexcept
    {
        return RemoveAllNative(path, options);
    }

    ResultVoid LocalFileSystem::Rename(const Path& from, const Path& to) noexcept
    {
        if (::rename(ToNativePath(from).CStr(), ToNativePath(to).CStr()) == 0)
            return {};
        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "rename failed", from, to)));
    }

    ResultVoid LocalFileSystem::ReplaceFile(const Path& source, const Path& destination) noexcept
    {
        return Rename(source, destination);
    }

    ResultVoid LocalFileSystem::CopyFile(const Path& from, const Path& to, const CopyOptions& options) noexcept
    {
        return CopyPathNative(from, to, options);
    }

    Result<FileHandle> LocalFileSystem::OpenFile(const Path& path, const FileOpenOptions& options) noexcept
    {
        auto opened = LocalFileHandle::Open(path, options);
        if (!opened.HasValue())
            return Result<FileHandle>(NGIN::Utilities::Unexpected<IOError>(std::move(opened.Error())));
        return Result<FileHandle>(FileHandle(std::move(opened).TakeValue()));
    }

    Result<DirectoryHandle> LocalFileSystem::OpenDirectory(const Path& path) noexcept
    {
        auto opened = LocalDirectoryHandle::Open(path);
        if (!opened.HasValue())
            return Result<DirectoryHandle>(NGIN::Utilities::Unexpected<IOError>(std::move(opened.Error())));
        return Result<DirectoryHandle>(DirectoryHandle(std::move(opened).TakeValue()));
    }

    Result<DirectoryEnumerator> LocalFileSystem::Enumerate(const Path& path, const EnumerateOptions& options) noexcept
    {
        auto entries = EnumerateEntries(path, options);
        if (!entries.HasValue())
            return Result<DirectoryEnumerator>(NGIN::Utilities::Unexpected<IOError>(std::move(entries.Error())));

        try
        {
            return Result<DirectoryEnumerator>(DirectoryEnumerator(std::unique_ptr<IDirectoryEnumerator>(new VectorDirectoryEnumerator(std::move(entries.Value())))));
        } catch (const std::bad_alloc&)
        {
            return Result<DirectoryEnumerator>(
                    NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "allocation failed", path)));
        }
    }

    Result<Path> LocalFileSystem::CurrentWorkingDirectory() noexcept
    {
        std::vector<char> buffer(256, '\0');
        for (;;)
        {
            if (::getcwd(buffer.data(), buffer.size()) != nullptr)
                return Result<Path>(FromNativePath(std::string_view(buffer.data())));
            if (errno != ERANGE)
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "getcwd failed")));
            buffer.resize(buffer.size() * 2);
        }
    }

    ResultVoid LocalFileSystem::SetCurrentWorkingDirectory(const Path& path) noexcept
    {
        if (::chdir(ToNativePath(path).CStr()) == 0)
            return {};
        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "chdir failed", path)));
    }

    Result<Path> LocalFileSystem::TempDirectory() noexcept
    {
        const char* tempDir = std::getenv("TMPDIR");
        if (tempDir == nullptr || *tempDir == '\0')
            tempDir = "/tmp";
        return Result<Path>(FromNativePath(std::string_view(tempDir)));
    }

    Result<Path> LocalFileSystem::CreateTempDirectory(const Path& directory, std::string_view prefix) noexcept
    {
        Path baseDirectory = directory;
        if (baseDirectory.IsEmpty())
        {
            auto tempDirectory = TempDirectory();
            if (!tempDirectory.HasValue())
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(tempDirectory.Error())));
            baseDirectory = std::move(tempDirectory.Value());
        }

        std::string nativeTemplate = ToNativePath(baseDirectory.Join(std::string(prefix) + "XXXXXX")).CStr();
        nativeTemplate.push_back('\0');
        char* created = ::mkdtemp(nativeTemplate.data());
        if (created == nullptr)
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "mkdtemp failed", baseDirectory)));
        return Result<Path>(FromNativePath(std::string_view(created)));
    }

    Result<Path> LocalFileSystem::CreateTempFile(const Path& directory, std::string_view prefix) noexcept
    {
        Path baseDirectory = directory;
        if (baseDirectory.IsEmpty())
        {
            auto tempDirectory = TempDirectory();
            if (!tempDirectory.HasValue())
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(tempDirectory.Error())));
            baseDirectory = std::move(tempDirectory.Value());
        }

        std::string nativeTemplate = ToNativePath(baseDirectory.Join(std::string(prefix) + "XXXXXX")).CStr();
        nativeTemplate.push_back('\0');
        const int fd = ::mkstemp(nativeTemplate.data());
        if (fd < 0)
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "mkstemp failed", baseDirectory)));
        (void) ::close(fd);
        return Result<Path>(FromNativePath(std::string_view(nativeTemplate.data())));
    }

    Result<SpaceInfo> LocalFileSystem::GetSpaceInfo(const Path& path) noexcept
    {
        struct statvfs stats {};
        if (::statvfs(ToNativePath(path).CStr(), &stats) != 0)
            return Result<SpaceInfo>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "statvfs failed", path)));

        SpaceInfo info;
        info.capacity  = static_cast<UInt64>(stats.f_blocks) * static_cast<UInt64>(stats.f_frsize);
        info.free      = static_cast<UInt64>(stats.f_bfree) * static_cast<UInt64>(stats.f_frsize);
        info.available = static_cast<UInt64>(stats.f_bavail) * static_cast<UInt64>(stats.f_frsize);
        return Result<SpaceInfo>(info);
    }

    AsyncTask<std::unique_ptr<IAsyncFileHandle>> LocalFileSystem::OpenFileAsync(
            NGIN::Async::TaskContext& ctx, const Path& path, const FileOpenOptions& options)
    {
        co_await ctx.YieldNow();

        auto opened = LocalFileHandle::Open(path, options);
        if (!opened.HasValue())
        {
            co_return NGIN::Utilities::Unexpected<IOError>(std::move(opened.Error()));
        }

        std::unique_ptr<IAsyncFileHandle> result(opened.Value().release());
        co_return std::move(result);
    }
}// namespace NGIN::IO
