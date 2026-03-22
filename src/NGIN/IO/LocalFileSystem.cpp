#include <NGIN/IO/LocalFileSystem.hpp>

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/IO/FileSystemUtilities.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace NGIN::IO
{
    namespace fs = std::filesystem;

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

        [[nodiscard]] IOErrorCode MapErrorCode(const std::error_code& ec) noexcept
        {
            if (!ec)
                return IOErrorCode::None;
            if (ec == std::errc::no_such_file_or_directory)
                return IOErrorCode::NotFound;
            if (ec == std::errc::file_exists)
                return IOErrorCode::AlreadyExists;
            if (ec == std::errc::permission_denied)
                return IOErrorCode::PermissionDenied;
            if (ec == std::errc::is_a_directory)
                return IOErrorCode::IsDirectory;
            if (ec == std::errc::not_a_directory)
                return IOErrorCode::NotDirectory;
            if (ec == std::errc::directory_not_empty)
                return IOErrorCode::DirectoryNotEmpty;
            if (ec == std::errc::filename_too_long)
                return IOErrorCode::PathTooLong;
            if (ec == std::errc::cross_device_link)
                return IOErrorCode::CrossDevice;
            if (ec == std::errc::device_or_resource_busy)
                return IOErrorCode::Busy;
            return IOErrorCode::SystemError;
        }

        [[nodiscard]] IOError MakeSystemError(const std::error_code& ec, std::string_view message,
                                              const Path& path = {}, const Path& secondary = {}) noexcept
        {
            IOError error;
            error.code          = MapErrorCode(ec);
            error.systemCode    = ec.value();
            error.path          = path;
            error.secondaryPath = secondary;
            error.message       = message;
            return error;
        }

        [[nodiscard]] fs::path ToFsPath(const Path& path)
        {
            const auto native = path.ToNative();
            return fs::path(std::string(native.Data(), native.Size()));
        }

        [[nodiscard]] Path FromFsPath(const fs::path& path)
        {
            const std::string value = path.generic_string();
            Path out {value};
            out.Normalize();
            return out;
        }

#if defined(_WIN32)
        [[nodiscard]] FilePermissions ToPermissions(fs::perms p) noexcept
        {
            FilePermissions out;
            out.nativeBits  = static_cast<UInt32>(p);
            out.readable    = (p & (fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read)) != fs::perms::none;
            out.writable    = (p & (fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write)) != fs::perms::none;
            out.executable  = (p & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) != fs::perms::none;
            return out;
        }
#endif

#if !defined(_WIN32)
        [[nodiscard]] FilePermissions ToPermissions(const mode_t mode) noexcept
        {
            FilePermissions out;
            out.nativeBits  = static_cast<UInt32>(mode);
            out.readable    = (mode & (S_IRUSR | S_IRGRP | S_IROTH)) != 0;
            out.writable    = (mode & (S_IWUSR | S_IWGRP | S_IWOTH)) != 0;
            out.executable  = (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
            out.setUserId   = (mode & S_ISUID) != 0;
            out.setGroupId  = (mode & S_ISGID) != 0;
            out.sticky      = (mode & S_ISVTX) != 0;
            return out;
        }
#endif

#if defined(_WIN32)
        [[nodiscard]] FileTime ToFileTime(const fs::file_time_type& value) noexcept
        {
            FileTime out;
            try
            {
                const auto ns       = std::chrono::duration_cast<std::chrono::nanoseconds>(value.time_since_epoch()).count();
                out.unixNanoseconds = static_cast<Int64>(ns);
                out.valid           = true;
            }
            catch (...)
            {
                out.valid = false;
            }
            return out;
        }
#endif

#if !defined(_WIN32)
        [[nodiscard]] FileTime ToFileTime(const struct timespec& value) noexcept
        {
            FileTime out;
            out.unixNanoseconds = static_cast<Int64>(value.tv_sec) * 1000000000LL + static_cast<Int64>(value.tv_nsec);
            out.valid           = true;
            return out;
        }
#endif

        [[nodiscard]] EntryType ToEntryType(fs::file_type type) noexcept
        {
            switch (type)
            {
                case fs::file_type::regular:
                    return EntryType::File;
                case fs::file_type::directory:
                    return EntryType::Directory;
                case fs::file_type::symlink:
                    return EntryType::Symlink;
                case fs::file_type::block:
                    return EntryType::BlockDevice;
                case fs::file_type::character:
                    return EntryType::CharacterDevice;
                case fs::file_type::fifo:
                    return EntryType::Fifo;
                case fs::file_type::socket:
                    return EntryType::Socket;
                case fs::file_type::none:
                case fs::file_type::not_found:
                    return EntryType::None;
                default:
                    return EntryType::Other;
            }
        }

#if !defined(_WIN32)
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

        [[nodiscard]] bool TargetExists(const NGIN::Text::String& nativePath) noexcept
        {
            struct stat st {};
            return ::stat(nativePath.CStr(), &st) == 0;
        }
#endif

        [[nodiscard]] Result<FileInfo> BuildFileInfo(
                const Path& path, [[maybe_unused]] const fs::path& nativePath, const MetadataOptions& options) noexcept
        {
#if !defined(_WIN32)
            const auto nativeString = path.ToNative();
            FileInfo   info;
            info.path = path;

            struct stat st {};
            const int   rc = options.symlinkMode == SymlinkMode::Follow
                                   ? ::stat(nativeString.CStr(), &st)
                                   : ::lstat(nativeString.CStr(), &st);
            if (rc != 0)
            {
                if ((errno == ENOENT || errno == ENOTDIR) && options.symlinkMode == SymlinkMode::Follow)
                {
                    struct stat linkStat {};
                    if (::lstat(nativeString.CStr(), &linkStat) == 0 && S_ISLNK(linkStat.st_mode))
                    {
                        info.exists             = true;
                        info.type               = EntryType::Symlink;
                        info.permissions        = ToPermissions(linkStat.st_mode);
                        info.ownership.userId   = static_cast<UInt32>(linkStat.st_uid);
                        info.ownership.groupId  = static_cast<UInt32>(linkStat.st_gid);
                        info.ownership.valid    = true;
                        info.identity.device    = static_cast<UInt64>(linkStat.st_dev);
                        info.identity.inode     = static_cast<UInt64>(linkStat.st_ino);
                        info.identity.hardLinkCount = static_cast<UInt64>(linkStat.st_nlink);
                        info.identity.valid     = true;
                        info.accessed           = ToAccessTime(linkStat);
                        info.modified           = ToModifyTime(linkStat);
                        info.changed            = ToChangeTime(linkStat);
                        info.symlinkTargetExists = false;
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
                        NGIN::Utilities::Unexpected<IOError>(MakeSystemError(std::error_code(errno, std::generic_category()), "stat failed", path)));
            }

            info.exists              = true;
            info.type                = ToEntryType(st.st_mode);
            info.size                = IsSizedEntryType(info.type) ? static_cast<UInt64>(st.st_size) : 0;
            info.permissions         = ToPermissions(st.st_mode);
            info.ownership.userId    = static_cast<UInt32>(st.st_uid);
            info.ownership.groupId   = static_cast<UInt32>(st.st_gid);
            info.ownership.valid     = true;
            info.identity.device     = static_cast<UInt64>(st.st_dev);
            info.identity.inode      = static_cast<UInt64>(st.st_ino);
            info.identity.hardLinkCount = static_cast<UInt64>(st.st_nlink);
            info.identity.valid      = true;
            info.accessed            = ToAccessTime(st);
            info.modified            = ToModifyTime(st);
            info.changed             = ToChangeTime(st);
            info.symlinkTargetExists = true;

            if (options.symlinkMode == SymlinkMode::DoNotFollow && info.type == EntryType::Symlink)
            {
                info.symlinkTargetExists = TargetExists(nativeString);
            }

            return Result<FileInfo>(std::move(info));
#else
            try
            {
                std::error_code ec;
                FileInfo info;
                info.path = path;
                const auto status =
                        options.symlinkMode == SymlinkMode::Follow ? fs::status(nativePath, ec) : fs::symlink_status(nativePath, ec);
                if (ec)
                {
                    if (ec == std::errc::no_such_file_or_directory)
                    {
                        info.exists = false;
                        info.type = EntryType::None;
                        return Result<FileInfo>(std::move(info));
                    }
                    return Result<FileInfo>(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "symlink_status failed", path)));
                }

                info.exists = fs::exists(status);
                info.type = ToEntryType(status.type());
                info.permissions = ToPermissions(status.permissions());
                info.symlinkTargetExists = info.exists;

                if (info.type == EntryType::File)
                {
                    info.size = static_cast<UInt64>(fs::file_size(nativePath, ec));
                    if (ec)
                    {
                        return Result<FileInfo>(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "file_size failed", path)));
                    }
                }

                ec.clear();
                const auto modified = fs::last_write_time(nativePath, ec);
                if (!ec)
                {
                    info.modified = ToFileTime(modified);
                }
                return Result<FileInfo>(std::move(info));
            }
            catch (const std::exception&)
            {
                return Result<FileInfo>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "filesystem exception", path)));
            }
#endif
        }

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
                                NGIN::Utilities::Unexpected<IOError>(std::move(result.ErrorUnsafe())));
                    }
                    return Result<std::unique_ptr<LocalFileHandle>>(std::move(handle));
                }
                catch (const std::bad_alloc&)
                {
                    return Result<std::unique_ptr<LocalFileHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "allocation failed", path)));
                }
                catch (const std::exception&)
                {
                    return Result<std::unique_ptr<LocalFileHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "file open exception", path)));
                }
            }

            Result<UIntSize> Read(std::span<NGIN::Byte> destination) noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!m_stream.is_open())
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));
                if (!m_canRead)
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotSupported, "file not opened for read", m_path)));
                if (destination.empty())
                    return Result<UIntSize>(UIntSize {0});

                try
                {
                    m_stream.clear();
                    m_stream.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(destination.size()));
                    const auto count = static_cast<UIntSize>(m_stream.gcount());
                    if (m_stream.bad())
                    {
                        return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "read failed", m_path)));
                    }
                    return Result<UIntSize>(count);
                }
                catch (const std::exception&)
                {
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "read exception", m_path)));
                }
            }

            Result<UIntSize> Write(std::span<const NGIN::Byte> source) noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!m_stream.is_open())
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));
                if (!m_canWrite)
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotSupported, "file not opened for write", m_path)));
                if (source.empty())
                    return Result<UIntSize>(UIntSize {0});
                try
                {
                    m_stream.clear();
                    m_stream.write(reinterpret_cast<const char*>(source.data()), static_cast<std::streamsize>(source.size()));
                    if (!m_stream)
                    {
                        return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "write failed", m_path)));
                    }
                    return Result<UIntSize>(static_cast<UIntSize>(source.size()));
                }
                catch (const std::exception&)
                {
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "write exception", m_path)));
                }
            }

            Result<UIntSize> ReadAt(UInt64 offset, std::span<NGIN::Byte> destination) noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!m_stream.is_open())
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));
                if (!m_canRead)
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotSupported, "file not opened for read", m_path)));
                if (destination.empty())
                    return Result<UIntSize>(UIntSize {0});
                try
                {
                    std::streampos oldGet = m_canRead ? m_stream.tellg() : std::streampos(-1);
                    if (m_canRead)
                    {
                        m_stream.clear();
                        m_stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
                    }
                    m_stream.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(destination.size()));
                    const auto count = static_cast<UIntSize>(m_stream.gcount());
                    if (oldGet != std::streampos(-1))
                    {
                        m_stream.clear();
                        m_stream.seekg(oldGet);
                    }
                    if (m_stream.bad())
                    {
                        return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "readat failed", m_path)));
                    }
                    return Result<UIntSize>(count);
                }
                catch (const std::exception&)
                {
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "readat exception", m_path)));
                }
            }

            Result<UIntSize> WriteAt(UInt64 offset, std::span<const NGIN::Byte> source) noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!m_stream.is_open())
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));
                if (!m_canWrite)
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotSupported, "file not opened for write", m_path)));
                if (source.empty())
                    return Result<UIntSize>(UIntSize {0});
                try
                {
                    std::streampos oldPut = m_canWrite ? m_stream.tellp() : std::streampos(-1);
                    m_stream.clear();
                    m_stream.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
                    m_stream.write(reinterpret_cast<const char*>(source.data()), static_cast<std::streamsize>(source.size()));
                    if (oldPut != std::streampos(-1))
                    {
                        m_stream.clear();
                        m_stream.seekp(oldPut);
                    }
                    if (!m_stream)
                    {
                        return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "writeat failed", m_path)));
                    }
                    return Result<UIntSize>(static_cast<UIntSize>(source.size()));
                }
                catch (const std::exception&)
                {
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "writeat exception", m_path)));
                }
            }

            ResultVoid Flush() noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!m_stream.is_open())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));
                try
                {
                    m_stream.flush();
                    if (!m_stream)
                        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "flush failed", m_path)));
                    return {};
                }
                catch (const std::exception&)
                {
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "flush exception", m_path)));
                }
            }

            ResultVoid Seek(Int64 offset, SeekOrigin origin) noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!m_stream.is_open())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));
                try
                {
                    const auto dir = origin == SeekOrigin::Begin ? std::ios::beg : (origin == SeekOrigin::Current ? std::ios::cur : std::ios::end);
                    m_stream.clear();
                    if (m_canRead)
                        m_stream.seekg(static_cast<std::streamoff>(offset), dir);
                    if (m_canWrite)
                        m_stream.seekp(static_cast<std::streamoff>(offset), dir);
                    if (!m_stream)
                        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "seek failed", m_path)));
                    return {};
                }
                catch (const std::exception&)
                {
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "seek exception", m_path)));
                }
            }

            Result<UInt64> Tell() const noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!m_stream.is_open())
                    return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));
                try
                {
                    auto& stream = const_cast<std::fstream&>(m_stream);
                    stream.clear();
                    std::streampos pos = std::streampos(-1);
                    if (m_canRead)
                        pos = stream.tellg();
                    if (pos == std::streampos(-1) && m_canWrite)
                        pos = stream.tellp();
                    if (pos == std::streampos(-1))
                        return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "tell failed", m_path)));
                    return Result<UInt64>(static_cast<UInt64>(static_cast<std::streamoff>(pos)));
                }
                catch (const std::exception&)
                {
                    return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "tell exception", m_path)));
                }
            }

            Result<UInt64> Size() const noexcept override
            {
                try
                {
                    std::error_code ec;
                    const auto size = fs::file_size(m_nativePath, ec);
                    if (ec)
                        return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "file_size failed", m_path)));
                    return Result<UInt64>(static_cast<UInt64>(size));
                }
                catch (const std::exception&)
                {
                    return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "size exception", m_path)));
                }
            }

            ResultVoid SetSize(UInt64 size) noexcept override
            {
                try
                {
                    std::error_code ec;
                    fs::resize_file(m_nativePath, static_cast<std::uintmax_t>(size), ec);
                    if (ec)
                        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "resize_file failed", m_path)));
                    return {};
                }
                catch (const std::exception&)
                {
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "setsize exception", m_path)));
                }
            }

            void Close() noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                try
                {
                    if (m_stream.is_open())
                        m_stream.close();
                }
                catch (...)
                {
                }
            }

            [[nodiscard]] bool IsOpen() const noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                return m_stream.is_open();
            }

            AsyncTask<UIntSize> ReadAsync(NGIN::Async::TaskContext& ctx, std::span<NGIN::Byte> destination) override
            {
                auto yielded = co_await ctx.YieldNow();
                if (!yielded)
                {
                    co_await AsyncTask<UIntSize>::ReturnError(yielded.error());
                    co_return AsyncResult<UIntSize>(UIntSize {0});
                }
                co_return ToAsyncResult(Read(destination));
            }

            AsyncTask<UIntSize> WriteAsync(NGIN::Async::TaskContext& ctx, std::span<const NGIN::Byte> source) override
            {
                auto yielded = co_await ctx.YieldNow();
                if (!yielded)
                {
                    co_await AsyncTask<UIntSize>::ReturnError(yielded.error());
                    co_return AsyncResult<UIntSize>(UIntSize {0});
                }
                co_return ToAsyncResult(Write(source));
            }

            AsyncTask<UIntSize> ReadAtAsync(NGIN::Async::TaskContext& ctx, UInt64 offset, std::span<NGIN::Byte> destination) override
            {
                auto yielded = co_await ctx.YieldNow();
                if (!yielded)
                {
                    co_await AsyncTask<UIntSize>::ReturnError(yielded.error());
                    co_return AsyncResult<UIntSize>(UIntSize {0});
                }
                co_return ToAsyncResult(ReadAt(offset, destination));
            }

            AsyncTask<UIntSize> WriteAtAsync(NGIN::Async::TaskContext& ctx, UInt64 offset, std::span<const NGIN::Byte> source) override
            {
                auto yielded = co_await ctx.YieldNow();
                if (!yielded)
                {
                    co_await AsyncTask<UIntSize>::ReturnError(yielded.error());
                    co_return AsyncResult<UIntSize>(UIntSize {0});
                }
                co_return ToAsyncResult(WriteAt(offset, source));
            }

            AsyncTaskVoid FlushAsync(NGIN::Async::TaskContext& ctx) override
            {
                auto yielded = co_await ctx.YieldNow();
                if (!yielded)
                {
                    co_await AsyncTaskVoid::ReturnError(yielded.error());
                    co_return AsyncResult<void> {};
                }
                co_return ToAsyncResult(Flush());
            }

            AsyncTaskVoid CloseAsync(NGIN::Async::TaskContext& ctx) override
            {
                auto yielded = co_await ctx.YieldNow();
                if (!yielded)
                {
                    co_await AsyncTaskVoid::ReturnError(yielded.error());
                    co_return AsyncResult<void> {};
                }
                Close();
                co_return AsyncResult<void> {};
            }

        private:
            ResultVoid OpenImpl(const Path& path, const FileOpenOptions& options) noexcept
            {
                m_path = path;
                m_nativePath = ToFsPath(path);
                m_canRead = (options.access == FileAccess::Read || options.access == FileAccess::ReadWrite);
                m_canWrite = (options.access == FileAccess::Write || options.access == FileAccess::ReadWrite || options.access == FileAccess::Append);

                try
                {
                    std::error_code ec;
                    const bool exists = fs::exists(m_nativePath, ec);
                    if (ec)
                        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "exists failed", m_path)));

                    switch (options.disposition)
                    {
                        case FileCreateDisposition::OpenExisting:
                            if (!exists)
                                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "file not found", m_path)));
                            break;
                        case FileCreateDisposition::CreateNew:
                            if (exists)
                                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::AlreadyExists, "file exists", m_path)));
                            break;
                        case FileCreateDisposition::TruncateExisting:
                            if (!exists)
                                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "file not found", m_path)));
                            break;
                        case FileCreateDisposition::OpenAlways:
                        case FileCreateDisposition::CreateAlways:
                            break;
                    }

                    std::ios::openmode mode = std::ios::binary;
                    if (m_canRead)
                        mode |= std::ios::in;
                    if (m_canWrite)
                        mode |= std::ios::out;
                    if (options.access == FileAccess::Append)
                        mode |= std::ios::app;
                    if (options.disposition == FileCreateDisposition::CreateAlways || options.disposition == FileCreateDisposition::TruncateExisting)
                        mode |= std::ios::trunc;

                    if ((options.disposition == FileCreateDisposition::OpenAlways || options.disposition == FileCreateDisposition::CreateAlways ||
                         options.disposition == FileCreateDisposition::CreateNew) && !exists && m_canWrite)
                    {
                        std::ofstream touch(m_nativePath, std::ios::binary | std::ios::out);
                        touch.close();
                    }

                    m_stream.open(m_nativePath, mode);
                    if (!m_stream.is_open())
                        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "open failed", m_path)));
                    return {};
                }
                catch (const std::exception&)
                {
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "open exception", m_path)));
                }
            }

            mutable std::mutex m_mutex {};
            std::fstream       m_stream {};
            fs::path           m_nativePath {};
            Path               m_path {};
            bool               m_canRead {false};
            bool               m_canWrite {false};
        };

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
    }// namespace

    FileSystemCapabilities LocalFileSystem::GetCapabilities() const noexcept
    {
        FileSystemCapabilities capabilities;
        capabilities.symlinks          = true;
        capabilities.hardLinks         = true;
        capabilities.memoryMappedFiles = true;
        capabilities.metadataNoFollow  = true;

#if defined(_WIN32)
        capabilities.nanosecondTimestamps = true;
#else
        capabilities.blockDevices       = true;
        capabilities.characterDevices   = true;
        capabilities.fifos              = true;
        capabilities.sockets            = true;
        capabilities.posixModeBits      = true;
        capabilities.ownership          = true;
        capabilities.setIdBits          = true;
        capabilities.stickyBit          = true;
        capabilities.fileIdentity       = true;
        capabilities.hardLinkCount      = true;
        capabilities.nanosecondTimestamps = true;
#endif

        return capabilities;
    }

    Result<bool> LocalFileSystem::Exists(const Path& path) noexcept
    {
        try
        {
            std::error_code ec;
            const bool exists = fs::exists(ToFsPath(path), ec);
            if (ec)
                return Result<bool>(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "exists failed", path)));
            return Result<bool>(exists);
        }
        catch (const std::exception&)
        {
            return Result<bool>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "exists exception", path)));
        }
    }

    Result<FileInfo> LocalFileSystem::GetInfo(const Path& path, const MetadataOptions& options) noexcept
    {
        return BuildFileInfo(path, ToFsPath(path), options);
    }

    ResultVoid LocalFileSystem::CreateDirectory(const Path& path, const DirectoryCreateOptions& options) noexcept
    {
        try
        {
            std::error_code ec;
            const bool created = fs::create_directory(ToFsPath(path), ec);
            if (ec)
            {
                if (options.ignoreIfExists && ec == std::errc::file_exists)
                    return {};
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "create_directory failed", path)));
            }
            (void)created;
            return {};
        }
        catch (const std::exception&)
        {
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "create_directory exception", path)));
        }
    }

    ResultVoid LocalFileSystem::CreateDirectories(const Path& path, const DirectoryCreateOptions& options) noexcept
    {
        try
        {
            std::error_code ec;
            fs::create_directories(ToFsPath(path), ec);
            if (ec)
            {
                if (options.ignoreIfExists && ec == std::errc::file_exists)
                    return {};
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "create_directories failed", path)));
            }
            return {};
        }
        catch (const std::exception&)
        {
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "create_directories exception", path)));
        }
    }

    ResultVoid LocalFileSystem::RemoveFile(const Path& path, const RemoveOptions& options) noexcept
    {
        try
        {
            std::error_code ec;
            const bool removed = fs::remove(ToFsPath(path), ec);
            if (ec)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "remove file failed", path)));
            if (!removed && !options.ignoreMissing)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "file not found", path)));
            return {};
        }
        catch (const std::exception&)
        {
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "remove file exception", path)));
        }
    }

    ResultVoid LocalFileSystem::RemoveDirectory(const Path& path, const RemoveOptions& options) noexcept
    {
        if (options.recursive)
        {
            auto removed = RemoveAll(path, options);
            if (!removed.HasValue())
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(removed.ErrorUnsafe())));
            return {};
        }

        try
        {
            std::error_code ec;
            const bool removed = fs::remove(ToFsPath(path), ec);
            if (ec)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "remove directory failed", path)));
            if (!removed && !options.ignoreMissing)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "directory not found", path)));
            return {};
        }
        catch (const std::exception&)
        {
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "remove directory exception", path)));
        }
    }

    Result<UInt64> LocalFileSystem::RemoveAll(const Path& path, const RemoveOptions& options) noexcept
    {
        try
        {
            std::error_code ec;
            const auto removed = fs::remove_all(ToFsPath(path), ec);
            if (ec)
                return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "remove_all failed", path)));
            if (removed == 0 && !options.ignoreMissing)
                return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "path not found", path)));
            return Result<UInt64>(static_cast<UInt64>(removed));
        }
        catch (const std::exception&)
        {
            return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "remove_all exception", path)));
        }
    }

    ResultVoid LocalFileSystem::Rename(const Path& from, const Path& to) noexcept
    {
        try
        {
            std::error_code ec;
            fs::rename(ToFsPath(from), ToFsPath(to), ec);
            if (ec)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "rename failed", from, to)));
            return {};
        }
        catch (const std::exception&)
        {
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "rename exception", from, to)));
        }
    }

    ResultVoid LocalFileSystem::CopyFile(const Path& from, const Path& to, const CopyOptions& options) noexcept
    {
        try
        {
            std::error_code ec;
            const auto copyOptions = options.overwriteExisting ? fs::copy_options::overwrite_existing : fs::copy_options::none;
            if (options.recursive)
            {
                fs::copy(ToFsPath(from), ToFsPath(to), copyOptions | fs::copy_options::recursive, ec);
            }
            else
            {
                const bool ok = fs::copy_file(ToFsPath(from), ToFsPath(to), copyOptions, ec);
                if (!ok && !ec)
                {
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::AlreadyExists, "destination exists", to)));
                }
            }
            if (ec)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "copy failed", from, to)));
            return {};
        }
        catch (const std::exception&)
        {
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "copy exception", from, to)));
        }
    }

    ResultVoid LocalFileSystem::Move(const Path& from, const Path& to, const CopyOptions& options) noexcept
    {
        auto rename = Rename(from, to);
        if (rename.HasValue())
            return rename;
        const auto code = rename.ErrorUnsafe().code;
        if (code != IOErrorCode::CrossDevice)
            return rename;

        auto copy = CopyFile(from, to, options);
        if (!copy.HasValue())
            return copy;
        RemoveOptions removeOptions;
        removeOptions.recursive = options.recursive;
        removeOptions.ignoreMissing = false;
        if (options.recursive)
            return RemoveDirectory(from, removeOptions);
        return RemoveFile(from, removeOptions);
    }

    Result<std::unique_ptr<IFileHandle>> LocalFileSystem::OpenFile(const Path& path, const FileOpenOptions& options) noexcept
    {
        auto opened = LocalFileHandle::Open(path, options);
        if (!opened.HasValue())
        {
            return Result<std::unique_ptr<IFileHandle>>(
                    NGIN::Utilities::Unexpected<IOError>(std::move(opened.ErrorUnsafe())));
        }
        std::unique_ptr<IFileHandle> out(opened.ValueUnsafe().release());
        return Result<std::unique_ptr<IFileHandle>>(std::move(out));
    }

    Result<FileView> LocalFileSystem::OpenFileView(const Path& path) noexcept
    {
        FileView view;
        auto result = view.Open(path);
        if (!result.HasValue())
        {
            return Result<FileView>(NGIN::Utilities::Unexpected<IOError>(std::move(result.ErrorUnsafe())));
        }
        return Result<FileView>(std::move(view));
    }

    Result<std::unique_ptr<IDirectoryEnumerator>> LocalFileSystem::Enumerate(const Path& path, const EnumerateOptions& options) noexcept
    {
        try
        {
            std::error_code ec;
            std::vector<DirectoryEntry> entries;
            const auto native = ToFsPath(path);

            auto pushEntry = [&](const fs::directory_entry& e) -> bool {
                std::error_code statusEc;
                const auto status = options.followSymlinks ? e.status(statusEc) : e.symlink_status(statusEc);
                if (statusEc)
                {
                    ec = statusEc;
                    return false;
                }

                DirectoryEntry entry;
                    entry.path = FromFsPath(e.path());
                    entry.name = Path {e.path().filename().generic_string()};
                    entry.type = ToEntryType(status.type());
                    if (!IncludeEntry(entry, options))
                        return true;
                    if (options.populateInfo)
                    {
                        MetadataOptions metadataOptions;
                        metadataOptions.symlinkMode = options.followSymlinks ? SymlinkMode::Follow : SymlinkMode::DoNotFollow;
                        auto info = BuildFileInfo(entry.path, e.path(), metadataOptions);
                        if (!info.HasValue())
                        {
                            ec = std::error_code(info.ErrorUnsafe().systemCode, std::generic_category());
                            if (!ec)
                                ec = std::make_error_code(std::errc::io_error);
                            return false;
                        }
                        entry.info = std::move(info.ValueUnsafe());
                    }
                    entries.push_back(std::move(entry));
                    return true;
                };

            if (options.recursive)
            {
                auto directoryOptions = fs::directory_options::skip_permission_denied;
                if (options.followSymlinks)
                    directoryOptions |= fs::directory_options::follow_directory_symlink;
                for (fs::recursive_directory_iterator it(native, directoryOptions, ec), end; !ec && it != end; it.increment(ec))
                {
                    if (!pushEntry(*it))
                        break;
                }
            }
            else
            {
                for (fs::directory_iterator it(native, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
                {
                    if (!pushEntry(*it))
                        break;
                }
            }

            if (ec)
                return Result<std::unique_ptr<IDirectoryEnumerator>>(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "enumerate failed", path)));

            if (options.stableSort)
            {
                std::sort(entries.begin(), entries.end(), [](const DirectoryEntry& a, const DirectoryEntry& b) {
                    return a.path.View() < b.path.View();
                });
            }

            std::unique_ptr<IDirectoryEnumerator> out(new VectorDirectoryEnumerator(std::move(entries)));
            return Result<std::unique_ptr<IDirectoryEnumerator>>(std::move(out));
        }
        catch (const std::exception&)
        {
            return Result<std::unique_ptr<IDirectoryEnumerator>>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "enumerate exception", path)));
        }
    }

    Result<Path> LocalFileSystem::CurrentWorkingDirectory() noexcept
    {
        try
        {
            std::error_code ec;
            const auto cwd = fs::current_path(ec);
            if (ec)
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "current_path failed")));
            return Result<Path>(FromFsPath(cwd));
        }
        catch (const std::exception&)
        {
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "current_path exception")));
        }
    }

    ResultVoid LocalFileSystem::SetCurrentWorkingDirectory(const Path& path) noexcept
    {
        try
        {
            std::error_code ec;
            fs::current_path(ToFsPath(path), ec);
            if (ec)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "set current_path failed", path)));
            return {};
        }
        catch (const std::exception&)
        {
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "set current_path exception", path)));
        }
    }

    Result<Path> LocalFileSystem::TempDirectory() noexcept
    {
        try
        {
            std::error_code ec;
            const auto tmp = fs::temp_directory_path(ec);
            if (ec)
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "temp_directory_path failed")));
            return Result<Path>(FromFsPath(tmp));
        }
        catch (const std::exception&)
        {
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "temp_directory_path exception")));
        }
    }

    Result<SpaceInfo> LocalFileSystem::GetSpaceInfo(const Path& path) noexcept
    {
        try
        {
            std::error_code ec;
            const auto info = fs::space(ToFsPath(path), ec);
            if (ec)
                return Result<SpaceInfo>(NGIN::Utilities::Unexpected<IOError>(MakeSystemError(ec, "space failed", path)));
            SpaceInfo out;
            out.capacity = static_cast<UInt64>(info.capacity);
            out.free = static_cast<UInt64>(info.free);
            out.available = static_cast<UInt64>(info.available);
            return Result<SpaceInfo>(out);
        }
        catch (const std::exception&)
        {
            return Result<SpaceInfo>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "space exception", path)));
        }
    }

    AsyncTask<std::unique_ptr<IAsyncFileHandle>> LocalFileSystem::OpenFileAsync(
            NGIN::Async::TaskContext& ctx, const Path& path, const FileOpenOptions& options)
    {
        auto yielded = co_await ctx.YieldNow();
        if (!yielded)
        {
            co_await AsyncTask<std::unique_ptr<IAsyncFileHandle>>::ReturnError(yielded.error());
            co_return AsyncResult<std::unique_ptr<IAsyncFileHandle>>(std::unique_ptr<IAsyncFileHandle> {});
        }

        auto opened = LocalFileHandle::Open(path, options);
        if (!opened.HasValue())
        {
            co_return AsyncResult<std::unique_ptr<IAsyncFileHandle>>(NGIN::Utilities::Unexpected<IOError>(std::move(opened.ErrorUnsafe())));
        }
        std::unique_ptr<IAsyncFileHandle> out(opened.ValueUnsafe().release());
        co_return AsyncResult<std::unique_ptr<IAsyncFileHandle>>(std::move(out));
    }

    AsyncTask<FileInfo> LocalFileSystem::GetInfoAsync(
            NGIN::Async::TaskContext& ctx, const Path& path, const MetadataOptions& options)
    {
        auto yielded = co_await ctx.YieldNow();
        if (!yielded)
        {
            co_await AsyncTask<FileInfo>::ReturnError(yielded.error());
            co_return AsyncResult<FileInfo>(FileInfo {});
        }
        co_return ToAsyncResult(GetInfo(path, options));
    }

    AsyncTaskVoid LocalFileSystem::CopyFileAsync(NGIN::Async::TaskContext& ctx, const Path& from, const Path& to, const CopyOptions& options)
    {
        auto yielded = co_await ctx.YieldNow();
        if (!yielded)
        {
            co_await AsyncTaskVoid::ReturnError(yielded.error());
            co_return AsyncResult<void> {};
        }
        co_return ToAsyncResult(CopyFile(from, to, options));
    }
}// namespace NGIN::IO
