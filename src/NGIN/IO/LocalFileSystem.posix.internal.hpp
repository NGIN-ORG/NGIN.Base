#pragma once

#include <NGIN/IO/AsyncFileHandle.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>

#include <cerrno>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>
#include <unistd.h>

namespace NGIN::IO::detail
{
    struct OpenedAsyncPosixFile final
    {
        int  fd {-1};
        Path path {};
        bool canRead {false};
        bool canWrite {false};
    };

    struct LocalAsyncFileState final
    {
        std::shared_ptr<FileSystemDriver> driver {};
        Path                              path {};
        bool                              canRead {false};
        bool                              canWrite {false};
        int                               fd {-1};
        mutable std::mutex                mutex {};
    };

    [[nodiscard]] inline IOErrorCode MapErrnoCode(const int code) noexcept
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

    [[nodiscard]] inline IOError MakeErrnoError(
            const int code, std::string_view message, const Path& path = {}, const Path& secondary = {}) noexcept
    {
        IOError error;
        error.code          = MapErrnoCode(code);
        error.systemCode    = code;
        error.path          = path;
        error.secondaryPath = secondary;
        error.message       = message;
        return error;
    }

    [[nodiscard]] inline IOError MakeInlineError(
            IOErrorCode code, std::string_view message, const Path& path = {}, const Path& secondary = {}) noexcept
    {
        IOError error;
        error.code          = code;
        error.path          = path;
        error.secondaryPath = secondary;
        error.message       = message;
        return error;
    }

    [[nodiscard]] inline Result<Path> NormalizeRelativeHandlePath(const Path& path) noexcept
    {
        Path normalized = path.LexicallyNormal();
        if (normalized.IsAbsolute())
        {
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(
                    MakeInlineError(IOErrorCode::InvalidPath, "directory handle path must be relative", path)));
        }

        if (normalized.IsEmpty())
        {
            return Result<Path>(Path {"."});
        }
        if (normalized.StartsWith(Path {".."}))
        {
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(
                    MakeInlineError(IOErrorCode::InvalidPath, "directory handle path escapes handle root", path)));
        }

        return Result<Path>(std::move(normalized));
    }

    [[nodiscard]] inline Path JoinHandlePath(const Path& base, const Path& relativePath)
    {
        if (relativePath.View() == ".")
        {
            return base;
        }
        Path joined = base.Join(relativePath.View());
        joined.Normalize();
        return joined;
    }

    [[nodiscard]] inline int BuildOpenFlags(const FileOpenOptions& options) noexcept
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

        if ((options.flags & FileOpenFlags::WriteThrough) == FileOpenFlags::WriteThrough)
            openFlags |= O_SYNC;
#if defined(O_CLOEXEC)
        openFlags |= O_CLOEXEC;
#endif
        return openFlags;
    }

    [[nodiscard]] inline Result<OpenedAsyncPosixFile> OpenAsyncPosixFile(
            const Path& path, const FileOpenOptions& options) noexcept
    {
        OpenedAsyncPosixFile opened;
        opened.path     = path;
        opened.canRead  = options.access == FileAccess::Read || options.access == FileAccess::ReadWrite;
        opened.canWrite = options.access == FileAccess::Write || options.access == FileAccess::ReadWrite || options.access == FileAccess::Append;
        const auto nativePath = path.ToNative();
        opened.fd             = ::open(nativePath.CStr(), BuildOpenFlags(options), 0666);
        if (opened.fd < 0)
        {
            return Result<OpenedAsyncPosixFile>(
                    NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "open failed", path)));
        }
        return Result<OpenedAsyncPosixFile>(std::move(opened));
    }

    [[nodiscard]] inline Result<UIntSize> LocalAsyncFileReadSync(
            LocalAsyncFileState& state, std::span<NGIN::Byte> destination) noexcept
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        if (state.fd < 0)
            return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(
                    MakeInlineError(IOErrorCode::InvalidArgument, "file not open", state.path)));
        if (!state.canRead)
            return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(
                    MakeInlineError(IOErrorCode::NotSupported, "file not opened for read", state.path)));
        if (destination.empty())
            return Result<UIntSize>(UIntSize {0});

        for (;;)
        {
            const auto bytesRead = ::read(state.fd, destination.data(), destination.size());
            if (bytesRead >= 0)
                return Result<UIntSize>(static_cast<UIntSize>(bytesRead));
            if (errno == EINTR)
                continue;
            return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "read failed", state.path)));
        }
    }

    [[nodiscard]] inline Result<UIntSize> LocalAsyncFileWriteSync(
            LocalAsyncFileState& state, std::span<const NGIN::Byte> source) noexcept
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        if (state.fd < 0)
            return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(
                    MakeInlineError(IOErrorCode::InvalidArgument, "file not open", state.path)));
        if (!state.canWrite)
            return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(
                    MakeInlineError(IOErrorCode::NotSupported, "file not opened for write", state.path)));
        if (source.empty())
            return Result<UIntSize>(UIntSize {0});

        for (;;)
        {
            const auto bytesWritten = ::write(state.fd, source.data(), source.size());
            if (bytesWritten >= 0)
                return Result<UIntSize>(static_cast<UIntSize>(bytesWritten));
            if (errno == EINTR)
                continue;
            return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "write failed", state.path)));
        }
    }

    [[nodiscard]] inline Result<UIntSize> LocalAsyncFileReadAtSync(
            LocalAsyncFileState& state, UInt64 offset, std::span<NGIN::Byte> destination) noexcept
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        if (state.fd < 0)
            return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(
                    MakeInlineError(IOErrorCode::InvalidArgument, "file not open", state.path)));
        if (!state.canRead)
            return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(
                    MakeInlineError(IOErrorCode::NotSupported, "file not opened for read", state.path)));
        if (destination.empty())
            return Result<UIntSize>(UIntSize {0});

        for (;;)
        {
            const auto bytesRead = ::pread(state.fd, destination.data(), destination.size(), static_cast<off_t>(offset));
            if (bytesRead >= 0)
                return Result<UIntSize>(static_cast<UIntSize>(bytesRead));
            if (errno == EINTR)
                continue;
            return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "pread failed", state.path)));
        }
    }

    [[nodiscard]] inline Result<UIntSize> LocalAsyncFileWriteAtSync(
            LocalAsyncFileState& state, UInt64 offset, std::span<const NGIN::Byte> source) noexcept
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        if (state.fd < 0)
            return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(
                    MakeInlineError(IOErrorCode::InvalidArgument, "file not open", state.path)));
        if (!state.canWrite)
            return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(
                    MakeInlineError(IOErrorCode::NotSupported, "file not opened for write", state.path)));
        if (source.empty())
            return Result<UIntSize>(UIntSize {0});

        for (;;)
        {
            const auto bytesWritten = ::pwrite(state.fd, source.data(), source.size(), static_cast<off_t>(offset));
            if (bytesWritten >= 0)
                return Result<UIntSize>(static_cast<UIntSize>(bytesWritten));
            if (errno == EINTR)
                continue;
            return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "pwrite failed", state.path)));
        }
    }

    [[nodiscard]] inline ResultVoid LocalAsyncFileFlushSync(LocalAsyncFileState& state) noexcept
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        if (state.fd < 0)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(
                    MakeInlineError(IOErrorCode::InvalidArgument, "file not open", state.path)));
        if (::fsync(state.fd) != 0)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeErrnoError(errno, "fsync failed", state.path)));
        return {};
    }

    [[nodiscard]] inline ResultVoid LocalAsyncFileCloseSync(LocalAsyncFileState& state) noexcept
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        if (state.fd >= 0)
        {
            (void)::close(state.fd);
            state.fd = -1;
        }
        return {};
    }

    [[nodiscard]] AsyncFileHandle MakeAsyncPosixFileHandle(
            std::shared_ptr<FileSystemDriver> driver, OpenedAsyncPosixFile opened);
}
