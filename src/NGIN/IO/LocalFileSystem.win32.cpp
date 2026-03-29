#include <NGIN/IO/LocalFileSystem.hpp>

#include "AsyncDispatch.hpp"
#include "NativeFileSystemBackend.hpp"

#define NOMINMAX
#include <windows.h>

#include <NGIN/Utilities/Expected.hpp>

#include <algorithm>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::IO
{
    namespace
    {
        using NativePath = std::wstring;

        [[nodiscard]] IOError MakeError(IOErrorCode code, std::string_view message, const Path& path = {}, const Path& secondary = {}) noexcept
        {
            IOError error;
            error.code          = code;
            error.path          = path;
            error.secondaryPath = secondary;
            error.message       = message;
            return error;
        }

        [[nodiscard]] IOErrorCode MapWindowsErrorCode(const DWORD code) noexcept
        {
            switch (code)
            {
                case ERROR_SUCCESS:
                    return IOErrorCode::None;
                case ERROR_FILE_NOT_FOUND:
                case ERROR_PATH_NOT_FOUND:
                case ERROR_INVALID_NAME:
                    return IOErrorCode::NotFound;
                case ERROR_ALREADY_EXISTS:
                case ERROR_FILE_EXISTS:
                    return IOErrorCode::AlreadyExists;
                case ERROR_ACCESS_DENIED:
                case ERROR_PRIVILEGE_NOT_HELD:
                    return IOErrorCode::PermissionDenied;
                case ERROR_DIRECTORY:
                    return IOErrorCode::NotDirectory;
                case ERROR_DIR_NOT_EMPTY:
                    return IOErrorCode::DirectoryNotEmpty;
                case ERROR_BUFFER_OVERFLOW:
                case ERROR_FILENAME_EXCED_RANGE:
                    return IOErrorCode::PathTooLong;
                case ERROR_NOT_SAME_DEVICE:
                    return IOErrorCode::CrossDevice;
                case ERROR_BUSY:
                case ERROR_SHARING_VIOLATION:
                case ERROR_LOCK_VIOLATION:
                    return IOErrorCode::Busy;
                default:
                    return IOErrorCode::SystemError;
            }
        }

        [[nodiscard]] IOError MakeWindowsError(const DWORD code, std::string_view message, const Path& path = {}, const Path& secondary = {}) noexcept
        {
            IOError error;
            error.code          = MapWindowsErrorCode(code);
            error.systemCode    = static_cast<int>(code);
            error.path          = path;
            error.secondaryPath = secondary;
            error.message       = message;
            return error;
        }

        [[nodiscard]] NativePath ToNativePath(const Path& path) noexcept
        {
            const auto utf8 = path.View();
            if (utf8.empty())
                return {};

            const int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
            if (wideLength <= 0)
                return {};

            NativePath result(static_cast<std::size_t>(wideLength), L'\0');
            (void) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), wideLength);
            for (auto& ch: result)
            {
                if (ch == L'/')
                    ch = L'\\';
            }
            return result;
        }

        [[nodiscard]] Path FromNativePath(std::wstring_view nativePath)
        {
            if (nativePath.empty())
                return {};

            const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, nativePath.data(), static_cast<int>(nativePath.size()), nullptr, 0, nullptr, nullptr);
            if (utf8Length <= 0)
                return {};

            std::string utf8(static_cast<std::size_t>(utf8Length), '\0');
            (void) WideCharToMultiByte(CP_UTF8, 0, nativePath.data(), static_cast<int>(nativePath.size()), utf8.data(), utf8Length, nullptr, nullptr);
            Path path {utf8};
            path.Normalize();
            return path;
        }

        [[nodiscard]] FileTime ToFileTime(const FILETIME& fileTime) noexcept
        {
            ULARGE_INTEGER value {};
            value.LowPart  = fileTime.dwLowDateTime;
            value.HighPart = fileTime.dwHighDateTime;

            static constexpr UInt64 WindowsEpochDifference100ns = 116444736000000000ULL;
            FileTime                out;
            if (value.QuadPart >= WindowsEpochDifference100ns)
            {
                out.unixNanoseconds = static_cast<Int64>((value.QuadPart - WindowsEpochDifference100ns) * 100ULL);
                out.valid           = true;
            }
            return out;
        }

        [[nodiscard]] FilePermissions ToPermissions(const DWORD attributes) noexcept
        {
            FilePermissions permissions;
            permissions.nativeBits = attributes;
            permissions.readable   = true;
            permissions.writable   = (attributes & FILE_ATTRIBUTE_READONLY) == 0;
            permissions.executable = true;
            return permissions;
        }

        [[nodiscard]] EntryType ToEntryType(const DWORD attributes) noexcept
        {
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                return EntryType::Symlink;
            if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                return EntryType::Directory;
            return EntryType::File;
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
            if (nativePath.empty())
            {
                FileInfo info;
                info.path = path;
                return Result<FileInfo>(std::move(info));
            }

            const DWORD flags = FILE_FLAG_BACKUP_SEMANTICS |
                                (options.symlinkMode == SymlinkMode::DoNotFollow ? FILE_FLAG_OPEN_REPARSE_POINT : 0);
            HANDLE handle = CreateFileW(
                    nativePath.c_str(),
                    FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr,
                    OPEN_EXISTING,
                    flags,
                    nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                const DWORD error = GetLastError();
                FileInfo    info;
                info.path = path;
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_INVALID_NAME)
                {
                    info.exists = false;
                    info.type   = EntryType::None;
                    return Result<FileInfo>(std::move(info));
                }
                return Result<FileInfo>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(error, "CreateFileW failed", path)));
            }

            FileInfo info;
            info.path = path;

            BY_HANDLE_FILE_INFORMATION byHandle {};
            if (!GetFileInformationByHandle(handle, &byHandle))
            {
                const DWORD error = GetLastError();
                CloseHandle(handle);
                return Result<FileInfo>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(error, "GetFileInformationByHandle failed", path)));
            }

            const DWORD attributes      = byHandle.dwFileAttributes;
            info.exists                 = true;
            info.type                   = ToEntryType(attributes);
            info.permissions            = ToPermissions(attributes);
            info.modified               = ToFileTime(byHandle.ftLastWriteTime);
            info.accessed               = ToFileTime(byHandle.ftLastAccessTime);
            info.created                = ToFileTime(byHandle.ftCreationTime);
            info.identity.device        = byHandle.dwVolumeSerialNumber;
            info.identity.inode         = (static_cast<UInt64>(byHandle.nFileIndexHigh) << 32u) | byHandle.nFileIndexLow;
            info.identity.hardLinkCount = byHandle.nNumberOfLinks;
            info.identity.valid         = true;
            info.symlinkTargetExists    = true;
            if (info.type == EntryType::File)
                info.size = (static_cast<UInt64>(byHandle.nFileSizeHigh) << 32u) | byHandle.nFileSizeLow;

            CloseHandle(handle);
            return Result<FileInfo>(std::move(info));
        }

        [[nodiscard]] Result<Path> MakeAbsolutePath(const Path& path, const Path& base) noexcept
        {
            Path input = path;
            if (!base.IsEmpty() && input.IsRelative())
            {
                input = base.Join(path.View());
                input.Normalize();
            }

            const auto nativeInput = ToNativePath(input);
            DWORD      required    = GetFullPathNameW(nativeInput.c_str(), 0, nullptr, nullptr);
            if (required == 0)
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "GetFullPathNameW failed", path)));

            std::wstring buffer(static_cast<std::size_t>(required), L'\0');
            const DWORD  count = GetFullPathNameW(nativeInput.c_str(), required, buffer.data(), nullptr);
            if (count == 0)
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "GetFullPathNameW failed", path)));
            if (!buffer.empty() && buffer.back() == L'\0')
                buffer.pop_back();
            return Result<Path>(FromNativePath(buffer));
        }

        [[nodiscard]] Result<Path> CanonicalizeExistingPath(const Path& path) noexcept
        {
            HANDLE handle = CreateFileW(
                    ToNativePath(path).c_str(),
                    0,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS,
                    nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "CreateFileW failed", path)));

            DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
            if (required == 0)
            {
                const DWORD error = GetLastError();
                CloseHandle(handle);
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(error, "GetFinalPathNameByHandleW failed", path)));
            }

            std::wstring buffer(static_cast<std::size_t>(required), L'\0');
            if (GetFinalPathNameByHandleW(handle, buffer.data(), required, FILE_NAME_NORMALIZED) == 0)
            {
                const DWORD error = GetLastError();
                CloseHandle(handle);
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(error, "GetFinalPathNameByHandleW failed", path)));
            }
            CloseHandle(handle);

            if (!buffer.empty() && buffer.back() == L'\0')
                buffer.pop_back();
            constexpr std::wstring_view verbatimPrefix = LR"(\\?\)";
            if (buffer.starts_with(verbatimPrefix))
                buffer.erase(0, verbatimPrefix.size());
            return Result<Path>(FromNativePath(buffer));
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

        class LocalFileHandle final : public IFileHandle
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

                DWORD bytesRead = 0;
                if (!ReadFile(m_handle, destination.data(), static_cast<DWORD>(destination.size()), &bytesRead, nullptr))
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "ReadFile failed", m_path)));
                return Result<UIntSize>(static_cast<UIntSize>(bytesRead));
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

                DWORD bytesWritten = 0;
                if (!WriteFile(m_handle, source.data(), static_cast<DWORD>(source.size()), &bytesWritten, nullptr))
                    return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "WriteFile failed", m_path)));
                return Result<UIntSize>(static_cast<UIntSize>(bytesWritten));
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

                OVERLAPPED overlapped {};
                overlapped.Offset     = static_cast<DWORD>(offset & 0xffffffffULL);
                overlapped.OffsetHigh = static_cast<DWORD>((offset >> 32u) & 0xffffffffULL);
                DWORD bytesRead       = 0;
                if (!ReadFile(m_handle, destination.data(), static_cast<DWORD>(destination.size()), &bytesRead, &overlapped))
                {
                    return Result<UIntSize>(
                            NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "ReadFile overlapped failed", m_path)));
                }
                return Result<UIntSize>(static_cast<UIntSize>(bytesRead));
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

                OVERLAPPED overlapped {};
                overlapped.Offset     = static_cast<DWORD>(offset & 0xffffffffULL);
                overlapped.OffsetHigh = static_cast<DWORD>((offset >> 32u) & 0xffffffffULL);
                DWORD bytesWritten    = 0;
                if (!WriteFile(m_handle, source.data(), static_cast<DWORD>(source.size()), &bytesWritten, &overlapped))
                {
                    return Result<UIntSize>(
                            NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "WriteFile overlapped failed", m_path)));
                }
                return Result<UIntSize>(static_cast<UIntSize>(bytesWritten));
            }

            ResultVoid Flush() noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!IsOpenUnlocked())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));
                if (!FlushFileBuffers(m_handle))
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "FlushFileBuffers failed", m_path)));
                return {};
            }

            ResultVoid Seek(Int64 offset, SeekOrigin origin) noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!IsOpenUnlocked())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));

                LARGE_INTEGER move {};
                move.QuadPart = offset;
                DWORD method  = FILE_BEGIN;
                if (origin == SeekOrigin::Current)
                    method = FILE_CURRENT;
                else if (origin == SeekOrigin::End)
                    method = FILE_END;

                LARGE_INTEGER ignored {};
                if (!SetFilePointerEx(m_handle, move, &ignored, method))
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "SetFilePointerEx failed", m_path)));
                return {};
            }

            Result<UInt64> Tell() const noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!IsOpenUnlocked())
                    return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));

                LARGE_INTEGER zero {};
                LARGE_INTEGER position {};
                if (!SetFilePointerEx(m_handle, zero, &position, FILE_CURRENT))
                    return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "SetFilePointerEx failed", m_path)));
                return Result<UInt64>(static_cast<UInt64>(position.QuadPart));
            }

            Result<UInt64> Size() const noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!IsOpenUnlocked())
                    return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));

                LARGE_INTEGER size {};
                if (!GetFileSizeEx(m_handle, &size))
                    return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "GetFileSizeEx failed", m_path)));
                return Result<UInt64>(static_cast<UInt64>(size.QuadPart));
            }

            ResultVoid SetSize(UInt64 size) noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (!IsOpenUnlocked())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", m_path)));

                LARGE_INTEGER position {};
                position.QuadPart = static_cast<LONGLONG>(size);
                if (!SetFilePointerEx(m_handle, position, nullptr, FILE_BEGIN))
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "SetFilePointerEx failed", m_path)));
                if (!SetEndOfFile(m_handle))
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "SetEndOfFile failed", m_path)));
                return {};
            }

            void Close() noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (m_handle != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(m_handle);
                    m_handle = INVALID_HANDLE_VALUE;
                }
            }

            [[nodiscard]] bool IsOpen() const noexcept override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                return IsOpenUnlocked();
            }

        private:
            [[nodiscard]] bool IsOpenUnlocked() const noexcept
            {
                return m_handle != INVALID_HANDLE_VALUE;
            }

            ResultVoid OpenImpl(const Path& path, const FileOpenOptions& options) noexcept
            {
                m_path     = path;
                m_canRead  = options.access == FileAccess::Read || options.access == FileAccess::ReadWrite;
                m_canWrite = options.access == FileAccess::Write || options.access == FileAccess::ReadWrite || options.access == FileAccess::Append;

                DWORD desiredAccess = 0;
                switch (options.access)
                {
                    case FileAccess::Read:
                        desiredAccess = GENERIC_READ;
                        break;
                    case FileAccess::Write:
                        desiredAccess = GENERIC_WRITE;
                        break;
                    case FileAccess::ReadWrite:
                        desiredAccess = GENERIC_READ | GENERIC_WRITE;
                        break;
                    case FileAccess::Append:
                        desiredAccess = FILE_APPEND_DATA;
                        break;
                }

                DWORD shareMode = 0;
                if ((options.share & FileShare::Read) == FileShare::Read)
                    shareMode |= FILE_SHARE_READ;
                if ((options.share & FileShare::Write) == FileShare::Write)
                    shareMode |= FILE_SHARE_WRITE;
                if ((options.share & FileShare::Delete) == FileShare::Delete)
                    shareMode |= FILE_SHARE_DELETE;

                DWORD creationDisposition = OPEN_EXISTING;
                switch (options.disposition)
                {
                    case FileCreateDisposition::OpenExisting:
                        creationDisposition = OPEN_EXISTING;
                        break;
                    case FileCreateDisposition::CreateAlways:
                        creationDisposition = CREATE_ALWAYS;
                        break;
                    case FileCreateDisposition::CreateNew:
                        creationDisposition = CREATE_NEW;
                        break;
                    case FileCreateDisposition::OpenAlways:
                        creationDisposition = OPEN_ALWAYS;
                        break;
                    case FileCreateDisposition::TruncateExisting:
                        creationDisposition = TRUNCATE_EXISTING;
                        break;
                }

                DWORD flags = FILE_ATTRIBUTE_NORMAL;
                if ((options.flags & FileOpenFlags::WriteThrough) == FileOpenFlags::WriteThrough)
                    flags |= FILE_FLAG_WRITE_THROUGH;
                if ((options.flags & FileOpenFlags::DeleteOnClose) == FileOpenFlags::DeleteOnClose)
                    flags |= FILE_FLAG_DELETE_ON_CLOSE;
                if ((options.flags & FileOpenFlags::Sequential) == FileOpenFlags::Sequential)
                    flags |= FILE_FLAG_SEQUENTIAL_SCAN;
                if ((options.flags & FileOpenFlags::RandomAccess) == FileOpenFlags::RandomAccess)
                    flags |= FILE_FLAG_RANDOM_ACCESS;

                m_handle = CreateFileW(ToNativePath(path).c_str(), desiredAccess, shareMode, nullptr, creationDisposition, flags, nullptr);
                if (m_handle == INVALID_HANDLE_VALUE)
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "CreateFileW failed", m_path)));
                return {};
            }

            mutable std::mutex m_mutex {};
            Path               m_path {};
            bool               m_canRead {false};
            bool               m_canWrite {false};
            HANDLE             m_handle {INVALID_HANDLE_VALUE};
        };

        class LocalDirectoryHandle final : public IDirectoryHandle
        {
        public:
            static Result<std::unique_ptr<LocalDirectoryHandle>> Open(const Path& path) noexcept
            {
                auto info = BuildFileInfo(path, {});
                if (!info.HasValue())
                {
                    return Result<std::unique_ptr<LocalDirectoryHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(std::move(info.Error())));
                }
                if (!info.Value().exists)
                {
                    return Result<std::unique_ptr<LocalDirectoryHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "directory not found", path)));
                }
                if (info.Value().type != EntryType::Directory)
                {
                    return Result<std::unique_ptr<LocalDirectoryHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotDirectory, "path is not a directory", path)));
                }

                try
                {
                    auto handle    = std::unique_ptr<LocalDirectoryHandle>(new LocalDirectoryHandle());
                    handle->m_path = path.LexicallyNormal();
                    return Result<std::unique_ptr<LocalDirectoryHandle>>(std::move(handle));
                } catch (const std::bad_alloc&)
                {
                    return Result<std::unique_ptr<LocalDirectoryHandle>>(
                            NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "allocation failed", path)));
                }
            }

            Result<bool> Exists(const Path& path) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return Result<bool>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));
                return m_fileSystem.Exists(JoinHandlePath(m_path, normalized.Value()));
            }

            Result<FileInfo> GetInfo(const Path& path, const MetadataOptions& options) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return Result<FileInfo>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));
                return m_fileSystem.GetInfo(JoinHandlePath(m_path, normalized.Value()), options);
            }

            Result<FileHandle> OpenFile(const Path& path, const FileOpenOptions& options) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                {
                    return Result<FileHandle>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));
                }
                return m_fileSystem.OpenFile(JoinHandlePath(m_path, normalized.Value()), options);
            }

            Result<DirectoryHandle> OpenDirectory(const Path& path) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                {
                    return Result<DirectoryHandle>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));
                }

                auto opened = Open(JoinHandlePath(m_path, normalized.Value()));
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
                    return m_fileSystem.CreateDirectories(resolvedPath, options);
                return m_fileSystem.CreateDirectory(resolvedPath, options);
            }

            ResultVoid RemoveFile(const Path& path, const RemoveOptions& options = {}) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));
                return m_fileSystem.RemoveFile(JoinHandlePath(m_path, normalized.Value()), options);
            }

            ResultVoid RemoveDirectory(const Path& path, const RemoveOptions& options = {}) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));
                return m_fileSystem.RemoveDirectory(JoinHandlePath(m_path, normalized.Value()), options);
            }

            Result<Path> ReadSymlink(const Path& path) noexcept override
            {
                auto normalized = NormalizeRelativeHandlePath(path);
                if (!normalized.HasValue())
                    return Result<Path>(NGIN::Utilities::Unexpected<IOError>(std::move(normalized.Error())));
                return m_fileSystem.ReadSymlink(JoinHandlePath(m_path, normalized.Value()));
            }

        private:
            Path            m_path {};
            LocalFileSystem m_fileSystem {};
        };

        [[nodiscard]] Result<bool> ExistsNative(const Path& path) noexcept
        {
            const auto attributes = GetFileAttributesW(ToNativePath(path).c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                const DWORD error = GetLastError();
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_INVALID_NAME)
                    return Result<bool>(false);
                return Result<bool>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(error, "GetFileAttributesW failed", path)));
            }
            return Result<bool>(true);
        }

        ResultVoid CreateDirectoryNative(const Path& path, const DirectoryCreateOptions& options) noexcept
        {
            if (CreateDirectoryW(ToNativePath(path).c_str(), nullptr) != 0)
                return {};

            const DWORD error = GetLastError();
            if (options.ignoreIfExists && (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS))
            {
                auto existing = BuildFileInfo(path, {});
                if (existing.HasValue() && existing.Value().exists && existing.Value().type == EntryType::Directory)
                    return {};
            }
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(error, "CreateDirectoryW failed", path)));
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
            if (DeleteFileW(ToNativePath(path).c_str()) != 0)
                return {};

            const DWORD error = GetLastError();
            if (options.ignoreMissing && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_INVALID_NAME))
                return {};
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(error, "DeleteFileW failed", path)));
        }

        ResultVoid     RemoveDirectoryNative(const Path& path, const RemoveOptions& options) noexcept;
        Result<UInt64> RemoveAllNative(const Path& path, const RemoveOptions& options) noexcept;

        [[nodiscard]] Result<std::vector<DirectoryEntry>> EnumerateEntries(const Path& path, const EnumerateOptions& options) noexcept
        {
            std::vector<DirectoryEntry> entries;

            const auto collect = [&](const auto& self, const Path& directoryPath) -> ResultVoid {
                std::wstring pattern = ToNativePath(directoryPath);
                if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/')
                    pattern.push_back(L'\\');
                pattern.push_back(L'*');

                WIN32_FIND_DATAW findData {};
                HANDLE           handle = FindFirstFileW(pattern.c_str(), &findData);
                if (handle == INVALID_HANDLE_VALUE)
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "FindFirstFileW failed", directoryPath)));

                do
                {
                    const std::wstring_view nameView {findData.cFileName};
                    if (nameView == L"." || nameView == L"..")
                        continue;

                    const Path childName = FromNativePath(nameView);
                    Path       childPath = directoryPath.Join(childName.View());

                    MetadataOptions metadataOptions;
                    metadataOptions.symlinkMode = options.followSymlinks ? SymlinkMode::Follow : SymlinkMode::DoNotFollow;
                    auto infoResult             = BuildFileInfo(childPath, metadataOptions);
                    if (!infoResult.HasValue())
                    {
                        const IOError error = std::move(infoResult.Error());
                        FindClose(handle);
                        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(error)));
                    }

                    DirectoryEntry entry;
                    entry.path = childPath;
                    entry.name = childName;
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
                            FindClose(handle);
                            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(error)));
                        }
                    }
                } while (FindNextFileW(handle, &findData) != 0);

                const DWORD lastError = GetLastError();
                FindClose(handle);
                if (lastError != ERROR_NO_MORE_FILES)
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(lastError, "FindNextFileW failed", directoryPath)));
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
            EnumerateOptions enumerateOptions;
            enumerateOptions.recursive       = true;
            enumerateOptions.includeSymlinks = true;
            enumerateOptions.stableSort      = true;
            auto entries                     = EnumerateEntries(path, enumerateOptions);
            if (!entries.HasValue())
                return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(std::move(entries.Error())));

            UInt64 removed = 0;
            auto&  values  = entries.Value();
            for (auto it = values.rbegin(); it != values.rend(); ++it)
            {
                auto entryInfo = BuildFileInfo(it->path, {});
                if (!entryInfo.HasValue())
                    return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(std::move(entryInfo.Error())));

                ResultVoid result;
                if (entryInfo.Value().type == EntryType::Directory)
                    result = RemoveDirectoryNative(it->path, options);
                else
                    result = RemoveFileNative(it->path, options);
                if (!result.HasValue())
                    return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(std::move(result.Error())));
                ++removed;
            }

            auto rootInfo = BuildFileInfo(path, {});
            if (!rootInfo.HasValue())
                return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(std::move(rootInfo.Error())));
            if (!rootInfo.Value().exists)
            {
                if (options.ignoreMissing)
                    return Result<UInt64>(UInt64 {0});
                return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotFound, "path not found", path)));
            }

            ResultVoid removeRoot;
            if (rootInfo.Value().type == EntryType::Directory)
                removeRoot = RemoveDirectoryNative(path, options);
            else
                removeRoot = RemoveFileNative(path, options);
            if (!removeRoot.HasValue())
                return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(std::move(removeRoot.Error())));

            return Result<UInt64>(removed + 1);
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

            if (RemoveDirectoryW(ToNativePath(path).c_str()) != 0)
                return {};
            const DWORD error = GetLastError();
            if (options.ignoreMissing && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_INVALID_NAME))
                return {};
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(error, "RemoveDirectoryW failed", path)));
        }
    }// namespace

    FileSystemCapabilities LocalFileSystem::GetCapabilities() const noexcept
    {
        FileSystemCapabilities capabilities;
        capabilities.symlinks             = true;
        capabilities.hardLinks            = true;
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
        auto canonical = CanonicalizeExistingPath(path);
        if (canonical.HasValue())
            return canonical;
        return MakeAbsolutePath(path, {});
    }

    Result<bool> LocalFileSystem::SameFile(const Path& lhs, const Path& rhs) noexcept
    {
        auto lhsInfo = BuildFileInfo(lhs, MetadataOptions {.symlinkMode = SymlinkMode::Follow});
        if (!lhsInfo.HasValue())
            return Result<bool>(NGIN::Utilities::Unexpected<IOError>(std::move(lhsInfo.Error())));

        auto rhsInfo = BuildFileInfo(rhs, MetadataOptions {.symlinkMode = SymlinkMode::Follow});
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
        return Result<Path>(
                NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::Unsupported, "read symlink is not implemented on Windows", path)));
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
        DWORD flags      = 0;
        auto  targetInfo = BuildFileInfo(target, MetadataOptions {});
        if (targetInfo.HasValue() && targetInfo.Value().exists && targetInfo.Value().type == EntryType::Directory)
            flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
#if defined(SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)
        flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#endif
        if (CreateSymbolicLinkW(ToNativePath(linkPath).c_str(), ToNativePath(target).c_str(), flags) != 0)
            return {};
        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "CreateSymbolicLinkW failed", linkPath, target)));
    }

    ResultVoid LocalFileSystem::CreateHardLink(const Path& target, const Path& linkPath) noexcept
    {
        if (CreateHardLinkW(ToNativePath(linkPath).c_str(), ToNativePath(target).c_str(), nullptr) != 0)
            return {};
        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "CreateHardLinkW failed", linkPath, target)));
    }

    ResultVoid LocalFileSystem::SetPermissions(const Path& path, const FilePermissions& permissions, const SymlinkMode) noexcept
    {
        DWORD attributes = GetFileAttributesW(ToNativePath(path).c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "GetFileAttributesW failed", path)));

        if (permissions.writable)
            attributes &= ~FILE_ATTRIBUTE_READONLY;
        else
            attributes |= FILE_ATTRIBUTE_READONLY;

        if (SetFileAttributesW(ToNativePath(path).c_str(), attributes) != 0)
            return {};
        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "SetFileAttributesW failed", path)));
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
        if (MoveFileExW(ToNativePath(from).c_str(), ToNativePath(to).c_str(), MOVEFILE_REPLACE_EXISTING) != 0)
            return {};
        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "MoveFileExW failed", from, to)));
    }

    ResultVoid LocalFileSystem::ReplaceFile(const Path& source, const Path& destination) noexcept
    {
        if (MoveFileExW(ToNativePath(source).c_str(), ToNativePath(destination).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0)
            return {};
        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "MoveFileExW replace failed", source, destination)));
    }

    ResultVoid LocalFileSystem::CopyFile(const Path& from, const Path& to, const CopyOptions& options) noexcept
    {
        if (!options.recursive)
        {
            const BOOL failIfExists = options.overwriteExisting ? FALSE : TRUE;
            if (CopyFileW(ToNativePath(from).c_str(), ToNativePath(to).c_str(), failIfExists) != 0)
                return {};
        }
        return ResultVoid(
                NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotSupported, "native recursive copy not implemented on Windows", from, to)));
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
        DWORD required = GetCurrentDirectoryW(0, nullptr);
        if (required == 0)
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "GetCurrentDirectoryW failed")));

        std::wstring buffer(static_cast<std::size_t>(required), L'\0');
        const DWORD  count = GetCurrentDirectoryW(required, buffer.data());
        if (count == 0)
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "GetCurrentDirectoryW failed")));
        if (!buffer.empty() && buffer.back() == L'\0')
            buffer.pop_back();
        return Result<Path>(FromNativePath(buffer));
    }

    ResultVoid LocalFileSystem::SetCurrentWorkingDirectory(const Path& path) noexcept
    {
        if (SetCurrentDirectoryW(ToNativePath(path).c_str()) != 0)
            return {};
        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "SetCurrentDirectoryW failed", path)));
    }

    Result<Path> LocalFileSystem::TempDirectory() noexcept
    {
        std::wstring buffer(MAX_PATH, L'\0');
        const DWORD  count = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
        if (count == 0 || count > buffer.size())
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "GetTempPathW failed")));
        buffer.resize(count);
        if (!buffer.empty() && (buffer.back() == L'\\' || buffer.back() == L'/'))
            buffer.pop_back();
        return Result<Path>(FromNativePath(buffer));
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

        for (unsigned int attempt = 0; attempt < 128; ++attempt)
        {
            const auto candidate =
                    baseDirectory.Join(std::string(prefix) + "_" + std::to_string(GetTickCount64()) + "_" + std::to_string(attempt));
            if (CreateDirectoryW(ToNativePath(candidate).c_str(), nullptr) != 0)
                return Result<Path>(candidate);

            const DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS)
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(error, "CreateDirectoryW temp failed", candidate)));
        }

        return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::Busy, "unable to allocate unique temp directory", baseDirectory)));
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

        wchar_t    tempBuffer[MAX_PATH] {};
        const auto baseNative = ToNativePath(baseDirectory);
        if (GetTempFileNameW(baseNative.c_str(), std::wstring(prefix.begin(), prefix.end()).c_str(), 0, tempBuffer) == 0)
        {
            return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "GetTempFileNameW failed", baseDirectory)));
        }
        return Result<Path>(FromNativePath(std::wstring_view(tempBuffer)));
    }

    Result<SpaceInfo> LocalFileSystem::GetSpaceInfo(const Path& path) noexcept
    {
        ULARGE_INTEGER available {};
        ULARGE_INTEGER capacity {};
        ULARGE_INTEGER free {};
        if (GetDiskFreeSpaceExW(ToNativePath(path).c_str(), &available, &capacity, &free) == 0)
        {
            return Result<SpaceInfo>(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "GetDiskFreeSpaceExW failed", path)));
        }

        SpaceInfo info;
        info.available = available.QuadPart;
        info.capacity  = capacity.QuadPart;
        info.free      = free.QuadPart;
        return Result<SpaceInfo>(info);
    }

    namespace
    {
        struct OpenedAsyncWindowsFile final
        {
            HANDLE handle {INVALID_HANDLE_VALUE};
            Path   path {};
            bool   canRead {false};
            bool   canWrite {false};
            bool   appendMode {false};
            UInt64 cursor {0};
        };

        struct LocalAsyncFileState final
        {
            std::shared_ptr<FileSystemDriver> driver {};
            HANDLE                            handle {INVALID_HANDLE_VALUE};
            Path                              path {};
            bool                              canRead {false};
            bool                              canWrite {false};
            bool                              appendMode {false};
            UInt64                            cursor {0};
            mutable std::mutex                mutex {};
        };

        struct LocalAsyncDirectoryState final
        {
            std::shared_ptr<FileSystemDriver> driver {};
            std::unique_ptr<LocalDirectoryHandle> handle {};
            Path                              path {};
        };

        extern const AsyncDirectoryHandle::Operations LocalAsyncDirectoryOperations;

        [[nodiscard]] Result<OpenedAsyncWindowsFile> OpenAsyncWindowsFile(const Path& path, const FileOpenOptions& options) noexcept
        {
            OpenedAsyncWindowsFile opened;
            opened.path       = path;
            opened.canRead    = options.access == FileAccess::Read || options.access == FileAccess::ReadWrite;
            opened.canWrite   = options.access == FileAccess::Write || options.access == FileAccess::ReadWrite || options.access == FileAccess::Append;
            opened.appendMode = options.access == FileAccess::Append;

            DWORD desiredAccess = 0;
            switch (options.access)
            {
                case FileAccess::Read:
                    desiredAccess = GENERIC_READ;
                    break;
                case FileAccess::Write:
                    desiredAccess = GENERIC_WRITE;
                    break;
                case FileAccess::ReadWrite:
                    desiredAccess = GENERIC_READ | GENERIC_WRITE;
                    break;
                case FileAccess::Append:
                    desiredAccess = FILE_APPEND_DATA;
                    break;
            }

            DWORD shareMode = 0;
            if ((options.share & FileShare::Read) == FileShare::Read)
                shareMode |= FILE_SHARE_READ;
            if ((options.share & FileShare::Write) == FileShare::Write)
                shareMode |= FILE_SHARE_WRITE;
            if ((options.share & FileShare::Delete) == FileShare::Delete)
                shareMode |= FILE_SHARE_DELETE;

            DWORD creationDisposition = OPEN_EXISTING;
            switch (options.disposition)
            {
                case FileCreateDisposition::OpenExisting:
                    creationDisposition = OPEN_EXISTING;
                    break;
                case FileCreateDisposition::CreateAlways:
                    creationDisposition = CREATE_ALWAYS;
                    break;
                case FileCreateDisposition::CreateNew:
                    creationDisposition = CREATE_NEW;
                    break;
                case FileCreateDisposition::OpenAlways:
                    creationDisposition = OPEN_ALWAYS;
                    break;
                case FileCreateDisposition::TruncateExisting:
                    creationDisposition = TRUNCATE_EXISTING;
                    break;
            }

            DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED;
            if ((options.flags & FileOpenFlags::WriteThrough) == FileOpenFlags::WriteThrough)
                flags |= FILE_FLAG_WRITE_THROUGH;
            if ((options.flags & FileOpenFlags::DeleteOnClose) == FileOpenFlags::DeleteOnClose)
                flags |= FILE_FLAG_DELETE_ON_CLOSE;
            if ((options.flags & FileOpenFlags::Sequential) == FileOpenFlags::Sequential)
                flags |= FILE_FLAG_SEQUENTIAL_SCAN;
            if ((options.flags & FileOpenFlags::RandomAccess) == FileOpenFlags::RandomAccess)
                flags |= FILE_FLAG_RANDOM_ACCESS;

            opened.handle = CreateFileW(ToNativePath(path).c_str(), desiredAccess, shareMode, nullptr, creationDisposition, flags, nullptr);
            if (opened.handle == INVALID_HANDLE_VALUE)
            {
                return Result<OpenedAsyncWindowsFile>(
                        NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "CreateFileW failed", path)));
            }

            if (opened.appendMode)
            {
                LARGE_INTEGER size {};
                if (GetFileSizeEx(opened.handle, &size))
                {
                    opened.cursor = static_cast<UInt64>(size.QuadPart);
                }
            }
            return Result<OpenedAsyncWindowsFile>(std::move(opened));
        }

        [[nodiscard]] UInt64 ReserveSequentialOffset(LocalAsyncFileState& state, const UInt64 requestedSize) noexcept
        {
            std::lock_guard<std::mutex> guard(state.mutex);
            UInt64 offset = state.cursor;
            if (state.appendMode)
            {
                LARGE_INTEGER size {};
                if (GetFileSizeEx(state.handle, &size))
                {
                    offset      = static_cast<UInt64>(size.QuadPart);
                    state.cursor = offset;
                }
            }
            state.cursor += requestedSize;
            return offset;
        }

        [[nodiscard]] Result<UIntSize> LocalAsyncFileReadAtSync(
                LocalAsyncFileState& state, const UInt64 offset, std::span<NGIN::Byte> destination) noexcept
        {
            std::lock_guard<std::mutex> guard(state.mutex);
            if (state.handle == INVALID_HANDLE_VALUE)
                return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", state.path)));
            if (!state.canRead)
                return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotSupported, "file not opened for read", state.path)));
            if (destination.empty())
                return Result<UIntSize>(UIntSize {0});

            OVERLAPPED overlapped {};
            overlapped.Offset     = static_cast<DWORD>(offset & 0xffffffffULL);
            overlapped.OffsetHigh = static_cast<DWORD>((offset >> 32u) & 0xffffffffULL);

            DWORD bytesRead = 0;
            if (!ReadFile(state.handle, destination.data(), static_cast<DWORD>(destination.size()), &bytesRead, &overlapped))
            {
                return Result<UIntSize>(
                        NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "ReadFile overlapped failed", state.path)));
            }
            return Result<UIntSize>(static_cast<UIntSize>(bytesRead));
        }

        [[nodiscard]] Result<UIntSize> LocalAsyncFileWriteAtSync(
                LocalAsyncFileState& state, const UInt64 offset, std::span<const NGIN::Byte> source) noexcept
        {
            std::lock_guard<std::mutex> guard(state.mutex);
            if (state.handle == INVALID_HANDLE_VALUE)
                return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", state.path)));
            if (!state.canWrite)
                return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::NotSupported, "file not opened for write", state.path)));
            if (source.empty())
                return Result<UIntSize>(UIntSize {0});

            OVERLAPPED overlapped {};
            overlapped.Offset     = static_cast<DWORD>(offset & 0xffffffffULL);
            overlapped.OffsetHigh = static_cast<DWORD>((offset >> 32u) & 0xffffffffULL);

            DWORD bytesWritten = 0;
            if (!WriteFile(state.handle, source.data(), static_cast<DWORD>(source.size()), &bytesWritten, &overlapped))
            {
                return Result<UIntSize>(
                        NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "WriteFile overlapped failed", state.path)));
            }
            return Result<UIntSize>(static_cast<UIntSize>(bytesWritten));
        }

        [[nodiscard]] Result<UIntSize> LocalAsyncFileReadSync(LocalAsyncFileState& state, std::span<NGIN::Byte> destination) noexcept
        {
            return LocalAsyncFileReadAtSync(state, ReserveSequentialOffset(state, destination.size()), destination);
        }

        [[nodiscard]] Result<UIntSize> LocalAsyncFileWriteSync(LocalAsyncFileState& state, std::span<const NGIN::Byte> source) noexcept
        {
            return LocalAsyncFileWriteAtSync(state, ReserveSequentialOffset(state, source.size()), source);
        }

        [[nodiscard]] ResultVoid LocalAsyncFileFlushSync(LocalAsyncFileState& state) noexcept
        {
            std::lock_guard<std::mutex> guard(state.mutex);
            if (state.handle == INVALID_HANDLE_VALUE)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::InvalidArgument, "file not open", state.path)));
            if (!FlushFileBuffers(state.handle))
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeWindowsError(GetLastError(), "FlushFileBuffers failed", state.path)));
            return {};
        }

        [[nodiscard]] ResultVoid LocalAsyncFileCloseSync(LocalAsyncFileState& state) noexcept
        {
            std::lock_guard<std::mutex> guard(state.mutex);
            if (state.handle != INVALID_HANDLE_VALUE)
            {
                ::CloseHandle(state.handle);
                state.handle = INVALID_HANDLE_VALUE;
            }
            return {};
        }

        struct NativeWindowsFileCompletion
        {
            enum class Status : UInt8
            {
                Completed,
                Canceled,
                Fault,
            };

            Status                  status {Status::Fault};
            Int64                   value {0};
            int                     systemCode {0};
            NGIN::Async::AsyncFault fault {};
        };

        class NativeWindowsFileAwaiter
        {
        public:
            NativeWindowsFileAwaiter(detail::NativeFileBackend& backend,
                                     NGIN::Async::TaskContext& ctx,
                                     detail::NativeFileRequest request) noexcept
                : m_backend(backend)
                , m_resumeExecutor(ctx.GetExecutor())
                , m_cancellation(ctx.GetCancellationToken())
                , m_request(std::move(request))
                , m_state(std::make_shared<State>())
            {
            }

            [[nodiscard]] bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> awaiting) noexcept
            {
                m_state->m_resumeExecutor = m_resumeExecutor;
                m_state->m_awaiting       = awaiting;

                if (m_cancellation.IsCancellationRequested())
                {
                    m_state->completion.status = NativeWindowsFileCompletion::Status::Canceled;
                    m_state->Resume();
                    return;
                }

                m_request.userData = m_state.get();
                m_request.completion = +[](void* rawState, detail::NativeFileCompletion completion) noexcept {
                    auto* state = static_cast<State*>(rawState);
                    if (state == nullptr)
                    {
                        return;
                    }
                    if (completion.status == detail::NativeFileCompletion::Status::Fault)
                    {
                        state->completion.status = NativeWindowsFileCompletion::Status::Fault;
                        state->completion.fault  = completion.fault;
                    }
                    else
                    {
                        state->completion.status     = NativeWindowsFileCompletion::Status::Completed;
                        state->completion.value      = completion.value;
                        state->completion.systemCode = completion.systemCode;
                    }
                    state->Resume();
                };

                if (!m_backend.Submit(m_request))
                {
                    m_state->completion.status = NativeWindowsFileCompletion::Status::Fault;
                    m_state->completion.fault  = NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::SchedulerFailure);
                    m_state->Resume();
                }
            }

            [[nodiscard]] NativeWindowsFileCompletion await_resume() noexcept
            {
                return std::move(m_state->completion);
            }

        private:
            struct State
            {
                NGIN::Execution::ExecutorRef m_resumeExecutor {};
                std::coroutine_handle<>      m_awaiting {};
                NativeWindowsFileCompletion  completion {};

                void Resume() noexcept
                {
                    if (m_awaiting)
                    {
                        if (m_resumeExecutor.IsValid())
                        {
                            m_resumeExecutor.Execute(m_awaiting);
                        }
                        else
                        {
                            m_awaiting.resume();
                        }
                    }
                }
            };

            detail::NativeFileBackend&     m_backend;
            NGIN::Execution::ExecutorRef   m_resumeExecutor {};
            NGIN::Async::CancellationToken m_cancellation {};
            detail::NativeFileRequest      m_request {};
            std::shared_ptr<State>         m_state {};
        };

        [[nodiscard]] auto SubmitNativeWindowsFile(detail::NativeFileBackend& backend,
                                                   NGIN::Async::TaskContext& ctx,
                                                   detail::NativeFileRequest request) noexcept
        {
            return NativeWindowsFileAwaiter(backend, ctx, std::move(request));
        }

        AsyncTask<UIntSize> LocalAsyncFileRead(
                const std::shared_ptr<void>& rawState, NGIN::Async::TaskContext& ctx, std::span<NGIN::Byte> destination)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                const auto offset = ReserveSequentialOffset(*state, destination.size());
                auto completion = co_await SubmitNativeWindowsFile(
                        *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind = detail::NativeFileOperationKind::Read,
                                .handleValue = reinterpret_cast<std::uintptr_t>(state->handle),
                                .offset = offset,
                                .buffer = destination.data(),
                                .size = static_cast<UInt32>(destination.size()),
                        });
                if (completion.status == NativeWindowsFileCompletion::Status::Canceled)
                {
                    co_await AsyncTask<UIntSize>::ReturnCanceled();
                    co_return 0;
                }
                if (completion.status == NativeWindowsFileCompletion::Status::Fault)
                {
                    co_await AsyncTask<UIntSize>::ReturnFault(completion.fault);
                    co_return 0;
                }
                if (completion.systemCode != 0)
                {
                    co_return NGIN::Utilities::Unexpected<IOError>(
                            MakeWindowsError(static_cast<DWORD>(completion.systemCode), "ReadFile overlapped failed", state->path));
                }
                co_return static_cast<UIntSize>(completion.value);
            }
            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state, destination]() mutable noexcept {
                return LocalAsyncFileReadSync(*state, destination);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTask<UIntSize>::ReturnCanceled();
                co_return 0;
            }
            if (completion.IsFault())
            {
                co_await AsyncTask<UIntSize>::ReturnFault(std::move(*completion.fault));
                co_return 0;
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).TakeError());
            co_return std::move(result).TakeValue();
        }

        AsyncTask<UIntSize> LocalAsyncFileWrite(
                const std::shared_ptr<void>& rawState, NGIN::Async::TaskContext& ctx, std::span<const NGIN::Byte> source)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                const auto offset = ReserveSequentialOffset(*state, source.size());
                auto completion = co_await SubmitNativeWindowsFile(
                        *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind = detail::NativeFileOperationKind::Write,
                                .handleValue = reinterpret_cast<std::uintptr_t>(state->handle),
                                .offset = offset,
                                .buffer = const_cast<NGIN::Byte*>(source.data()),
                                .size = static_cast<UInt32>(source.size()),
                        });
                if (completion.status == NativeWindowsFileCompletion::Status::Canceled)
                {
                    co_await AsyncTask<UIntSize>::ReturnCanceled();
                    co_return 0;
                }
                if (completion.status == NativeWindowsFileCompletion::Status::Fault)
                {
                    co_await AsyncTask<UIntSize>::ReturnFault(completion.fault);
                    co_return 0;
                }
                if (completion.systemCode != 0)
                {
                    co_return NGIN::Utilities::Unexpected<IOError>(
                            MakeWindowsError(static_cast<DWORD>(completion.systemCode), "WriteFile overlapped failed", state->path));
                }
                co_return static_cast<UIntSize>(completion.value);
            }
            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state, source]() mutable noexcept {
                return LocalAsyncFileWriteSync(*state, source);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTask<UIntSize>::ReturnCanceled();
                co_return 0;
            }
            if (completion.IsFault())
            {
                co_await AsyncTask<UIntSize>::ReturnFault(std::move(*completion.fault));
                co_return 0;
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).TakeError());
            co_return std::move(result).TakeValue();
        }

        AsyncTask<UIntSize> LocalAsyncFileReadAt(const std::shared_ptr<void>& rawState,
                                                 NGIN::Async::TaskContext&  ctx,
                                                 UInt64                     offset,
                                                 std::span<NGIN::Byte>      destination)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                auto completion = co_await SubmitNativeWindowsFile(
                        *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind = detail::NativeFileOperationKind::Read,
                                .handleValue = reinterpret_cast<std::uintptr_t>(state->handle),
                                .offset = offset,
                                .buffer = destination.data(),
                                .size = static_cast<UInt32>(destination.size()),
                        });
                if (completion.status == NativeWindowsFileCompletion::Status::Canceled)
                {
                    co_await AsyncTask<UIntSize>::ReturnCanceled();
                    co_return 0;
                }
                if (completion.status == NativeWindowsFileCompletion::Status::Fault)
                {
                    co_await AsyncTask<UIntSize>::ReturnFault(completion.fault);
                    co_return 0;
                }
                if (completion.systemCode != 0)
                {
                    co_return NGIN::Utilities::Unexpected<IOError>(
                            MakeWindowsError(static_cast<DWORD>(completion.systemCode), "ReadFile overlapped failed", state->path));
                }
                co_return static_cast<UIntSize>(completion.value);
            }
            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state, offset, destination]() mutable noexcept {
                return LocalAsyncFileReadAtSync(*state, offset, destination);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTask<UIntSize>::ReturnCanceled();
                co_return 0;
            }
            if (completion.IsFault())
            {
                co_await AsyncTask<UIntSize>::ReturnFault(std::move(*completion.fault));
                co_return 0;
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).TakeError());
            co_return std::move(result).TakeValue();
        }

        AsyncTask<UIntSize> LocalAsyncFileWriteAt(const std::shared_ptr<void>& rawState,
                                                  NGIN::Async::TaskContext&  ctx,
                                                  UInt64                     offset,
                                                  std::span<const NGIN::Byte> source)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                auto completion = co_await SubmitNativeWindowsFile(
                        *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind = detail::NativeFileOperationKind::Write,
                                .handleValue = reinterpret_cast<std::uintptr_t>(state->handle),
                                .offset = offset,
                                .buffer = const_cast<NGIN::Byte*>(source.data()),
                                .size = static_cast<UInt32>(source.size()),
                        });
                if (completion.status == NativeWindowsFileCompletion::Status::Canceled)
                {
                    co_await AsyncTask<UIntSize>::ReturnCanceled();
                    co_return 0;
                }
                if (completion.status == NativeWindowsFileCompletion::Status::Fault)
                {
                    co_await AsyncTask<UIntSize>::ReturnFault(completion.fault);
                    co_return 0;
                }
                if (completion.systemCode != 0)
                {
                    co_return NGIN::Utilities::Unexpected<IOError>(
                            MakeWindowsError(static_cast<DWORD>(completion.systemCode), "WriteFile overlapped failed", state->path));
                }
                co_return static_cast<UIntSize>(completion.value);
            }
            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state, offset, source]() mutable noexcept {
                return LocalAsyncFileWriteAtSync(*state, offset, source);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTask<UIntSize>::ReturnCanceled();
                co_return 0;
            }
            if (completion.IsFault())
            {
                co_await AsyncTask<UIntSize>::ReturnFault(std::move(*completion.fault));
                co_return 0;
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).TakeError());
            co_return std::move(result).TakeValue();
        }

        AsyncTaskVoid LocalAsyncFileFlush(const std::shared_ptr<void>& rawState, NGIN::Async::TaskContext& ctx)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                auto completion = co_await SubmitNativeWindowsFile(
                        *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind = detail::NativeFileOperationKind::Flush,
                                .handleValue = reinterpret_cast<std::uintptr_t>(state->handle),
                        });
                if (completion.status == NativeWindowsFileCompletion::Status::Canceled)
                {
                    co_await AsyncTaskVoid::ReturnCanceled();
                    co_return;
                }
                if (completion.status == NativeWindowsFileCompletion::Status::Fault)
                {
                    co_await AsyncTaskVoid::ReturnFault(completion.fault);
                    co_return;
                }
                if (completion.systemCode != 0)
                {
                    co_await AsyncTaskVoid::ReturnError(
                            MakeWindowsError(static_cast<DWORD>(completion.systemCode), "FlushFileBuffers failed", state->path));
                    co_return;
                }
                co_return;
            }
            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state]() mutable noexcept {
                return LocalAsyncFileFlushSync(*state);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTaskVoid::ReturnCanceled();
                co_return;
            }
            if (completion.IsFault())
            {
                co_await AsyncTaskVoid::ReturnFault(std::move(*completion.fault));
                co_return;
            }
            auto result = std::move(*completion.result);
            if (!result)
            {
                co_await AsyncTaskVoid::ReturnError(std::move(result).TakeError());
                co_return;
            }
            co_return;
        }

        AsyncTaskVoid LocalAsyncFileClose(const std::shared_ptr<void>& rawState, NGIN::Async::TaskContext& ctx)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                HANDLE handleToClose = INVALID_HANDLE_VALUE;
                {
                    std::lock_guard<std::mutex> guard(state->mutex);
                    handleToClose = state->handle;
                    state->handle = INVALID_HANDLE_VALUE;
                }
                if (handleToClose == INVALID_HANDLE_VALUE)
                {
                    co_return;
                }
                auto completion = co_await SubmitNativeWindowsFile(
                        *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind = detail::NativeFileOperationKind::Close,
                                .handleValue = reinterpret_cast<std::uintptr_t>(handleToClose),
                        });
                if (completion.status == NativeWindowsFileCompletion::Status::Canceled)
                {
                    co_await AsyncTaskVoid::ReturnCanceled();
                    co_return;
                }
                if (completion.status == NativeWindowsFileCompletion::Status::Fault)
                {
                    co_await AsyncTaskVoid::ReturnFault(completion.fault);
                    co_return;
                }
                if (completion.systemCode != 0)
                {
                    co_await AsyncTaskVoid::ReturnError(
                            MakeWindowsError(static_cast<DWORD>(completion.systemCode), "CloseHandle failed", state->path));
                    co_return;
                }
                co_return;
            }
            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state]() mutable noexcept {
                return LocalAsyncFileCloseSync(*state);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTaskVoid::ReturnCanceled();
                co_return;
            }
            if (completion.IsFault())
            {
                co_await AsyncTaskVoid::ReturnFault(std::move(*completion.fault));
                co_return;
            }
            co_return;
        }

        [[nodiscard]] bool LocalAsyncFileIsOpen(const std::shared_ptr<void>& rawState) noexcept
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (!state)
                return false;
            std::lock_guard<std::mutex> guard(state->mutex);
            return state->handle != INVALID_HANDLE_VALUE;
        }

        const AsyncFileHandle::Operations LocalAsyncFileOperations {
                .read = &LocalAsyncFileRead,
                .write = &LocalAsyncFileWrite,
                .readAt = &LocalAsyncFileReadAt,
                .writeAt = &LocalAsyncFileWriteAt,
                .flush = &LocalAsyncFileFlush,
                .close = &LocalAsyncFileClose,
                .isOpen = &LocalAsyncFileIsOpen,
        };

        [[nodiscard]] AsyncFileHandle MakeAsyncFileHandle(
                std::shared_ptr<FileSystemDriver> driver, OpenedAsyncWindowsFile opened)
        {
            auto state = std::make_shared<LocalAsyncFileState>();
            state->driver     = std::move(driver);
            state->handle     = opened.handle;
            state->path       = std::move(opened.path);
            state->canRead    = opened.canRead;
            state->canWrite   = opened.canWrite;
            state->appendMode = opened.appendMode;
            state->cursor     = opened.cursor;
            return AsyncFileHandle(std::move(state), &LocalAsyncFileOperations);
        }

        [[nodiscard]] Path ResolveAsyncDirectoryPath(const LocalAsyncDirectoryState& state, const Path& relativePath)
        {
            return JoinHandlePath(state.path, relativePath);
        }

        AsyncTask<bool> LocalAsyncDirectoryExists(
                const std::shared_ptr<void>& rawState, NGIN::Async::TaskContext& ctx, const Path& path)
        {
            auto state      = std::static_pointer_cast<LocalAsyncDirectoryState>(rawState);
            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state, path]() mutable noexcept {
                return state->handle->Exists(path);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTask<bool>::ReturnCanceled();
                co_return false;
            }
            if (completion.IsFault())
            {
                co_await AsyncTask<bool>::ReturnFault(std::move(*completion.fault));
                co_return false;
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).TakeError());
            co_return std::move(result).TakeValue();
        }

        AsyncTask<FileInfo> LocalAsyncDirectoryGetInfo(const std::shared_ptr<void>& rawState,
                                                       NGIN::Async::TaskContext&  ctx,
                                                       const Path&                path,
                                                       const MetadataOptions&     options)
        {
            auto state      = std::static_pointer_cast<LocalAsyncDirectoryState>(rawState);
            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state, path, options]() mutable noexcept {
                return state->handle->GetInfo(path, options);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTask<FileInfo>::ReturnCanceled();
                co_return FileInfo {};
            }
            if (completion.IsFault())
            {
                co_await AsyncTask<FileInfo>::ReturnFault(std::move(*completion.fault));
                co_return FileInfo {};
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).TakeError());
            co_return std::move(result).TakeValue();
        }

        AsyncTask<AsyncFileHandle> LocalAsyncDirectoryOpenFile(const std::shared_ptr<void>& rawState,
                                                               NGIN::Async::TaskContext&  ctx,
                                                               const Path&                path,
                                                               const FileOpenOptions&     options)
        {
            auto state      = std::static_pointer_cast<LocalAsyncDirectoryState>(rawState);
            auto normalized = NormalizeRelativeHandlePath(path);
            if (!normalized.HasValue())
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(normalized).TakeError());

            const Path resolvedPath = ResolveAsyncDirectoryPath(*state, normalized.Value());
            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [resolvedPath, options]() mutable noexcept {
                return OpenAsyncWindowsFile(resolvedPath, options);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTask<AsyncFileHandle>::ReturnCanceled();
                co_return AsyncFileHandle {};
            }
            if (completion.IsFault())
            {
                co_await AsyncTask<AsyncFileHandle>::ReturnFault(std::move(*completion.fault));
                co_return AsyncFileHandle {};
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).TakeError());
            co_return MakeAsyncFileHandle(state->driver, std::move(result).TakeValue());
        }

        AsyncTask<AsyncDirectoryHandle> LocalAsyncDirectoryOpenDirectory(
                const std::shared_ptr<void>& rawState, NGIN::Async::TaskContext& ctx, const Path& path)
        {
            auto state      = std::static_pointer_cast<LocalAsyncDirectoryState>(rawState);
            auto normalized = NormalizeRelativeHandlePath(path);
            if (!normalized.HasValue())
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(normalized).TakeError());

            const Path resolvedPath = ResolveAsyncDirectoryPath(*state, normalized.Value());
            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [resolvedPath]() mutable noexcept {
                return LocalDirectoryHandle::Open(resolvedPath);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTask<AsyncDirectoryHandle>::ReturnCanceled();
                co_return AsyncDirectoryHandle {};
            }
            if (completion.IsFault())
            {
                co_await AsyncTask<AsyncDirectoryHandle>::ReturnFault(std::move(*completion.fault));
                co_return AsyncDirectoryHandle {};
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).TakeError());

            auto nextState    = std::make_shared<LocalAsyncDirectoryState>();
            nextState->driver = state->driver;
            nextState->handle = std::move(result).TakeValue();
            nextState->path   = resolvedPath;
            co_return AsyncDirectoryHandle(std::move(nextState), &LocalAsyncDirectoryOperations);
        }

        AsyncTask<Path> LocalAsyncDirectoryReadSymlink(
                const std::shared_ptr<void>& rawState, NGIN::Async::TaskContext& ctx, const Path& path)
        {
            auto state      = std::static_pointer_cast<LocalAsyncDirectoryState>(rawState);
            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state, path]() mutable noexcept {
                return state->handle->ReadSymlink(path);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTask<Path>::ReturnCanceled();
                co_return Path {};
            }
            if (completion.IsFault())
            {
                co_await AsyncTask<Path>::ReturnFault(std::move(*completion.fault));
                co_return Path {};
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).TakeError());
            co_return std::move(result).TakeValue();
        }

        const AsyncDirectoryHandle::Operations LocalAsyncDirectoryOperations {
                .exists = &LocalAsyncDirectoryExists,
                .getInfo = &LocalAsyncDirectoryGetInfo,
                .openFile = &LocalAsyncDirectoryOpenFile,
                .openDirectory = &LocalAsyncDirectoryOpenDirectory,
                .readSymlink = &LocalAsyncDirectoryReadSymlink,
        };
    }// namespace

    AsyncTask<AsyncFileHandle> LocalFileSystem::OpenFileAsync(
            NGIN::Async::TaskContext& ctx, const Path& path, const FileOpenOptions& options)
    {
        auto completion = co_await detail::DispatchToDriver(*m_asyncDriver, ctx, [path, options]() mutable noexcept {
            return OpenAsyncWindowsFile(path, options);
        });
        if (completion.IsCanceled())
        {
            co_await AsyncTask<AsyncFileHandle>::ReturnCanceled();
            co_return AsyncFileHandle {};
        }
        if (completion.IsFault())
        {
            co_await AsyncTask<AsyncFileHandle>::ReturnFault(std::move(*completion.fault));
            co_return AsyncFileHandle {};
        }
        auto opened = std::move(*completion.result);
        if (!opened)
            co_return NGIN::Utilities::Unexpected<IOError>(std::move(opened).TakeError());
        co_return MakeAsyncFileHandle(m_asyncDriver, std::move(opened).TakeValue());
    }

    AsyncTask<AsyncDirectoryHandle> LocalFileSystem::OpenDirectoryAsync(
            NGIN::Async::TaskContext& ctx, const Path& path)
    {
        auto completion = co_await detail::DispatchToDriver(*m_asyncDriver, ctx, [path]() mutable noexcept {
            return LocalDirectoryHandle::Open(path);
        });
        if (completion.IsCanceled())
        {
            co_await AsyncTask<AsyncDirectoryHandle>::ReturnCanceled();
            co_return AsyncDirectoryHandle {};
        }
        if (completion.IsFault())
        {
            co_await AsyncTask<AsyncDirectoryHandle>::ReturnFault(std::move(*completion.fault));
            co_return AsyncDirectoryHandle {};
        }
        auto opened = std::move(*completion.result);
        if (!opened)
            co_return NGIN::Utilities::Unexpected<IOError>(std::move(opened).TakeError());

        auto state    = std::make_shared<LocalAsyncDirectoryState>();
        state->driver = m_asyncDriver;
        state->handle = std::move(opened).TakeValue();
        state->path   = path.LexicallyNormal();
        co_return AsyncDirectoryHandle(std::move(state), &LocalAsyncDirectoryOperations);
    }
}// namespace NGIN::IO
