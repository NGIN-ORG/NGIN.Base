#pragma once

#include <NGIN/IO/FileHandle.hpp>
#include <NGIN/IO/IDirectoryHandle.hpp>

#include <memory>

namespace NGIN::IO
{
    /// @brief Move-only owning facade over an `IDirectoryHandle` implementation.
    class NGIN_IO_API DirectoryHandle
    {
    public:
        /// @brief Constructs an empty handle.
        DirectoryHandle() noexcept = default;
        /// @brief Takes ownership of a concrete directory-handle implementation.
        explicit DirectoryHandle(std::unique_ptr<IDirectoryHandle> handle) noexcept
            : m_handle(std::move(handle))
        {
        }

        /// @brief Directory handles are non-copyable because they uniquely own their implementation.
        DirectoryHandle(const DirectoryHandle&) = delete;
        /// @brief Directory handles are non-copy-assignable because they uniquely own their implementation.
        DirectoryHandle& operator=(const DirectoryHandle&) = delete;
        /// @brief Transfers ownership from another handle.
        DirectoryHandle(DirectoryHandle&&) noexcept = default;
        /// @brief Transfers ownership from another handle.
        DirectoryHandle& operator=(DirectoryHandle&&) noexcept = default;
        /// @brief Destroys the owned implementation.
        ~DirectoryHandle() = default;

        /// @brief Returns whether this facade contains an implementation.
        [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_handle); }
        /// @brief Returns whether this facade contains an implementation.
        explicit operator bool() const noexcept { return IsValid(); }

        /// @brief Returns whether a relative entry exists.
        Result<bool> Exists(const Path& path) noexcept
        {
            if (!m_handle)
                return Result<bool>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("directory handle is empty")));
            return m_handle->Exists(path);
        }

        /// @brief Returns metadata for a relative entry.
        Result<FileInfo> GetInfo(const Path& path, const MetadataOptions& options = {}) noexcept
        {
            if (!m_handle)
                return Result<FileInfo>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("directory handle is empty")));
            return m_handle->GetInfo(path, options);
        }

        /// @brief Opens a file relative to this directory.
        Result<FileHandle> OpenFile(const Path& path, const FileOpenOptions& options) noexcept
        {
            if (!m_handle)
                return Result<FileHandle>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("directory handle is empty")));
            return m_handle->OpenFile(path, options);
        }

        /// @brief Opens a directory relative to this directory.
        Result<DirectoryHandle> OpenDirectory(const Path& path) noexcept
        {
            if (!m_handle)
                return Result<DirectoryHandle>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("directory handle is empty")));
            return m_handle->OpenDirectory(path);
        }

        /// @brief Creates a directory relative to this directory.
        ResultVoid CreateDirectory(const Path& path, const DirectoryCreateOptions& options = {}) noexcept
        {
            if (!m_handle)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("directory handle is empty")));
            return m_handle->CreateDirectory(path, options);
        }

        /// @brief Removes a file relative to this directory.
        ResultVoid RemoveFile(const Path& path, const RemoveOptions& options = {}) noexcept
        {
            if (!m_handle)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("directory handle is empty")));
            return m_handle->RemoveFile(path, options);
        }

        /// @brief Removes a directory relative to this directory.
        ResultVoid RemoveDirectory(const Path& path, const RemoveOptions& options = {}) noexcept
        {
            if (!m_handle)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("directory handle is empty")));
            return m_handle->RemoveDirectory(path, options);
        }

        /// @brief Reads the stored target of a relative symbolic link.
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
