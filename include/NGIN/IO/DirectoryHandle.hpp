#pragma once

#include <NGIN/IO/FileHandle.hpp>
#include <NGIN/IO/IDirectoryHandle.hpp>

#include <memory>

namespace NGIN::IO
{
    class NGIN_BASE_API DirectoryHandle
    {
    public:
        DirectoryHandle() noexcept = default;
        explicit DirectoryHandle(std::unique_ptr<IDirectoryHandle> handle) noexcept
            : m_handle(std::move(handle))
        {
        }

        DirectoryHandle(const DirectoryHandle&)                = delete;
        DirectoryHandle& operator=(const DirectoryHandle&)     = delete;
        DirectoryHandle(DirectoryHandle&&) noexcept            = default;
        DirectoryHandle& operator=(DirectoryHandle&&) noexcept = default;
        ~DirectoryHandle()                                     = default;

        [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_handle); }
        explicit           operator bool() const noexcept { return IsValid(); }

        Result<bool> Exists(const Path& path) noexcept
        {
            if (!m_handle)
                return Result<bool>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("directory handle is empty")));
            return m_handle->Exists(path);
        }

        Result<FileInfo> GetInfo(const Path& path, const MetadataOptions& options = {}) noexcept
        {
            if (!m_handle)
                return Result<FileInfo>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("directory handle is empty")));
            return m_handle->GetInfo(path, options);
        }

        Result<FileHandle> OpenFile(const Path& path, const FileOpenOptions& options) noexcept
        {
            if (!m_handle)
                return Result<FileHandle>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("directory handle is empty")));
            return m_handle->OpenFile(path, options);
        }

        Result<DirectoryHandle> OpenDirectory(const Path& path) noexcept
        {
            if (!m_handle)
                return Result<DirectoryHandle>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("directory handle is empty")));
            return m_handle->OpenDirectory(path);
        }

        ResultVoid CreateDirectory(const Path& path, const DirectoryCreateOptions& options = {}) noexcept
        {
            if (!m_handle)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("directory handle is empty")));
            return m_handle->CreateDirectory(path, options);
        }

        ResultVoid RemoveFile(const Path& path, const RemoveOptions& options = {}) noexcept
        {
            if (!m_handle)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("directory handle is empty")));
            return m_handle->RemoveFile(path, options);
        }

        ResultVoid RemoveDirectory(const Path& path, const RemoveOptions& options = {}) noexcept
        {
            if (!m_handle)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("directory handle is empty")));
            return m_handle->RemoveDirectory(path, options);
        }

        Result<Path> ReadSymlink(const Path& path) noexcept
        {
            if (!m_handle)
                return Result<Path>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("directory handle is empty")));
            return m_handle->ReadSymlink(path);
        }

    private:
        [[nodiscard]] static IOError MakeInvalidHandleError(const char* message) noexcept
        {
            IOError error;
            error.code    = IOErrorCode::InvalidArgument;
            error.message = message;
            return error;
        }

        std::unique_ptr<IDirectoryHandle> m_handle {};
    };
}// namespace NGIN::IO
