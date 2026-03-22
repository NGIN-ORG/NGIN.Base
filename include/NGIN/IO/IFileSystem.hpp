#pragma once

#include <NGIN/IO/FileSystemTypes.hpp>
#include <NGIN/IO/FileView.hpp>
#include <NGIN/IO/IDirectoryEnumerator.hpp>
#include <NGIN/IO/IFileHandle.hpp>
#include <NGIN/IO/IOResult.hpp>

#include <memory>
#include <string_view>

namespace NGIN::IO
{
    class NGIN_BASE_API IFileSystem
    {
    public:
        virtual ~IFileSystem() = default;

        [[nodiscard]] virtual FileSystemCapabilities GetCapabilities() const noexcept                                        = 0;
        virtual Result<bool>                         Exists(const Path& path) noexcept                                       = 0;
        virtual Result<FileInfo>                     GetInfo(const Path& path, const MetadataOptions& options = {}) noexcept = 0;
        virtual Result<Path>                         Absolute(const Path& path, const Path& base = {}) noexcept              = 0;
        virtual Result<Path>                         Canonical(const Path& path) noexcept                                    = 0;
        virtual Result<Path>                         WeaklyCanonical(const Path& path) noexcept                              = 0;
        virtual Result<bool>                         SameFile(const Path& lhs, const Path& rhs) noexcept                     = 0;
        virtual Result<Path>                         ReadSymlink(const Path& path) noexcept                                  = 0;

        virtual ResultVoid     CreateDirectory(const Path& path, const DirectoryCreateOptions& options = {}) noexcept                                             = 0;
        virtual ResultVoid     CreateDirectories(const Path& path, const DirectoryCreateOptions& options = {}) noexcept                                           = 0;
        virtual ResultVoid     CreateSymlink(const Path& target, const Path& linkPath) noexcept                                                                   = 0;
        virtual ResultVoid     CreateHardLink(const Path& target, const Path& linkPath) noexcept                                                                  = 0;
        virtual ResultVoid     SetPermissions(const Path& path, const FilePermissions& permissions, const SymlinkMode symlinkMode = SymlinkMode::Follow) noexcept = 0;
        virtual ResultVoid     RemoveFile(const Path& path, const RemoveOptions& options = {}) noexcept                                                           = 0;
        virtual ResultVoid     RemoveDirectory(const Path& path, const RemoveOptions& options = {}) noexcept                                                      = 0;
        virtual Result<UInt64> RemoveAll(const Path& path, const RemoveOptions& options = {}) noexcept                                                            = 0;

        virtual ResultVoid Rename(const Path& from, const Path& to) noexcept                                    = 0;
        virtual ResultVoid ReplaceFile(const Path& source, const Path& destination) noexcept                    = 0;
        virtual ResultVoid CopyFile(const Path& from, const Path& to, const CopyOptions& options = {}) noexcept = 0;
        virtual ResultVoid Move(const Path& from, const Path& to, const CopyOptions& options = {}) noexcept     = 0;

        virtual Result<std::unique_ptr<IFileHandle>>          OpenFile(const Path& path, const FileOpenOptions& options) noexcept        = 0;
        virtual Result<FileView>                              OpenFileView(const Path& path) noexcept                                    = 0;
        virtual Result<std::unique_ptr<IDirectoryEnumerator>> Enumerate(const Path& path, const EnumerateOptions& options = {}) noexcept = 0;

        virtual Result<Path>      CurrentWorkingDirectory() noexcept                                                         = 0;
        virtual ResultVoid        SetCurrentWorkingDirectory(const Path& path) noexcept                                      = 0;
        virtual Result<Path>      TempDirectory() noexcept                                                                   = 0;
        virtual Result<Path>      CreateTempDirectory(const Path& directory = {}, std::string_view prefix = "ngin") noexcept = 0;
        virtual Result<Path>      CreateTempFile(const Path& directory = {}, std::string_view prefix = "ngin") noexcept      = 0;
        virtual Result<SpaceInfo> GetSpaceInfo(const Path& path) noexcept                                                    = 0;
    };
}// namespace NGIN::IO
