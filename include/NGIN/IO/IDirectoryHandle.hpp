#pragma once

#include <NGIN/IO/FileSystemTypes.hpp>
#include <NGIN/IO/IOResult.hpp>

namespace NGIN::IO
{
    class FileHandle;
    class DirectoryHandle;

    class NGIN_BASE_API IDirectoryHandle
    {
    public:
        virtual ~IDirectoryHandle() = default;

        virtual Result<bool>            Exists(const Path& path) noexcept                                                      = 0;
        virtual Result<FileInfo>        GetInfo(const Path& path, const MetadataOptions& options = {}) noexcept                = 0;
        virtual Result<FileHandle>      OpenFile(const Path& path, const FileOpenOptions& options) noexcept                    = 0;
        virtual Result<DirectoryHandle> OpenDirectory(const Path& path) noexcept                                               = 0;
        virtual ResultVoid              CreateDirectory(const Path& path, const DirectoryCreateOptions& options = {}) noexcept = 0;
        virtual ResultVoid              RemoveFile(const Path& path, const RemoveOptions& options = {}) noexcept               = 0;
        virtual ResultVoid              RemoveDirectory(const Path& path, const RemoveOptions& options = {}) noexcept          = 0;
        virtual Result<Path>            ReadSymlink(const Path& path) noexcept                                                 = 0;
    };
}// namespace NGIN::IO
