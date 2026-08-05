#pragma once

#include <NGIN/IO/FileSystemTypes.hpp>
#include <NGIN/IO/IOResult.hpp>

namespace NGIN::IO
{
    class FileHandle;
    class DirectoryHandle;

    /// @brief Polymorphic interface for operations relative to an open directory.
    class NGIN_IO_API IDirectoryHandle
    {
    public:
        /// @brief Destroys the interface; concrete handles release their directory resource.
        virtual ~IDirectoryHandle() = default;

        /// @brief Returns whether a relative entry exists.
        virtual Result<bool> Exists(const Path& path) noexcept = 0;
        /// @brief Returns metadata for a relative entry.
        virtual Result<FileInfo> GetInfo(const Path& path, const MetadataOptions& options = {}) noexcept = 0;
        /// @brief Opens a file relative to this directory.
        virtual Result<FileHandle> OpenFile(const Path& path, const FileOpenOptions& options) noexcept = 0;
        /// @brief Opens a directory relative to this directory.
        virtual Result<DirectoryHandle> OpenDirectory(const Path& path) noexcept = 0;
        /// @brief Creates a directory relative to this directory.
        virtual ResultVoid CreateDirectory(const Path& path, const DirectoryCreateOptions& options = {}) noexcept = 0;
        /// @brief Removes a file relative to this directory.
        virtual ResultVoid RemoveFile(const Path& path, const RemoveOptions& options = {}) noexcept = 0;
        /// @brief Removes a directory relative to this directory.
        virtual ResultVoid RemoveDirectory(const Path& path, const RemoveOptions& options = {}) noexcept = 0;
        /// @brief Reads the stored target of a relative symbolic link without following it.
        virtual Result<Path> ReadSymlink(const Path& path) noexcept = 0;
    };
}// namespace NGIN::IO
